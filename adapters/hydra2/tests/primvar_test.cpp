// SPDX-License-Identifier: Apache-2.0
#include "adapter.hpp"
#include "generated_tangents.hpp"
#ifdef TOON_HAS_AVATAR_STATE
#include "avatar_binding.hpp"
#endif

#include <pxr/base/tf/setenv.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <toon/vulkan_backend.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
bool Check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

struct Host {
  HdToonRenderDelegate delegate;
  UsdImagingSceneIndices indices;
  std::unique_ptr<HdRenderIndex> index;
  explicit Host(const UsdStageRefPtr& stage) {
    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage;
    indices = UsdImagingCreateSceneIndices(info);
    index.reset(HdRenderIndex::New(&delegate, {}));
    index->InsertSceneIndex(indices.finalSceneIndex, SdfPath::AbsoluteRootPath());
  }
  Toon::FrameSnapshot Sync(double time = 0) {
    indices.stageSceneIndex->SetTime(UsdTimeCode(time));
    indices.stageSceneIndex->ApplyPendingUpdates();
    index->EnqueueCollectionToSync(HdRprimCollection(HdTokens->geometry,
        HdReprSelector(HdReprTokens->smoothHull)));
    HdTaskSharedPtrVector tasks;
    HdTaskContext context;
    index->SyncAll(&tasks, &context);
    return delegate.CommitScene();
  }
};

#if defined(TOON_HAS_AVATAR_STATE) && AR_ABI_VERSION >= 5
struct UvSnapshot {
  static inline ArStateView view{};
  static ArStatus AR_CALL Get(ArSnapshot, ArStateView* output) {
    *output = view;
    return AR_OK;
  }
  static ArStatus AR_CALL Hold(ArSnapshot) { return AR_OK; }
};
#endif

bool PrimaryUvMapping(Host& host, const std::vector<std::uint32_t>& vertices,
    std::uint32_t source_count = 4) {
  const auto targets = host.delegate.DescribeResidentTargets();
  if (!Check(targets.meshes.size() == 1, "UV mapping check needs one resident mesh")) return false;
  const auto& mapping = targets.meshes[0].primary_uv;
  if (!Check(vertices.empty() ? !mapping :
          mapping && mapping->source_point_count == source_count && mapping->vertices == vertices,
      "resident primary UV identities must follow corner and tangent-seam splits")) return false;
#if defined(TOON_HAS_AVATAR_STATE) && AR_ABI_VERSION >= 5
  if (mapping) {
    const std::string path = targets.meshes[0].path.GetString();
    std::vector<ArVertexUv> uvs;
    for (std::uint32_t i = source_count; i > 0; --i)
      uvs.push_back({path.c_str(), i - 1, {double(i), -double(i)}});
    ArStateView view{AR_HEADER(ArStateView)};
    view.instance = view.generation = view.layout_version = view.frame_id = 1;
    view.layout_id = "primary-uv-test";
    view.vertex_uvs = uvs.data();
    view.vertex_uv_count = source_count;
    const auto match = HdToonMatchAvatarTargets(view, targets, {});
    if (!Check(match.mismatches.empty() && match.bindings.uvs.size() == 1,
            "Hydra UV identities must automatically match reversed runtime slots")) return false;
    const auto& binding = match.bindings.uvs[0];
    if (!Check(binding.mesh == targets.meshes[0].mesh && binding.sources.size() == vertices.size(),
            "matched UV binding must cover the resident mesh")) return false;
    for (std::size_t i = 0; i < vertices.size(); ++i)
      if (!Check(binding.sources[i] == source_count - 1 - vertices[i],
              "matched UV slot lost a corner or tangent-seam identity")) return false;
    const auto baseline = host.delegate.CommitScene();
    Toon::AvatarStateAdapter adapter;
    std::string error;
    const bool bound = adapter.Bind(view, baseline, 1, match.bindings, error);
    if (!baseline.meshes[0].tangents->empty()) {
      if (!Check(!bound && error.find("tangents") != std::string::npos,
              "automatic UV matching must preserve resident tangent refusal")) return false;
    } else {
      if (!Check(bound, error.c_str())) return false;
      ArRuntimeApi api{AR_HEADER(ArRuntimeApi)};
      api.get_snapshot = UvSnapshot::Get;
      api.retain_snapshot = api.release_snapshot = UvSnapshot::Hold;
      UvSnapshot::view = view;
      Toon::RetainedAvatarSnapshot held;
      Toon::FrameSnapshot output;
      const bool applied = held.Reset(api, 1, error) && adapter.Apply(held, baseline, 1, output, error);
      if (!Check(applied, error.c_str())) return false;
      for (std::size_t i = 0; i < vertices.size(); ++i) {
        const float value = static_cast<float>(vertices[i] + 1);
        if (!Check((*output.meshes[0].uvs)[i] == Toon::Float2{value, -value},
                "automatically bound UV publication differs from source point identities")) return false;
      }
      if (!Check(adapter.Release(baseline, 1, output, error) &&
                     *output.meshes[0].uvs == *baseline.meshes[0].uvs,
              "automatic UV binding release must restore the Hydra baseline")) return false;
    }
  }
#endif
  return true;
}

bool SameGeometry(const Toon::MeshSnapshot& a, const Toon::MeshSnapshot& b) {
  return a.points_revision == b.points_revision &&
         a.topology_revision == b.topology_revision &&
         a.normals_revision == b.normals_revision &&
         a.skin_revision == b.skin_revision && a.morph_revision == b.morph_revision;
}

bool Gpu(const Toon::FrameSnapshot& frame, const Toon::FrameSnapshot& expected,
    const std::string& shaders, int tolerance = 0) {
  for (const unsigned samples : {1U, 4U}) {
    Toon::FrameStatus status;
    std::string error;
    auto renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, error, {samples});
    auto oracle = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, error, {samples});
    if (!Check(renderer && oracle, error.c_str())) return false;
    Toon::ColorProduct color, reference_color;
    Toon::DepthProduct depth, reference_depth;
    auto draws = Toon::ExtractDrawList(frame);
    auto reference = Toon::ExtractDrawList(expected);
    draws.lighting.key_direction = reference.lighting.key_direction = {0.5F, 0.2F, 1.0F};
    if (!Check(renderer->Render(draws, 128, 128, color, depth, error) &&
                   oracle->Render(reference, 128, 128, reference_color, reference_depth, error), error.c_str()) ||
        !Check(color.payload.size() == reference_color.payload.size() &&
                   std::equal(color.payload.begin(), color.payload.end(), reference_color.payload.begin(),
                       [tolerance](auto a, auto b) { return std::abs(int(a) - int(b)) <= tolerance; }) &&
                   depth.payload == reference_depth.payload,
            "split geometry differs from the independent explicit-vertex GPU oracle")) return false;
    const auto before = renderer->statistics();
    if (!Check(renderer->Render(draws, 128, 128, color, depth, error), error.c_str())) return false;
    const auto after = renderer->statistics();
    if (!Check(after.point_uploads == before.point_uploads &&
                   after.topology_uploads == before.topology_uploads &&
                   after.skin_uploads == before.skin_uploads &&
                   after.morph_uploads == before.morph_uploads &&
                   after.pipelines_created == before.pipelines_created &&
                   after.validation_message_count == 0,
            "a steady split mesh must reuse geometry and pipelines without validation messages")) return false;
  }
  return true;
}

bool PreviewNormals(const std::string& shaders) {
  if (shaders.empty()) return true;
  const auto stage = UsdStage::CreateInMemory();
  if (!stage->GetRootLayer()->ImportFromString(R"USD(#usda 1.0
def Material "Normal" {
 token outputs:surface.connect = </Normal/Pbr.outputs:surface>
 def Shader "Pbr" {
  uniform token info:id = "UsdPreviewSurface"
  color3f inputs:diffuseColor = (0.7,0.4,0.2)
  normal3f inputs:normal.connect = </Normal/Map.outputs:rgb>
  token outputs:surface
 }
 def Shader "Map" {
  uniform token info:id = "UsdUVTexture"
  token inputs:sourceColorSpace = "raw"
  float4 inputs:fallback = (0.65,0.8,0.9,1)
  float4 inputs:scale.timeSamples = { 0: (2,2,2,1), 1: (1,1.5,2,1) }
  float4 inputs:bias = (-1,-1,-1,0)
  float2 inputs:st.connect = </Normal/Reader.outputs:result>
  float3 outputs:rgb
 }
 def Shader "Reader" {
  uniform token info:id = "UsdPrimvarReader_float2"
  string inputs:varname = "st"
  float2 outputs:result
 }
}
def Mesh "Body" (prepend apiSchemas = ["MaterialBindingAPI"]) {
 uniform token subdivisionScheme = "none"
 rel material:binding = </Normal>
 point3f[] points = [(-0.6,-0.6,0), (0.6,-0.6,0), (0.6,0.6,0), (-0.6,0.6,0)]
 int[] faceVertexCounts = [3,3]
 int[] faceVertexIndices = [0,1,2,0,2,3]
 texCoord2f[] primvars:st = [(0,0), (1,0), (1,1), (0,1)] (interpolation = "vertex")
 float4[] primvars:tangents = [(0,1,0,1), (1,0,0,-1)] (interpolation = "faceVarying")
 int[] primvars:tangents:indices = [0,0,0,1,1,1]
}
)USD")) return false;
  Host host(stage);
  Toon::RenderWorld reference;
  const auto mesh = reference.CreateMesh();
  reference.SetMeshPoints(mesh, {{-0.6F,-0.6F,0},{0.6F,-0.6F,0},{0.6F,0.6F,0},
      {-0.6F,-0.6F,0},{0.6F,0.6F,0},{-0.6F,0.6F,0}});
  reference.SetMeshTopology(mesh, {0,1,2,3,4,5});
  Toon::ToonMaterial material;
  material.preview.enabled = true;
  material.base_color = {0.7F,0.4F,0.2F};
  const auto mat = reference.CreateMaterial();
  reference.SetMaterial(mat, material);
  reference.SetMeshMaterial(mesh, mat);
  for (int time = 0; time < 2; ++time) {
    const auto actual = host.Sync(time);
    const float x = time == 0 ? 0.3F : -0.35F;
    const float y = time == 0 ? 0.6F : 0.2F;
    const float length = std::sqrt(x*x + y*y + 0.8F*0.8F);
    const Toon::Float3 a{-y/length, x/length, 0.8F/length};
    const Toon::Float3 b{x/length, -y/length, 0.8F/length};
    reference.SetMeshNormals(mesh, {a,a,a,b,b,b});
    if (!Gpu(actual, reference.Commit(), shaders, 2)) return false;
  }
  std::cout << "USD PreviewSurface normal graph, signed scale/bias and tangent seams passed\n";
  return true;
}

bool Tangents(const std::string& shaders) {
  const auto stage = UsdStage::CreateInMemory();
  if (!stage->GetRootLayer()->ImportFromString(R"USD(#usda 1.0
def Mesh "Body" {
 uniform token subdivisionScheme = "none"
 point3f[] points = [(-0.6,-0.6,0), (0.6,-0.6,0), (0.6,0.6,0), (-0.6,0.6,0)]
 int[] faceVertexCounts = [3,3]
 int[] faceVertexIndices = [0,1,2,0,2,3]
 texCoord2f[] primvars:st = [(0,0), (1,0), (1,1), (0,1)] (interpolation = "vertex")
 float4[] primvars:tangents = [(0,1,0,1), (1,0,0,-1)] (interpolation = "faceVarying")
 int[] primvars:tangents:indices = [0,0,0,1,1,1]
}
)USD")) return false;
  Host host(stage);
  const auto first = host.Sync();
  const auto mesh = first.meshes[0];
  const std::vector<Toon::Float4> tangents{{0,1,0,1},{0,1,0,1},{0,1,0,1},
      {1,0,0,-1},{1,0,0,-1},{1,0,0,-1}};
  if (!PrimaryUvMapping(host, {0,1,2,0,2,3})) return false;
  if (!Check(mesh.points->size() == 6 && *mesh.tangents == tangents &&
                 *mesh.uvs == std::vector<Toon::Float2>{{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}},
          "indexed tangent seams must split points and UVs with their signs")) return false;
  if (!shaders.empty()) {
    Toon::RenderWorld reference;
    reference.SetMeshPoints(reference.CreateMesh(),
        {{-0.6F,-0.6F,0},{0.6F,-0.6F,0},{0.6F,0.6F,0},
         {-0.6F,-0.6F,0},{0.6F,0.6F,0},{-0.6F,0.6F,0}});
    reference.SetMeshTopology(1, {0,1,2,3,4,5});
    reference.SetMeshUVs(1, *mesh.uvs);
    reference.SetMeshTangents(1, tangents);
    Toon::ToonTexture texture;
    texture.width = texture.height = 1;
    texture.encoding = Toon::ToonTextureEncoding::Linear;
    texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{191,230,204,255});
    const auto tex = reference.CreateTexture(); reference.SetTexture(tex, texture);
    Toon::ToonMaterial material;
    material.model = Toon::ToonShadingModel::MToon;
    material.normal_texture.texture = tex;
    material.mtoon.shading_toony = 0;
    material.mtoon.shade_color = {0.1F,0.2F,0.3F};
    const auto mat = reference.CreateMaterial();
    reference.SetMaterial(mat, material); reference.SetMeshMaterial(1, mat);
    const auto expected = reference.Commit();
    auto actual = first;
    actual.materials = expected.materials; actual.textures = expected.textures;
    actual.meshes[0].material = mat; actual.meshes[0].parts = expected.meshes[0].parts;
    if (!Gpu(actual, expected, shaders)) return false;
  }
  auto prim = stage->GetPrimAtPath(SdfPath("/Body"));
  auto pv = UsdGeomPrimvarsAPI(prim).GetPrimvar(TfToken("tangents"));
  pv.SetIndices(VtIntArray{1,1,1,0,0,0});
  const auto edit = host.Sync();
  if (!Check(SameGeometry(mesh, edit.meshes[0]) && mesh.uvs == edit.meshes[0].uvs &&
                 mesh.tangents_revision != edit.meshes[0].tangents_revision &&
                 (*edit.meshes[0].tangents)[0].w == -1,
          "tangent index edits must update only the tangent stream")) return false;
  prim.RemoveProperty(TfToken("primvars:tangents:indices"));
  pv.SetInterpolation(UsdGeomTokens->uniform);
  pv.Set(VtVec4fArray{GfVec4f(1,0,0,1),GfVec4f(0,1,0,-1)});
  const auto uniform = host.Sync();
  if (!Check(SameGeometry(edit.meshes[0], uniform.meshes[0]) &&
                 *uniform.meshes[0].tangents == std::vector<Toon::Float4>{
                     {1,0,0,1},{1,0,0,1},{1,0,0,1},{0,1,0,-1},{0,1,0,-1},{0,1,0,-1}},
          "uniform tangents must retain the existing corner layout")) return false;
  // Constant tangents no longer require a split and expand on point edits.
  pv.SetInterpolation(UsdGeomTokens->constant);
  pv.Set(VtVec4fArray{GfVec4f(1,0,0,-1)});
  const auto constant = host.Sync();
  if (!Check(constant.meshes[0].points->size() == 4 &&
                 *constant.meshes[0].tangents == std::vector<Toon::Float4>(4, {1,0,0,-1}),
          "constant tangents must expand after removing a seam")) return false;
  auto points = *constant.meshes[0].points;
  VtVec3fArray expanded;
  for (auto point : points) expanded.push_back(GfVec3f(point.x,point.y,point.z));
  expanded.push_back(GfVec3f(0,0,0));
  UsdGeomMesh(prim).GetPointsAttr().Set(expanded);
  const auto grown = host.Sync();
  if (!PrimaryUvMapping(host, {})) return false;
  if (!Check(grown.meshes[0].tangents->size() == 5,
          "constant tangent expansion did not follow point count")) return false;
  for (const auto interpolation : {UsdGeomTokens->vertex, UsdGeomTokens->varying}) {
    pv.SetInterpolation(interpolation);
    pv.Set(VtVec4fArray(5, GfVec4f(0,1,0,1)));
    const auto vertex = host.Sync();
    if (!Check(vertex.meshes[0].topology_revision == grown.meshes[0].topology_revision &&
                   *vertex.meshes[0].tangents == std::vector<Toon::Float4>(5,{0,1,0,1}),
            "vertex/varying tangent values must preserve topology")) return false;
  }
  pv.Set(VtVec4fArray(5, GfVec4f(1,0,0,0)));
  if (!Check(host.Sync().meshes[0].tangents->empty(), "malformed signs retained stale tangents")) return false;
  prim.RemoveProperty(TfToken("primvars:tangents"));
  prim.RemoveProperty(TfToken("primvars:tangents:indices"));
  if (!Check(host.Sync().meshes[0].tangents->empty(), "short UVs must not generate frames")) return false;
  expanded.resize(4);
  UsdGeomMesh(prim).GetPointsAttr().Set(expanded);
  if (!Check(*host.Sync().meshes[0].tangents == std::vector<Toon::Float4>(4, {1,0,0,1}),
          "removing authored tangents must restore generation when UVs cover the points")) return false;
  return true;
}

bool Generated(const std::string& shaders) {
  const std::vector<Toon::Float3> points{{-0.6F,-0.6F,0},{0.6F,-0.6F,0},
      {0.6F,0.6F,0},{-0.6F,0.6F,0}};
  const std::vector<Toon::Float3> normals(4, {0,0,1});
  const std::vector<Toon::Float2> uvs{{0,0},{1,0},{0,1},{1,1}};
  const std::vector<std::uint32_t> triangles{0,1,2,0,2,3};
  const std::vector<std::uint32_t> vertices{0,1,2,3,0,2};
  const std::vector<std::uint32_t> split_indices{0,1,2,4,5,3};
  const std::vector<Toon::Float4> frames{{1,0,0,1},{1,0,0,1},{1,0,0,1},
      {-1,0,0,-1},{-1,0,0,-1},{-1,0,0,-1}};
  const auto generated = Toon::detail::GenerateTangents(points, normals, uvs, triangles);
  if (!Check(generated.vertices == vertices && generated.indices == split_indices &&
                 generated.tangents == frames,
          "generated mirrored frames must split only the two conflicting shared vertices")) return false;
  const auto reordered = Toon::detail::GenerateTangents(points, normals, uvs, {2,3,0,1,2,0});
  // Compare per-corner results: numbering may differ with visitation order.
  const std::vector<unsigned> order{4,5,3,1,2,0};
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (!Check(reordered.tangents[reordered.indices[i]] == frames[split_indices[order[i]]],
            "face order and cyclic corner order changed a generated frame")) return false;
  }
  auto invalid_normals = normals;
  invalid_normals[0].x = std::numeric_limits<float>::quiet_NaN();
  if (!Check(Toon::detail::GenerateTangents(points, normals, {}, triangles).tangents.empty() &&
                 Toon::detail::GenerateTangents(points, invalid_normals, uvs, triangles).tangents.empty() &&
                 Toon::detail::GenerateTangents(points, normals, uvs, {0,1,99}).tangents.empty() &&
                 Toon::detail::GenerateTangents(points, normals, std::vector<Toon::Float2>(4), triangles).tangents.empty(),
          "incomplete, non-finite or collapsed data must keep the fallback frame")) return false;

  const auto stage = UsdStage::CreateInMemory();
  if (!stage->GetRootLayer()->ImportFromString(R"USD(#usda 1.0
def Mesh "Body" {
 uniform token subdivisionScheme = "none"
 point3f[] points = [(-0.6,-0.6,0), (0.6,-0.6,0), (0.6,0.6,0), (-0.6,0.6,0)]
 int[] faceVertexCounts = [3,3]
 int[] faceVertexIndices = [0,1,2,0,2,3]
 texCoord2f[] primvars:st = [(0,0), (1,0), (0,1), (1,1)] (interpolation = "vertex")
 float[] primvars:mmd:edgeScale = [1,2,3,4] (interpolation = "vertex")
 float4[] primvars:mmd:uv1 = [(0,0,0,0), (1,1,0,0), (2,2,0,0), (3,3,0,0)] (interpolation = "vertex")
}
)USD")) return false;
  Host host(stage);
  const auto first = host.Sync();
  const auto mesh = first.meshes[0];
  if (!PrimaryUvMapping(host, vertices)) return false;
  if (!Check(*mesh.tangents == frames && *mesh.indices == split_indices &&
                 *mesh.edge_scales == std::vector<float>{1,2,3,4,1,3} &&
                 *mesh.sub_texture_uvs == std::vector<Toon::Float2>{{0,0},{1,1},{2,2},{3,3},{0,0},{2,2}},
          "generated seam did not propagate its map to all auxiliary streams")) return false;
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    if (!Check((*mesh.points)[i] == points[vertices[i]] && (*mesh.normals)[i] == normals[vertices[i]] &&
                   (*mesh.uvs)[i] == uvs[vertices[i]], "generated seam corrupted a vertex stream")) return false;
  }
  if (!shaders.empty()) {
    // Independent analytic normals for each UV orientation. The oracle
    // has no UVs, normal map or tangent stream.
    for (const bool preview : {false, true}) {
      Toon::RenderWorld reference;
      reference.SetMeshPoints(reference.CreateMesh(),
          {points[0],points[1],points[2],points[0],points[2],points[3]});
      reference.SetMeshTopology(1, {0,1,2,3,4,5});
      const float x = 191 / 255.0F * 2 - 1, y = 230 / 255.0F * 2 - 1, z = 204 / 255.0F * 2 - 1;
      const float length = std::sqrt(x*x + y*y + z*z);
      const Toon::Float3 a{x/length, (preview ? y : -y)/length, z/length};
      const Toon::Float3 b{-a.x, a.y, a.z};
      reference.SetMeshNormals(1, {a,a,a,b,b,b});
      Toon::ToonMaterial material;
      material.model = preview ? Toon::ToonShadingModel::PreviewSurface : Toon::ToonShadingModel::MToon;
      material.preview.enabled = preview;
      material.mtoon.shading_toony = 0;
      const auto mat = reference.CreateMaterial();
      reference.SetMaterial(mat, material); reference.SetMeshMaterial(1, mat);
      const auto expected = reference.Commit();
      Toon::RenderWorld resources;
      Toon::ToonTexture texture;
      texture.width = texture.height = 1;
      texture.encoding = Toon::ToonTextureEncoding::Linear;
      texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
          std::vector<std::uint8_t>{191,230,204,255});
      const auto tex = resources.CreateTexture(); resources.SetTexture(tex, texture);
      material.normal_texture.texture = tex;
      material.preview.inputs[5].connected = preview;
      material.preview.inputs[5].scale = {2,2,2,1};
      material.preview.inputs[5].bias = {-1,-1,-1,0};
      resources.SetMaterial(resources.CreateMaterial(), material);
      auto actual = first;
      actual.materials = resources.Commit().materials; actual.textures = resources.Commit().textures;
      actual.meshes[0].material = mat; actual.meshes[0].parts = expected.meshes[0].parts;
      if (!Gpu(actual, expected, shaders, 2)) return false;
    }
  }
  auto prim = stage->GetPrimAtPath(SdfPath("/Body"));
  auto st = UsdGeomPrimvarsAPI(prim).GetPrimvar(TfToken("st"));
  // Scale preserves both the seam layout and frame; no geometry upload.
  st.Set(VtVec2fArray{GfVec2f(0,0),GfVec2f(2,0),GfVec2f(0,2),GfVec2f(2,2)});
  const auto scaled = host.Sync();
  if (!PrimaryUvMapping(host, vertices)) return false;
  if (!Check(SameGeometry(mesh, scaled.meshes[0]) && mesh.tangents == scaled.meshes[0].tangents,
          "UV scaling rebuilt an unchanged generated frame or seam map")) return false;
  // A UV rotation changes frames but retains the mirrored split.
  st.Set(VtVec2fArray{GfVec2f(0,0),GfVec2f(0,1),GfVec2f(-1,0),GfVec2f(-1,1)});
  const auto rotated = host.Sync();
  if (!Check(SameGeometry(mesh, rotated.meshes[0]) && mesh.tangents_revision != rotated.meshes[0].tangents_revision,
          "UV rotation must update generated frames without resending geometry")) return false;
  // Restore ordinary UVs, eliminating the tangent discontinuity.
  st.Set(VtVec2fArray{GfVec2f(0,0),GfVec2f(1,0),GfVec2f(1,1),GfVec2f(0,1)});
  const auto ordinary = host.Sync();
  if (!PrimaryUvMapping(host, {0,1,2,3})) return false;
  if (!Check(ordinary.meshes[0].points->size() == 4 && *ordinary.meshes[0].indices == triangles &&
                 *ordinary.meshes[0].tangents == std::vector<Toon::Float4>(4,{1,0,0,1}),
          "removing a generated seam must restore the original indexed layout")) return false;
  UsdGeomMesh usd_mesh(prim);
  VtVec3fArray moved_points;
  for (const auto& p : points) moved_points.push_back(GfVec3f(-p.y,p.x,p.z));
  usd_mesh.GetPointsAttr().Set(moved_points);
  const auto moved = host.Sync();
  if (!Check(moved.meshes[0].topology_revision == ordinary.meshes[0].topology_revision &&
                 *moved.meshes[0].tangents == std::vector<Toon::Float4>(4,{0,1,0,1}),
          "rest-point edits must regenerate tangent directions without retriangulation")) return false;
  moved_points.clear();
  for (const auto& p : points) moved_points.push_back(GfVec3f(p.x,p.y,p.z));
  usd_mesh.GetPointsAttr().Set(moved_points);
  const auto restored = host.Sync();
  usd_mesh.SetNormalsInterpolation(UsdGeomTokens->constant);
  usd_mesh.CreateNormalsAttr().Set(VtVec3fArray{GfVec3f(0.6F,0,0.8F)});
  const auto tilted = host.Sync();
  const auto& tilted_tangent = (*tilted.meshes[0].tangents)[0];
  if (!Check(tilted.meshes[0].points_revision == restored.meshes[0].points_revision &&
                 tilted.meshes[0].topology_revision == restored.meshes[0].topology_revision &&
                 std::abs(tilted_tangent.x - 0.8F) < 1e-6F && std::abs(tilted_tangent.z + 0.6F) < 1e-6F,
          "rest-normal edits must reproject tangents without rebuilding geometry")) return false;
  const auto tangent = UsdGeomPrimvarsAPI(prim).CreatePrimvar(TfToken("tangents"),
      SdfValueTypeNames->Float4Array, UsdGeomTokens->constant);
  tangent.Set(VtVec4fArray{GfVec4f(0,1,0,-1)});
  if (!Check(*host.Sync().meshes[0].tangents == std::vector<Toon::Float4>(4,{0,1,0,-1}),
          "authored tangents must take precedence over generation")) return false;
  prim.RemoveProperty(TfToken("primvars:tangents"));
  st.Set(VtVec2fArray(4, GfVec2f(0,0)));
  if (!Check(host.Sync().meshes[0].tangents->empty(), "collapsed UVs retained stale frames")) return false;
  if (!PrimaryUvMapping(host, {0,1,2,3})) return false;
  st.SetInterpolation(UsdGeomTokens->faceVarying);
  st.Set(VtVec2fArray(6, GfVec2f(0,0)));
  host.Sync();
  if (!PrimaryUvMapping(host, {})) return false;
  st.SetInterpolation(UsdGeomTokens->varying);
  st.Set(VtVec2fArray(4, GfVec2f(0,0)));
  host.Sync();
  if (!PrimaryUvMapping(host, {0,1,2,3})) return false;
  std::cout << "MikkTSpace mirrored seams, analytic normal mapping and edit routing passed\n";
  return true;
}

bool GeneratedSkin(const char* filename, const std::string& shaders) {
  const auto stage = UsdStage::Open(filename);
  if (!stage) return false;
  UsdGeomMesh mesh(stage->GetPrimAtPath(SdfPath("/Root/Linear")));
  mesh.GetFaceVertexCountsAttr().Set(VtIntArray{3,3});
  mesh.GetFaceVertexIndicesAttr().Set(VtIntArray{0,1,2,0,2,3});
  for (const char* path : {"/Root/Linear", "/Root/DualQuaternion"}) {
    const auto st = UsdGeomPrimvarsAPI(stage->GetPrimAtPath(SdfPath(path)))
        .CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray, UsdGeomTokens->vertex);
    st.Set(VtVec2fArray{GfVec2f(0,0),GfVec2f(1,0),GfVec2f(0,1),GfVec2f(1,1)});
  }
  // A uniform normal-only morph makes the analytic mapped normal constant
  // on each triangle, so the CPU oracle does not share fragment math.
  stage->GetPrimAtPath(SdfPath("/Root/Skel/Anim"))
      .GetAttribute(TfToken("blendShapeWeights")).Set(VtFloatArray{0,1,0}, 3);
  Host host(stage);
  Toon::MeshSnapshot initial;
  const std::vector<unsigned> source{0,1,2,3,0,2};
  for (const int time : {1,2,3}) {
    auto actual = host.Sync(time);
    const Toon::MeshSnapshot* linear = nullptr;
    for (const auto& entry : actual.meshes) {
      if (Toon::IsSkinned(entry)) linear = &entry;
      else if (!Check(entry.tangents->empty(), "CPU deformation must keep derivative frames even with usable UVs")) return false;
    }
    if (!Check(linear && linear->points->size() == 6 && linear->tangents->size() == 6 &&
                   *linear->indices == std::vector<std::uint32_t>{0,1,2,4,5,3},
            "generated skinned mirror seam is missing")) return false;
    for (std::size_t i = 0; i < source.size(); ++i) {
      if (!Check((*linear->influences)[i].joint == (source[i] < 2 ? 1U : 0U) &&
                     (*linear->morph_ranges)[i] == (*linear->morph_ranges)[source[i]],
              "generated seam lost skin influences or sparse morph ranges")) return false;
      const auto& t = (*linear->tangents)[i];
      const float sign = i < 3 ? 1.0F : -1.0F;
      if (!Check(std::abs(t.x - 0.8F * sign) < 1e-6F && std::abs(t.y) < 1e-6F &&
                     std::abs(t.z + 0.6F * sign) < 1e-6F && t.w == sign,
              "generated rest frame does not match the analytic slanted-normal frame")) return false;
    }
    if (time == 1) initial = *linear;
    else if (!Check(SameGeometry(initial, *linear) && initial.tangents == linear->tangents &&
                        initial.tangents_revision == linear->tangents_revision,
                 "pose and normal-morph weights regenerated rest frames or geometry")) return false;
    if (shaders.empty()) continue;
    actual.meshes = {*linear};
    for (const bool preview : {false, true}) {
      Toon::RenderWorld reference, resources;
      const float shift = time == 1 ? 0.0F : 1.0F;
      reference.SetMeshPoints(reference.CreateMesh(),
          {{0,0,0},{1,0,0},{1+shift,1,0},{0,0,0},{1+shift,1,0},{shift,1,0}});
      reference.SetMeshTopology(1, {0,1,2,3,4,5});
      const float ny = time == 3 ? 0.25F : 0.0F;
      const float nl = std::sqrt(1 + ny*ny);
      const Toon::Float3 n{0.6F/nl, ny/nl, 0.8F/nl};
      const Toon::Float3 t{0.8F,0,-0.6F};
      const Toon::Float3 b{-0.6F*n.y, n.z*0.8F+n.x*0.6F, -0.8F*n.y};
      const auto mapped = [&](float sign) {
        const float x = preview ? 0.3F : 191 / 255.0F * 2 - 1;
        const float y = preview ? 0.5F : -(230 / 255.0F * 2 - 1);
        const float z = preview ? 0.8F : 204 / 255.0F * 2 - 1;
        Toon::Float3 v{sign*t.x*x+b.x*y+n.x*z, b.y*y+n.y*z, sign*t.z*x+b.z*y+n.z*z};
        const float length = std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
        return Toon::Float3{v.x/length,v.y/length,v.z/length};
      };
      const auto a = mapped(1), bnormal = mapped(-1);
      reference.SetMeshNormals(1, {a,a,a,bnormal,bnormal,bnormal});
      Toon::ToonMaterial material;
      material.model = preview ? Toon::ToonShadingModel::PreviewSurface : Toon::ToonShadingModel::MToon;
      material.preview.enabled = preview;
      material.mtoon.shading_toony = 0;
      const auto mat = reference.CreateMaterial();
      reference.SetMaterial(mat, material); reference.SetMeshMaterial(1, mat);
      Toon::ToonView view;
      view.projection.m[0] = view.projection.m[5] = 0.6F;
      view.projection.m[10] = 0.3F; view.projection.m[12] = -0.6F; view.projection.m[13] = -0.3F;
      reference.SetView(view);
      const auto expected = reference.Commit();
      Toon::ToonTexture texture;
      texture.width = texture.height = 1; texture.encoding = Toon::ToonTextureEncoding::Linear;
      texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
          std::vector<std::uint8_t>{191,230,204,255});
      const auto tex = resources.CreateTexture(); resources.SetTexture(tex, texture);
      material.normal_texture.texture = preview ? 0 : tex;
      material.preview.normal = {0.3F,0.5F,0.8F};
      resources.SetMaterial(resources.CreateMaterial(), material);
      actual.materials = resources.Commit().materials; actual.textures = resources.Commit().textures;
      actual.view = view; actual.meshes[0].material = mat; actual.meshes[0].parts = expected.meshes[0].parts;
      if (!Gpu(actual, expected, shaders, 2)) return false;
    }
  }
  std::cout << "Generated skin/morph frames, sparse seam data and analytic shading passed\n";
  return true;
}

bool Ordinary(const std::string& shaders) {
  const auto stage = UsdStage::CreateInMemory();
  if (!stage->GetRootLayer()->ImportFromString(R"USD(#usda 1.0
 def Xform "Root" {
  def Material "Base" {}
  def Material "Other" {}
  def Mesh "Body" (prepend apiSchemas = ["MaterialBindingAPI"]) {
    uniform token subdivisionScheme = "none"
    point3f[] points = [(-0.8,-0.7,0), (0,-0.7,0), (0,0.7,0), (-0.8,0.7,0), (0.8,-0.7,0.4), (0.8,0.7,0.4)]
    int[] faceVertexCounts = [4,4]
    int[] faceVertexIndices = [0,1,2,3,1,4,5,2]
    texCoord2f[] primvars:st = [(0,0), (1,0), (1,1), (0,1)] (interpolation = "faceVarying")
    int[] primvars:st:indices = [0,1,2,3,3,2,1,0]
    normal3f[] normals = [(0,0,1), (-0.6,0,0.8)] (interpolation = "uniform")
    float[] primvars:mmd:edgeScale = [1,2,3,4,5,6] (interpolation = "vertex")
    float4[] primvars:mmd:uv1 = [(0,0,0,0), (1,1,0,0), (2,2,0,0), (3,3,0,0), (4,4,0,0), (5,5,0,0)] (interpolation = "vertex")
    rel material:binding = </Root/Base>
    def GeomSubset "Second" (prepend apiSchemas = ["MaterialBindingAPI"]) {
      uniform token elementType = "face"
      uniform token familyName = "materialBind"
      int[] indices = [1]
      rel material:binding = </Root/Other>
    }
  }
 }
)USD")) return false;
  Host host(stage);
  const auto first = host.Sync();
  if (!Check(first.meshes.size() == 1, "expected one ordinary mesh")) return false;
  const auto mesh = first.meshes[0];
  const std::vector<Toon::Float3> points{{-0.8F,-0.7F,0}, {0,-0.7F,0}, {0,0.7F,0}, {-0.8F,0.7F,0},
      {0,-0.7F,0}, {0.8F,-0.7F,0.4F}, {0.8F,0.7F,0.4F}, {0,0.7F,0}};
  const std::vector<Toon::Float2> uvs{{0,0},{1,0},{1,1},{0,1},{0,1},{1,1},{1,0},{0,0}};
  const std::vector<Toon::Float3> normals{{0,0,1},{0,0,1},{0,0,1},{0,0,1},
      {-0.6F,0,0.8F},{-0.6F,0,0.8F},{-0.6F,0,0.8F},{-0.6F,0,0.8F}};
  const std::vector<std::uint32_t> triangles{4,5,6,4,6,7,0,1,2,0,2,3};
  if (!Check(*mesh.points == points && *mesh.uvs == uvs && *mesh.normals == normals &&
                 *mesh.indices == triangles && mesh.authored_normals,
          "indexed face-varying UVs, uniform normals or subset triangulation were lost") ||
      !Check(*mesh.edge_scales == std::vector<float>{1,2,3,4,2,5,6,3} &&
                 *mesh.sub_texture_uvs == std::vector<Toon::Float2>{{0,0},{1,1},{2,2},{3,3},{1,1},{4,4},{5,5},{2,2}},
          "splitting must duplicate additional UVs and edge scales") ||
      !Check(mesh.parts->size() == 2 && (*mesh.parts)[0].index_count == 6 &&
                 (*mesh.parts)[1].first_index == 6,
          "subset parts must retain their face ranges")) return false;

  if (!shaders.empty()) {
    Toon::RenderWorld reference;
    reference.SetMeshPoints(reference.CreateMesh(), points);
    reference.SetMeshTopology(1, triangles);
    reference.SetMeshNormals(1, normals);
    reference.SetMeshUVs(1, uvs);
    Toon::ToonTexture texture;
    texture.width = texture.height = 2;
    texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255});
    const auto tex = reference.CreateTexture();
    reference.SetTexture(tex, texture);
    Toon::ToonMaterial material;
    material.model = Toon::ToonShadingModel::PreviewSurface;
    material.base_texture.texture = tex;
    material.preview.roughness = 0.7F;
    const auto mat = reference.CreateMaterial();
    reference.SetMaterial(mat, material);
    reference.SetMeshMaterial(1, mat);
    const auto expected = reference.Commit();
    auto actual = first;
    actual.materials = expected.materials;
    actual.textures = expected.textures;
    actual.meshes[0].material = mat;
    auto parts = *mesh.parts;
    for (auto& part : parts) part.material = mat;
    actual.meshes[0].parts = std::make_shared<const std::vector<Toon::MeshPart>>(parts);
    if (!Gpu(actual, expected, shaders)) return false;
  }

  auto prim = stage->GetPrimAtPath(SdfPath("/Root/Body"));
  auto st = UsdGeomPrimvarsAPI(prim).GetPrimvar(TfToken("st"));
  st.SetIndices(VtIntArray{3,2,1,0,0,1,2,3});
  const auto uv_edit = host.Sync();
  if (!Check(SameGeometry(mesh, uv_edit.meshes[0]) &&
                 mesh.uvs_revision != uv_edit.meshes[0].uvs_revision,
          "UV index edits must update UVs alone")) return false;
  // A uniform -> face-varying normal edit keeps the same corner layout.
  UsdGeomMesh usd_mesh(prim);
  usd_mesh.SetNormalsInterpolation(UsdGeomTokens->faceVarying);
  usd_mesh.GetNormalsAttr().Set(VtVec3fArray(8, GfVec3f(0,1,0)));
  const auto normal_edit = host.Sync();
  if (!Check(normal_edit.meshes[0].points_revision == mesh.points_revision &&
                 normal_edit.meshes[0].topology_revision == mesh.topology_revision &&
                 *normal_edit.meshes[0].normals == std::vector<Toon::Float3>(8, {0,1,0}),
          "face-varying normals must preserve the existing corner layout")) return false;
  // Smooth normals must be computed on shared source points, before UV seams split them.
  usd_mesh.GetNormalsAttr().Clear();
  const auto smooth = host.Sync();
  if (!Check(!smooth.meshes[0].authored_normals &&
                 (*smooth.meshes[0].normals)[1] == (*smooth.meshes[0].normals)[4] &&
                 (*smooth.meshes[0].normals)[2] == (*smooth.meshes[0].normals)[7] &&
                 (*smooth.meshes[0].normals)[1].x < 0,
          "UV seams must not introduce creases in generated smooth normals")) return false;
  // Removing the seam returns to source-point numbering and preserves no stale UVs.
  prim.RemoveProperty(TfToken("primvars:st"));
  prim.RemoveProperty(TfToken("primvars:st:indices"));
  const auto unsplit = host.Sync();
  if (!Check(unsplit.meshes[0].points->size() == 6 && unsplit.meshes[0].uvs->empty(),
          "removing corner primvars must restore source geometry")) return false;
  st = UsdGeomPrimvarsAPI(prim).CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray,
      UsdGeomTokens->constant);
  st.Set(VtVec2fArray{GfVec2f(0.25F,0.75F)});
  const auto constant = host.Sync();
  if (!Check(*constant.meshes[0].uvs == std::vector<Toon::Float2>(6, {0.25F,0.75F}) &&
                 SameGeometry(unsplit.meshes[0], constant.meshes[0]),
          "constant UVs must expand without changing geometry")) return false;
  VtVec3fArray original_points;
  usd_mesh.GetPointsAttr().Get(&original_points);
  auto longer_points = original_points;
  longer_points.push_back(GfVec3f(0,0,0));
  usd_mesh.GetPointsAttr().Set(longer_points);
  const auto longer = host.Sync();
  if (!Check(*longer.meshes[0].uvs == std::vector<Toon::Float2>(7, {0.25F,0.75F}) &&
                 longer.meshes[0].topology_revision == constant.meshes[0].topology_revision,
          "constant UVs must follow point-count edits without rebuilding topology")) return false;
  usd_mesh.GetPointsAttr().Set(original_points);
  // Malformed corner data must fall back without indexing out of bounds.
  st.SetInterpolation(UsdGeomTokens->faceVarying);
  st.Set(VtVec2fArray{GfVec2f(0,0)});
  const auto malformed = host.Sync();
  if (!Check(malformed.meshes[0].uvs->empty(), "short face-varying UVs must be ignored")) return false;
  // Left-handed winding and holes use the same corner map as all other data.
  usd_mesh.GetOrientationAttr().Set(UsdGeomTokens->leftHanded);
  usd_mesh.GetHoleIndicesAttr().Set(VtIntArray{0});
  st.Set(VtVec2fArray(8, GfVec2f(0.5F,0.5F)));
  const auto hole = host.Sync();
  if (!Check(hole.meshes[0].points->size() == 4 &&
                 *hole.meshes[0].indices == std::vector<std::uint32_t>{2,1,0,2,0,3} &&
                 (*hole.meshes[0].points)[0] == points[4],
          "holes, reversed winding and subset order must share one corner map")) return false;
  return true;
}

bool Skin(const char* path, const std::string& shaders) {
  const auto stage = UsdStage::Open(path);
  if (!stage) return false;
  const auto prim = stage->GetPrimAtPath(SdfPath("/Root/Linear"));
  UsdGeomMesh mesh(prim);
  mesh.GetFaceVertexCountsAttr().Set(VtIntArray{3,3});
  mesh.GetFaceVertexIndicesAttr().Set(VtIntArray{0,1,2,0,2,3});
  auto st = UsdGeomPrimvarsAPI(prim).CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray,
      UsdGeomTokens->faceVarying);
  st.Set(VtVec2fArray{GfVec2f(0,0),GfVec2f(1,0),GfVec2f(1,1),
      GfVec2f(1,0),GfVec2f(0,1),GfVec2f(0,0)});
  auto tangent = UsdGeomPrimvarsAPI(prim).CreatePrimvar(TfToken("tangents"),
      SdfValueTypeNames->Float4Array, UsdGeomTokens->vertex);
  tangent.Set(VtVec4fArray{GfVec4f(0,1,0,1), GfVec4f(0,1,0,-1),
      GfVec4f(0,1,0,1), GfVec4f(0,1,0,-1)});
  auto dq_tangent = UsdGeomPrimvarsAPI(stage->GetPrimAtPath(SdfPath("/Root/DualQuaternion")))
      .CreatePrimvar(TfToken("tangents"), SdfValueTypeNames->Float4Array, UsdGeomTokens->constant);
  dq_tangent.Set(VtVec4fArray{GfVec4f(0,1,0,1)});
  Host host(stage);
  const auto find = [](const Toon::FrameSnapshot& frame) -> const Toon::MeshSnapshot* {
    for (const auto& entry : frame.meshes) if (Toon::IsSkinned(entry)) return &entry;
    return nullptr;
  };
  const auto first = host.Sync(1);
  const auto* linear = find(first);
  bool cpu_fallback = false;
  for (const auto& entry : first.meshes) if (!Toon::IsSkinned(entry)) {
    cpu_fallback = true;
    if (!Check(entry.tangents->empty(),
            "CPU-deformed points must not consume undeformed rest tangents")) return false;
  }
  if (!Check(cpu_fallback, "expected a dual quaternion CPU fallback mesh")) return false;
  if (!Check(linear && linear->points->size() == 6 && Toon::IsMorphed(*linear),
          "a UV seam must retain GPU skinning and morphs")) return false;
  const std::vector<unsigned> source{0,1,2,0,2,3};
  const std::vector<Toon::Float3> rest{{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
  for (std::size_t i = 0; i < source.size(); ++i) {
    if (!Check((*linear->points)[i] == rest[source[i]] &&
                   (*linear->influences)[i].joint == (source[i] < 2 ? 1U : 0U) &&
                   (*linear->normals)[i] == Toon::Float3{0.6F,0,0.8F} &&
                   (*linear->tangents)[i] == Toon::Float4{0,1,0,source[i] % 2 ? -1.0F : 1.0F},
            "split points must retain their rest normals and source influences")) return false;
  }
  if (!Check((*linear->morph_ranges)[0] == (*linear->morph_ranges)[3] &&
                 (*linear->morph_ranges)[2] == (*linear->morph_ranges)[4],
          "split vertices must reference the same source morph offsets")) return false;
  const auto posed = host.Sync(2);
  const auto morphed = host.Sync(3);
  const auto* pose = find(posed);
  const auto* morph = find(morphed);
  if (!Check(pose && morph && SameGeometry(*linear, *pose) && SameGeometry(*pose, *morph) &&
                 linear->tangents == pose->tangents && pose->tangents == morph->tangents &&
                 linear->tangents_revision == morph->tangents_revision &&
                 linear->pose_revision != pose->pose_revision &&
                 pose->morph_weights_revision != morph->morph_weights_revision,
          "pose and morph weight edits must preserve all split geometry")) return false;
  if (!shaders.empty()) {
    Toon::RenderWorld reference;
    reference.SetMeshPoints(reference.CreateMesh(),
        {{0,0,1},{1,0,0},{2,1,0.25F},{0,0,1},{2,1,0.25F},{1,1,0}});
    reference.SetMeshTopology(1, {0,1,2,3,4,5});
    reference.SetMeshNormals(1,
        {{0,0.25F,1.4F},{0.6F,0.25F,0.8F},{0.6F,0.25F,0.8F},
         {0,0.25F,1.4F},{0.6F,0.25F,0.8F},{0.6F,0.25F,0.8F}});
    Toon::ToonView view;
    view.projection.m[0] = view.projection.m[5] = 0.6F;
    view.projection.m[10] = 0.3F;
    view.projection.m[12] = -0.6F;
    view.projection.m[13] = -0.3F;
    reference.SetView(view);
    Toon::ToonMaterial material;
    material.model = Toon::ToonShadingModel::MToon;
    material.mtoon.shading_toony = 0;
    const auto mat = reference.CreateMaterial();
    reference.SetMaterial(mat, material);
    reference.SetMeshMaterial(1, mat);
    const auto expected = reference.Commit();
    auto actual = morphed;
    actual.meshes = {*morph};
    actual.meshes[0].material = mat;
    actual.meshes[0].parts = expected.meshes[0].parts;
    actual.materials = expected.materials;
    actual.view = view;
    if (!Gpu(actual, expected, shaders)) return false;
  }
  st.Set(VtVec2fArray(6, GfVec2f(0.5F,0.5F)));
  const auto uv_edit = host.Sync(3);
  if (!Check(find(uv_edit) && SameGeometry(*morph, *find(uv_edit)) &&
                 morph->uvs_revision != find(uv_edit)->uvs_revision,
          "a skinned UV edit must not rebuild skin or targets")) return false;
  auto normal_primvar = UsdGeomPrimvarsAPI(prim).GetPrimvar(TfToken("normals"));
  normal_primvar.SetInterpolation(UsdGeomTokens->faceVarying);
  normal_primvar.Set(VtVec3fArray{GfVec3f(0,0,1),GfVec3f(0,0,1),GfVec3f(0,0,1),
      GfVec3f(0,1,0),GfVec3f(0,1,0),GfVec3f(0,1,0)});
  // Form the computation after authoring its interpolation. OpenUSD 26.08
  // caches hasFaceVaryingNormals when forming that computation.
  Host corner_host(stage);
  const auto corner_normals = corner_host.Sync(3);
  const auto* normal_mesh = find(corner_normals);
  if (!Check(normal_mesh && normal_mesh->authored_normals &&
                 *normal_mesh->normals == std::vector<Toon::Float3>{{0,0,1},{0,0,1},{0,0,1},
                     {0,1,0},{0,1,0},{0,1,0}},
          "skinned face-varying rest normals must reach the corner vertices")) return false;
  normal_primvar.Set(VtVec3fArray(6, GfVec3f(1,0,0)));
  const auto edited_normals = corner_host.Sync(3);
  const auto* edited_mesh = find(edited_normals);
  if (!Check(edited_mesh &&
                 *edited_mesh->normals == std::vector<Toon::Float3>(6, {1,0,0}) &&
                 edited_mesh->points_revision == normal_mesh->points_revision &&
                 edited_mesh->topology_revision == normal_mesh->topology_revision &&
                 edited_mesh->skin_revision == normal_mesh->skin_revision,
          "skinned corner-normal value edits must preserve points, topology and skin")) return false;
  return true;
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  TfSetenv("USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS", "1");
  const std::string shaders = argc == 3 ? argv[2] : "";
  if (!shaders.empty()) {
    // A usable loader does not imply an instance/device (e.g. hosted CI).
    Toon::FrameStatus status;
    std::string error;
    const auto probe = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, error, {1});
    if (status == Toon::FrameStatus::Skip) {
      std::cout << error << '\n';
      return 77;
    }
    if (!Check(probe != nullptr, error.c_str())) return 1;
  }
  if (!PreviewNormals(shaders) || !Tangents(shaders) || !Generated(shaders) ||
      !GeneratedSkin(argv[1], shaders) || !Ordinary(shaders) || !Skin(argv[1], shaders)) return 1;
  std::cout << "corner primvars: indexed UVs, normals, subsets, holes, winding, edits and deformation passed\n";
  return 0;
}
