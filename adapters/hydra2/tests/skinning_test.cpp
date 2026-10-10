// SPDX-License-Identifier: Apache-2.0
//
// hdToon's skinned meshes, through UsdImaging and usdSkelImaging as a host
// drives them (design policy §11). skinning.usda's linear quad becomes a
// skin and a pose; moving to a time where only the pose changes sets the
// pose alone, and a blend shape weight change sets only morph weights. The linear
// quad's authored normals arrive as its normals computation's rest normals,
// which the pose does not touch. Its dual quaternion quad runs
// usdSkelImaging's CPU kernel instead. The default check reads the scene the
// next frame would draw; an optional shader directory also runs GPU image
// comparisons against independently deformed rest geometry. The delegate
// also describes the Hydra identities behind the resident palette, subshape
// and material slots, which a host binds evaluated avatar targets with;
// built with the optional runtime consumer, bindings matched from them must
// turn runtime shape weights into usdSkelImaging's subshape weights and
// runtime joints into its palette, and unmatched identities are reported.
// With --stage, a real stage's described identities are checked instead:
// they must name every palette entry and morph weight slot, and compose the
// palette usdSkelImaging computed, with every joint's rest rotated so a
// mismatched joint cannot hide behind an identity palette. With the runtime
// consumer, a layout read from the stage's UsdSkel prims must also bind
// completely and compose the same palettes.
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/setenv.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <toon/render_world.hpp>
#include <toon/vulkan_backend.hpp>
#ifdef TOON_HAS_AVATAR_STATE
#include "avatar_binding.hpp"

#include <pxr/base/gf/transform.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

Toon::Float3 Transform(const Toon::Matrix4& matrix, const Toon::Float3& p) {
  const auto& m = matrix.m;
  return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
      m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
      m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}

// The vertex stage's linear blend skinning, on the CPU.
std::vector<Toon::Float3> Skinned(const Toon::MeshSnapshot& mesh) {
  std::vector<Toon::Float3> points;
  const std::uint32_t count = mesh.influences_per_point;
  for (std::size_t point = 0; point < mesh.points->size(); ++point) {
    Toon::Float3 rest = (*mesh.points)[point];
    if (Toon::IsMorphed(mesh)) {
      const auto range = (*mesh.morph_ranges)[point];
      for (std::uint32_t i = 0; i < range.count; ++i) {
        const auto& offset = (*mesh.morph_offsets)[range.first + i];
        if (offset.target >= mesh.morph_weights->size()) continue;
        const float weight = (*mesh.morph_weights)[offset.target];
        rest.x += offset.position.x * weight;
        rest.y += offset.position.y * weight;
        rest.z += offset.position.z * weight;
      }
    }
    const Toon::Float3 bound =
        Transform(mesh.geom_bind, rest);
    Toon::Float3 sum;
    for (std::uint32_t index = 0; index < count; ++index) {
      const Toon::ToonJointInfluence& influence =
          (*mesh.influences)[(mesh.constant_influences ? 0 : point * count) +
                             index];
      const Toon::Float3 moved =
          Transform((*mesh.joints)[influence.joint], bound);
      sum.x += moved.x * influence.weight;
      sum.y += moved.y * influence.weight;
      sum.z += moved.z * influence.weight;
    }
    points.push_back(Transform(mesh.skeleton_to_mesh, sum));
  }
  return points;
}

bool Near(const std::vector<Toon::Float3>& points,
    const std::vector<Toon::Float3>& expected) {
  if (points.size() != expected.size()) {
    return false;
  }
  for (std::size_t index = 0; index < points.size(); ++index) {
    if (std::abs(points[index].x - expected[index].x) > 1e-4F ||
        std::abs(points[index].y - expected[index].y) > 1e-4F ||
        std::abs(points[index].z - expected[index].z) > 1e-4F) {
      return false;
    }
  }
  return true;
}

std::vector<Toon::Float3> MorphedNormals(const Toon::MeshSnapshot& mesh) {
  auto normals = *mesh.normals;
  for (std::size_t point = 0; point < normals.size(); ++point) {
    const auto range = (*mesh.morph_ranges)[point];
    for (std::uint32_t i = 0; i < range.count; ++i) {
      const auto& offset = (*mesh.morph_offsets)[range.first + i];
      if (offset.target >= mesh.morph_weights->size()) continue;
      const float weight = (*mesh.morph_weights)[offset.target];
      normals[point].x += offset.normal.x * weight;
      normals[point].y += offset.normal.y * weight;
      normals[point].z += offset.normal.z * weight;
    }
  }
  return normals;
}

Toon::Matrix4 Translation(float x, float y, float z) {
  Toon::Matrix4 matrix;
  matrix.m[12] = x;
  matrix.m[13] = y;
  matrix.m[14] = z;
  return matrix;
}

// Relative to the larger magnitude, so a stage of centimetres and one of
// metres meet the same tolerance.
bool Near(const Toon::Matrix4& a, const Toon::Matrix4& b, float tolerance = 1e-5F) {
  for (std::size_t i = 0; i < a.m.size(); ++i) {
    const float scale = std::max({1.0F, std::abs(a.m[i]), std::abs(b.m[i])});
    if (!(std::abs(a.m[i] - b.m[i]) <= tolerance * scale)) return false;
  }
  return true;
}

Toon::Matrix4 ToToon(const GfMatrix4d& matrix) {
  Toon::Matrix4 result;
  for (std::size_t i = 0; i < result.m.size(); ++i) {
    result.m[i] = static_cast<float>(matrix.data()[i]);
  }
  return result;
}

const HdToonResidentMesh* Described(const HdToonResidentTargets& targets,
    const char* path) {
  for (const HdToonResidentMesh& mesh : targets.meshes) {
    if (mesh.path == SdfPath(path)) return &mesh;
  }
  return nullptr;
}

// Composes each palette entry from the described identities as the fast
// adapter composes a binding: a skeleton-order joint transform taken to
// world, back to skeleton space, after the inverse bind. It must give the
// palette usdSkelImaging computed.
bool PaletteFromIdentities(const HdToonResidentSkin& skin,
    const std::vector<Toon::Matrix4>& skeleton_space,
    const Toon::Matrix4& skeleton_to_world, const Toon::MeshSnapshot& mesh,
    float tolerance = 1e-5F) {
  if (skin.skeleton_joints.size() != mesh.joints->size() ||
      skin.inverse_bind.size() != mesh.joints->size() ||
      !Near(skin.skeleton_to_mesh, mesh.skeleton_to_mesh, tolerance)) {
    return false;
  }
  for (std::size_t i = 0; i < skin.skeleton_joints.size(); ++i) {
    const std::int32_t joint = skin.skeleton_joints[i];
    if (joint < 0 || static_cast<std::size_t>(joint) >= skeleton_space.size()) {
      return false;
    }
    const Toon::Matrix4 world = Toon::Multiply(skeleton_to_world,
        skeleton_space[static_cast<std::size_t>(joint)]);
    if (!Near(Toon::Multiply(Toon::Multiply(skin.world_to_skeleton, world),
                  skin.inverse_bind[i]),
            (*mesh.joints)[i], tolerance)) {
      return false;
    }
  }
  return true;
}

struct Meshes {
  const Toon::MeshSnapshot* linear = nullptr;
  const Toon::MeshSnapshot* dual = nullptr;
};

// The linear quad is the one the GPU skins; the dual quaternion one is the
// other with four points. The third mesh is the skeleton's guide, which
// usdSkelImaging makes a mesh of.
Meshes Find(const Toon::FrameSnapshot& snapshot) {
  Meshes meshes;
  for (const Toon::MeshSnapshot& mesh : snapshot.meshes) {
    if (Toon::IsSkinned(mesh)) {
      meshes.linear = &mesh;
    } else if (mesh.points->size() == 4U) {
      meshes.dual = &mesh;
    }
  }
  return meshes;
}

// The expected rest-space values come from the fixture, independently of
// the adapter's packed targets. Both renderers then use the same skin.
bool GpuNormals(const std::vector<Toon::FrameSnapshot>& frames,
    const std::string& shader_directory, bool offsets = true, bool authored = true) {
  for (int mode = 0; mode < 3; ++mode) {
    for (bool outlines : {false, true}) {
      Toon::FrameStatus status;
      std::string error;
      const auto shaders = Toon::SceneShadersIn(shader_directory);
      auto renderer = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
      auto oracle = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
      if (!Check(renderer && oracle, error.c_str())) return false;
      Toon::ToonMaterial material;
      material.model = Toon::ToonShadingModel::MToon;
      material.base_color = {0.8F, 0.5F, 0.3F};
      material.mtoon.shade_color = {0.05F, 0.02F, 0.01F};
      material.outline = true;
      material.outline_width = 0.08F;
      material.mtoon.shading_toony = 0.0F;
      material.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
      if (mode == 1) material.alpha_mode = Toon::ToonAlphaMode::Mask;
      if (mode == 2) {
        material.alpha_mode = Toon::ToonAlphaMode::Blend;
        material.alpha = 0.6F;
      }
      Toon::RenderWorld reference;
      const auto ref = reference.CreateMesh();
      const auto mat = reference.CreateMaterial();
      reference.SetMaterial(mat, material);
      reference.SetMeshMaterial(ref, mat);
      auto triangles = *Find(frames[0]).linear->indices;
      const auto front = triangles;
      for (std::size_t i = 0; i < front.size(); i += 3) {
        triangles.insert(triangles.end(), {front[i + 2], front[i + 1], front[i]});
      }
      const auto indices = std::make_shared<const std::vector<std::uint32_t>>(triangles);
      reference.SetMeshTopology(ref, triangles);
      Toon::ToonView view;
      view.projection.m[0] = 0.6F;
      view.projection.m[5] = 0.6F;
      view.projection.m[10] = 0.3F;
      view.projection.m[12] = -0.6F;
      view.projection.m[13] = -0.3F;
      reference.SetView(view);
      for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        auto actual = frames[frame];
        const auto mesh = *Find(actual).linear;
        actual.meshes = {mesh};
        actual.meshes[0].indices = indices;
        actual.meshes[0].material = mat;
        actual.materials = {{mat, material, 1, 1}};
        actual.view = view;
        Toon::ToonSkin skin;
        skin.influences_per_point = mesh.influences_per_point;
        skin.constant = mesh.constant_influences;
        skin.influences = *mesh.influences;
        skin.geom_bind = mesh.geom_bind;
        reference.SetMeshSkin(ref, skin);
        reference.SetMeshSkinPose(ref, {*mesh.joints, mesh.skeleton_to_mesh});
        std::vector<Toon::Float3> points{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const Toon::Float3 rest_normal = authored
            ? Toon::Float3{0.6F, 0, 0.8F} : Toon::Float3{0, 0, 1};
        std::vector<Toon::Float3> normals(4, rest_normal);
        if (frame >= 2) {
          const bool half = frame == 2;
          points[0].z = half ? 0.75F : 1.0F;
          points[2].z = half ? 0.125F : 0.25F;
          if (offsets) {
            for (auto& normal : normals) normal.y = half ? 0.125F : 0.25F;
            normals[0].x -= half ? 0.2F : 0.6F;
            normals[0].z += half ? 0.2F : 0.6F;
          }
        }
        reference.SetMeshPoints(ref, points);
        reference.SetMeshNormals(ref, normals);
        auto draws = Toon::ExtractDrawList(actual);
        auto expected = Toon::ExtractDrawList(reference.Commit());
        draws.outlines = expected.outlines = outlines;
        // A directional source across the morph's changing normal creates
        // a visible lit-surface difference, including the MToon ramp.
        draws.lighting.key_direction = expected.lighting.key_direction = {1, 0.25F, 0.2F};
        Toon::ColorProduct color, expected_color;
        Toon::DepthProduct depth, expected_depth;
        const auto before = renderer->statistics();
        if (!Check(renderer->Render(draws, 128, 128, color, depth, error) &&
                oracle->Render(expected, 128, 128, expected_color, expected_depth, error), error.c_str()) ||
            !Check(color.payload == expected_color.payload,
                "Hydra morph normals differ from the independent lit-surface/hull oracle")) return false;
        for (std::size_t i = 0; i < depth.payload.size(); ++i) {
          if (!Check(std::abs(depth.payload[i] - expected_depth.payload[i]) < 1e-6F,
                  "Hydra morph-normal hull depth differs from oracle")) return false;
        }
        const auto stats = renderer->statistics();
        if (frame > 0 && !Check(stats.point_uploads == before.point_uploads &&
                stats.topology_uploads == before.topology_uploads &&
                stats.morph_uploads == before.morph_uploads &&
                stats.skin_uploads == before.skin_uploads &&
                stats.material_writes == before.material_writes &&
                stats.pipelines_created == before.pipelines_created,
                "animated Hydra normals uploaded static resources")) return false;
        if (!Check(stats.validation_message_count == 0 &&
                oracle->statistics().validation_message_count == 0,
                "Vulkan validation reported a morph-normal error")) return false;
        if (frame == 3 && offsets) {
          // Prove this fixture detects omitted normal offsets even when
          // morphed positions remain identical.
          reference.SetMeshNormals(ref, std::vector<Toon::Float3>(4, rest_normal));
          auto stale = Toon::ExtractDrawList(reference.Commit());
          stale.outlines = outlines;
          stale.lighting = expected.lighting;
          if (!Check(oracle->Render(stale, 128, 128, expected_color, expected_depth, error) &&
                  color.payload != expected_color.payload,
                  "the image fixture must detect missing morph normals")) return false;
        }
      }
      std::cout << "Hydra morph normals mode=" << mode << " outlines=" << outlines
          << " offsets=" << offsets << " authored=" << authored
          << " comparisons=" << frames.size() << " targets=" << renderer->statistics().morph_uploads
          << " points=" << renderer->statistics().point_uploads << '\n';
    }
  }
  return true;
}


} // namespace

#ifdef TOON_HAS_AVATAR_STATE
// Retained runtime views served from memory; the adapter only reads them.
struct HeldViews {
  static inline std::map<ArSnapshot, ArStateView> views;
  static ArStatus AR_CALL Get(ArSnapshot handle, ArStateView* view) {
    const auto found = views.find(handle);
    if (found == views.end()) return AR_INVALID_HANDLE;
    *view = found->second;
    return AR_OK;
  }
  static ArStatus AR_CALL Hold(ArSnapshot handle) {
    return views.contains(handle) ? AR_OK : AR_INVALID_HANDLE;
  }
};

// A runtime layout with the identities its USD binders publish: skeleton
// path and joint token, mesh path and `skel:blendShapes` token, material
// path and canonical input, visibility target. The ids are owned here.
struct RuntimeLayout {
  std::deque<std::string> ids;
  std::vector<ArJoint> joints;
  std::vector<ArBlendShape> shapes;
  std::vector<ArMaterialInput> materials;
  std::vector<ArVisibility> visibility;
  const char* Id(std::string id) {
    return ids.emplace_back(std::move(id)).c_str();
  }
  ArStateView View(std::uint64_t frame) const {
    ArStateView view{AR_HEADER(ArStateView)};
    view.instance = 1;
    view.frame_id = frame;
    view.generation = 1;
    view.joints = joints.data();
    view.joint_count = static_cast<std::uint32_t>(joints.size());
    view.blend_shapes = shapes.data();
    view.blend_shape_count = static_cast<std::uint32_t>(shapes.size());
    view.materials = materials.data();
    view.material_count = static_cast<std::uint32_t>(materials.size());
    view.visibility = visibility.data();
    view.visibility_count = static_cast<std::uint32_t>(visibility.size());
    view.layout_id = "skinning-test";
    view.layout_version = 1;
    return view;
  }
};

// Parent-local translation in metres, as the runtime publishes it.
ArTransform ToRuntime(const GfMatrix4d& matrix, double meters_per_unit) {
  const GfTransform transform(matrix);
  const GfVec3d translation = transform.GetTranslation() * meters_per_unit;
  const GfQuatd rotation = transform.GetRotation().GetQuat().GetNormalized();
  const GfVec3d scale = transform.GetScale();
  ArTransform result{};
  for (int k = 0; k < 3; ++k) {
    result.translation[k] = translation[k];
    result.rotation[k] = rotation.GetImaginary()[k];
    result.scale[k] = scale[k];
  }
  result.rotation[3] = rotation.GetReal();
  return result;
}

ArTransform Translated(double x, double y, double z) {
  return {{x, y, z}, {0, 0, 0, 1}, {1, 1, 1}};
}

// Applies one runtime result to `scene` through freshly matched bindings.
bool ApplyMatched(const RuntimeLayout& layout,
    const HdToonResidentTargets& resident, const Toon::FrameSnapshot& scene,
    Toon::FrameSnapshot& output, HdToonAvatarTargetMatch& match) {
  const ArStateView view = layout.View(1);
  match = HdToonMatchAvatarTargets(view, resident,
      HdToonCanonicalMaterialInputs());
  for (const HdToonAvatarMismatch& mismatch : match.mismatches) {
    std::cerr << mismatch.subject << ": " << mismatch.reason << '\n';
  }
  if (!match.mismatches.empty()) return false;
  HeldViews::views[1] = view;
  ArRuntimeApi api{AR_HEADER(ArRuntimeApi)};
  api.get_snapshot = HeldViews::Get;
  api.retain_snapshot = HeldViews::Hold;
  api.release_snapshot = HeldViews::Hold;
  Toon::AvatarStateAdapter adapter;
  Toon::RetainedAvatarSnapshot held;
  std::string error;
  const bool applied = adapter.Bind(view, scene, 1, match.bindings, error) &&
      held.Reset(api, 1, error) && adapter.Apply(held, scene, 1, output, error);
  held.Clear();
  HeldViews::views.clear();
  if (!applied) std::cerr << error << '\n';
  return applied;
}

// Every matched skin's palette, composed from runtime joints by the fast
// adapter, against the one usdSkelImaging computed.
bool SamePalettes(const HdToonAvatarTargetMatch& match,
    const Toon::FrameSnapshot& hydra, const Toon::FrameSnapshot& direct,
    float tolerance) {
  for (const Toon::AvatarSkinBinding& skin : match.bindings.skins) {
    const auto by_id = [&](const Toon::FrameSnapshot& snapshot) {
      return std::find_if(snapshot.meshes.begin(), snapshot.meshes.end(),
          [&](const auto& mesh) { return mesh.id == skin.mesh; });
    };
    const auto a = by_id(hydra);
    const auto b = by_id(direct);
    if (a == hydra.meshes.end() || b == direct.meshes.end() ||
        a->joints->size() != b->joints->size() ||
        !Near(a->skeleton_to_mesh, b->skeleton_to_mesh, tolerance)) {
      return false;
    }
    for (std::size_t i = 0; i < a->joints->size(); ++i) {
      if (!Near((*a->joints)[i], (*b->joints)[i], tolerance)) {
        std::cerr << "mesh " << skin.mesh << " palette entry " << i << '\n';
        return false;
      }
    }
  }
  return true;
}

// The stage's skeletons and blend shapes as the runtime's USD binders read
// them, not from the delegate: joint locals in skeleton order, each root
// carrying the skeleton's placement, and every skinned mesh's shape tokens.
RuntimeLayout StageLayout(const UsdStageRefPtr& stage, UsdTimeCode time,
    double meters_per_unit) {
  RuntimeLayout layout;
  UsdSkelCache cache;
  for (const UsdPrim& prim : stage->Traverse()) {
    if (const UsdSkelSkeleton skeleton{prim}) {
      const UsdSkelSkeletonQuery query = cache.GetSkelQuery(skeleton);
      VtMatrix4dArray local;
      if (!query || !query.ComputeJointLocalTransforms(&local, time)) continue;
      const GfMatrix4d placement =
          UsdGeomXformable(prim).ComputeLocalToWorldTransform(time);
      const VtTokenArray order = query.GetJointOrder();
      const UsdSkelTopology& topology = query.GetTopology();
      const char* path = layout.Id(prim.GetPath().GetString());
      for (std::size_t i = 0; i < order.size(); ++i) {
        const int parent = topology.GetParent(i);
        layout.joints.push_back({path, layout.Id(order[i].GetString()), parent,
            ToRuntime(parent < 0 ? local[i] * placement : local[i],
                meters_per_unit)});
      }
    }
    if (prim.IsA<UsdGeomMesh>() && prim.HasAPI<UsdSkelBindingAPI>()) {
      VtTokenArray names;
      UsdSkelBindingAPI(prim).GetBlendShapesAttr().Get(&names);
      const char* path = layout.Id(prim.GetPath().GetString());
      for (const TfToken& name : names) {
        layout.shapes.push_back({path, layout.Id(name.GetString()), 0.0});
      }
    }
  }
  return layout;
}

// The stage's runtime layout must bind completely to the resident targets,
// and its joints must compose usdSkelImaging's palettes.
bool StageBindings(const UsdStageRefPtr& stage, UsdTimeCode time,
    const Toon::FrameSnapshot& snapshot, const HdToonResidentTargets& resident,
    const char* label) {
  const RuntimeLayout layout =
      StageLayout(stage, time, snapshot.meters_per_unit);
  HdToonAvatarTargetMatch match;
  Toon::FrameSnapshot direct;
  std::size_t inbetweens = 0;
  if (!Check(ApplyMatched(layout, resident, snapshot, direct, match),
          "the stage's runtime layout must bind to its resident targets") ||
      !Check(SamePalettes(match, snapshot, direct, 1e-4F),
          "runtime joints must compose usdSkelImaging's palettes")) {
    return false;
  }
  for (const auto& morph : match.bindings.morphs) {
    inbetweens += morph.inbetweens.size();
  }
  std::cout << label << " runtime layout: " << layout.joints.size()
            << " joints and " << layout.shapes.size()
            << " blend shapes bound as " << match.bindings.skins.size()
            << " skins and " << match.bindings.morphs.size() << " morphs ("
            << inbetweens << " inbetween slots)\n";
  return true;
}
#endif

// Each skinned mesh's described identities against what usdSkelImaging
// computed at the stage's start time code, before and after every joint's
// rest is rotated in the session layer.
int RunStage(const char* path) {
  const UsdStageRefPtr stage = UsdStage::Open(path);
  if (!Check(stage != nullptr, "cannot open the stage")) return 1;
  UsdImagingCreateSceneIndicesInfo info;
  info.stage = stage;
  const UsdImagingSceneIndices indices = UsdImagingCreateSceneIndices(info);
  HdToonRenderDelegate delegate;
  std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
  index->InsertSceneIndex(indices.finalSceneIndex, SdfPath::AbsoluteRootPath());
  HdTaskSharedPtrVector tasks;
  HdTaskContext context;
  const HdRprimCollection collection(HdTokens->geometry,
      HdReprSelector(HdReprTokens->smoothHull));
  const UsdTimeCode time(stage->GetStartTimeCode());
  const auto sync = [&]() {
    indices.stageSceneIndex->ApplyPendingUpdates();
    indices.stageSceneIndex->SetTime(time);
    index->EnqueueCollectionToSync(collection);
    index->SyncAll(&tasks, &context);
    return delegate.CommitScene();
  };
  // Rotated rests must leave no palette entry the identity, so a joint the
  // identities mismatch cannot match by accident.
  const auto check = [&](const char* label, bool posed_only) {
    const Toon::FrameSnapshot snapshot = sync();
    const HdToonResidentTargets targets = delegate.DescribeResidentTargets();
    UsdSkelCache cache;
    std::size_t skinned = 0, palette = 0, posed = 0, subshapes = 0, bound = 0;
    for (const HdToonResidentMesh& target : targets.meshes) {
      const auto mesh = std::find_if(snapshot.meshes.begin(), snapshot.meshes.end(),
          [&](const auto& m) { return m.id == target.mesh; });
      if (!Check(mesh != snapshot.meshes.end(), "a described mesh must be committed")) return false;
      if (target.material != 0) ++bound;
      if (!target.skin) continue;
      const UsdSkelSkeletonQuery query = cache.GetSkelQuery(
          UsdSkelSkeleton(stage->GetPrimAtPath(target.skin->skeleton)));
      VtMatrix4dArray joints;
      if (!Check(query && query.ComputeJointSkelTransforms(&joints, time),
              "a described skeleton must evaluate")) return false;
      std::vector<Toon::Matrix4> skeleton_space;
      for (const GfMatrix4d& joint : joints) skeleton_space.push_back(ToToon(joint));
      const Toon::Matrix4 skeleton_to_world = ToToon(UsdGeomXformable(
          query.GetPrim()).ComputeLocalToWorldTransform(time));
      if (!Check(std::find(target.skin->skeleton_joints.begin(),
                     target.skin->skeleton_joints.end(), -1) ==
                     target.skin->skeleton_joints.end(),
              "every palette entry must name a skeleton joint") ||
          !Check(PaletteFromIdentities(*target.skin, skeleton_space,
                     skeleton_to_world, *mesh, 1e-4F),
              (target.path.GetString() + ": described identities must compose the palette").c_str()) ||
          !Check(target.subshapes.size() == mesh->morph_weights->size(),
              (target.path.GetString() + ": every morph weight slot must be described").c_str())) {
        return false;
      }
      ++skinned;
      palette += target.skin->joints.size();
      posed += static_cast<std::size_t>(std::count_if(mesh->joints->begin(),
          mesh->joints->end(), [](const Toon::Matrix4& joint) {
            return !Near(joint, Toon::Matrix4{}, 1e-4F);
          }));
      subshapes += target.subshapes.size();
    }
#ifdef TOON_HAS_AVATAR_STATE
    if (!StageBindings(stage, time, snapshot, targets, label)) return false;
#endif
    std::cout << label << ": " << targets.meshes.size() << " meshes, " << skinned
              << " GPU-skinned with " << palette << " palette entries ("
              << posed << " not the identity) and "
              << subshapes << " subshape slots, " << bound << " bound to "
              << targets.materials.size() << " materials\n";
    return Check(skinned > 0 && (!posed_only || posed == palette),
        "the check needs GPU-skinned meshes, and rotated rests a posed palette");
  };
  if (!check("authored rest", false)) return 1;
  for (const UsdPrim& prim : stage->Traverse()) {
    UsdSkelSkeleton skeleton(prim);
    if (!skeleton) continue;
    VtMatrix4dArray rest;
    skeleton.GetRestTransformsAttr().Get(&rest);
    for (std::size_t i = 0; i < rest.size(); ++i) {
      GfMatrix4d turn;
      turn.SetRotate(GfRotation(GfVec3d(1, 2, 3), 5.0 + 7.0 * static_cast<double>(i % 11)));
      rest[i] = turn * rest[i];
    }
    stage->SetEditTarget(stage->GetSessionLayer());
    skeleton.GetRestTransformsAttr().Set(rest);
  }
  if (!check("rotated rest", true)) return 1;
  index.reset();
  return 0;
}

#ifdef TOON_HAS_AVATAR_STATE
// The runtime publishes one weight per `skel:blendShapes` target. A host
// binds each to the slots the delegate describes for that shape, and the
// fast adapter must then fill exactly the subshape weights usdSkelImaging
// computes when the same weights are authored (design policy §34).
template <class Sync>
bool AvatarInbetweenParity(HdToonRenderDelegate& delegate,
    const UsdAttribute& weights, Sync sync) {
  const Toon::FrameSnapshot baseline = sync(3.0);
  const HdToonResidentTargets resident = delegate.DescribeResidentTargets();
  const HdToonResidentMesh* target = Described(resident, "/Root/Linear");
  if (!Check(target != nullptr, "the linear quad must be described")) {
    return false;
  }
  // The runtime's USD identities: the mesh path and the shape token.
  RuntimeLayout layout;
  for (const char* name : {"lift", "tilt", "plain"}) {
    layout.shapes.push_back({"/Root/Linear", name, 0});
  }
  ArStateView view = layout.View(0);
  const HdToonAvatarTargetMatch match = HdToonMatchAvatarTargets(view,
      resident, HdToonCanonicalMaterialInputs());
  const Toon::AvatarBindings& bindings = match.bindings;
  if (!Check(match.mismatches.empty() && bindings.morphs.size() == 3 &&
                 bindings.skins.empty(),
          "every runtime shape must match a described shape")) {
    return false;
  }
  const Toon::AvatarMorphBinding& lifted = bindings.morphs[0];
  if (!Check(lifted.source == 0 && lifted.mesh == target->mesh &&
                 lifted.weight == 1 && lifted.inbetweens.size() == 1 &&
                 lifted.inbetweens[0].weight == 0 &&
                 lifted.inbetweens[0].position == 0.5F &&
                 bindings.morphs[1].weight == 2 &&
                 bindings.morphs[1].inbetweens.empty() &&
                 bindings.morphs[2].weight == 3,
          "matched shapes must take their described primary and inbetween slots")) {
    return false;
  }
  ArRuntimeApi api{AR_HEADER(ArRuntimeApi)};
  api.get_snapshot = HeldViews::Get;
  api.retain_snapshot = HeldViews::Hold;
  api.release_snapshot = HeldViews::Hold;
  Toon::AvatarStateAdapter adapter;
  std::string error;
  if (!Check(adapter.Bind(view, baseline, 1, bindings, error),
          "matched subshapes must bind lift's inbetween")) {
    std::cerr << error << '\n';
    return false;
  }
  // Around, on and beyond lift's knots at 0, 0.5 and 1.
  const float lifts[] = {-1.0F, -0.5F, -0.25F, 0.0F, 0.25F, 0.4F, 0.5F,
      0.6F, 0.75F, 1.0F, 1.25F, 2.0F};
  ArSnapshot handle = 0;
  for (const float lift : lifts) {
    const VtFloatArray authored{lift, 0.3F, -0.7F};
    weights.Set(authored, UsdTimeCode(3));
    const Toon::FrameSnapshot hydra = sync(3.0);
    for (std::size_t i = 0; i < 3; ++i) layout.shapes[i].weight = authored[i];
    view.frame_id = ++handle;
    HeldViews::views[handle] = view;
    Toon::RetainedAvatarSnapshot held;
    Toon::FrameSnapshot direct;
    // The dual quaternion quad's CPU kernel rewrites its points on every
    // animation edit, which the adapter takes as a scene change, so the
    // same bindings are rebound over each synced frame.
    if (!held.Reset(api, handle, error) ||
        !adapter.Bind(view, hydra, handle + 1, bindings, error) ||
        !adapter.Apply(held, hydra, handle + 1, direct, error)) {
      std::cerr << error << '\n';
      return false;
    }
    if (!Check(*Find(direct).linear->morph_weights ==
                *Find(hydra).linear->morph_weights,
            "runtime shape weights must give usdSkelImaging's subshape weights")) {
      std::cerr << "lift " << lift << '\n';
      return false;
    }
  }
  HeldViews::views.clear();
  return true;
}

// The runtime publishes the skeleton in its own joint order and roots in
// runtime world, in metres. Matching must reorder the joints into the
// mesh's reversed palette and keep the described inverse binds and
// placement, so the fast adapter composes usdSkelImaging's palette at the
// tip's pose at time 2, with the skeleton raised by `height` units.
bool AvatarSkinParity(HdToonRenderDelegate& delegate,
    const Toon::FrameSnapshot& hydra, double height) {
  const HdToonResidentTargets resident = delegate.DescribeResidentTargets();
  const double metres = hydra.meters_per_unit;
  RuntimeLayout layout;
  layout.joints.push_back({"/Root/Skel", "base", -1,
      Translated(0, 0, height * metres)});
  layout.joints.push_back({"/Root/Skel", "base/tip", 0,
      Translated(1 * metres, 1 * metres, 0)});
  HdToonAvatarTargetMatch match;
  Toon::FrameSnapshot direct;
  if (!Check(ApplyMatched(layout, resident, hydra, direct, match),
          "runtime joints must bind to the described palette")) {
    return false;
  }
  const HdToonResidentMesh* linear = Described(resident, "/Root/Linear");
  return Check(match.bindings.skins.size() == 1 &&
                   match.bindings.skins[0].mesh == linear->mesh &&
                   match.bindings.skins[0].joints ==
                       std::vector<std::uint32_t>{1, 0},
             "only the GPU skin must bind, in its own palette order") &&
      Check(SamePalettes(match, hydra, direct, 1e-5F),
          "matched runtime joints must compose usdSkelImaging's palette");
}

// What has no resident target is reported by its runtime identity; what
// does is still bound beside it.
bool AvatarMismatches(const HdToonResidentTargets& resident) {
  RuntimeLayout layout;
  // The skeleton without the tip a palette entry needs, and one not there.
  layout.joints.push_back({"/Root/Skel", "base", -1, Translated(0, 0, 0)});
  layout.joints.push_back({"/Root/Other", "joint", -1, Translated(0, 0, 0)});
  layout.shapes.push_back({"/Root/Linear", "lift", 0});
  layout.shapes.push_back({"/Root/Linear", "missing", 0});
  layout.shapes.push_back({"/Root/DualQuaternion", "lift", 0});
  layout.materials.push_back({"/Root/Looks/Skin",
      "inputs:vrm:mtoon:shadeColorFactor", AR_VALUE_VEC3, 0, {}});
  layout.materials.push_back({"/Root/Looks/Skin",
      "inputs:vrm:material:emissiveFactor", AR_VALUE_VEC3, 0, {}});
  layout.materials.push_back({"/Root/Looks/None",
      "inputs:vrm:material:baseColorFactor", AR_VALUE_VEC3, 0, {}});
  layout.visibility.push_back({"/Root/Linear", 1});
  layout.visibility.push_back({"/Root/Nowhere", 1});
  const HdToonAvatarTargetMatch match = HdToonMatchAvatarTargets(
      layout.View(0), resident, HdToonCanonicalMaterialInputs());
  std::vector<std::string> subjects;
  for (const auto& mismatch : match.mismatches) {
    subjects.push_back(mismatch.subject);
  }
  const std::vector<std::string> expected{"/Root/Linear base/tip",
      "/Root/Other", "/Root/Skel", "/Root/Linear missing",
      "/Root/DualQuaternion lift", "/Root/Looks/Skin inputs:vrm:material:emissiveFactor",
      "/Root/Looks/None inputs:vrm:material:baseColorFactor", "/Root/Nowhere"};
  const HdToonResidentMesh* linear = Described(resident, "/Root/Linear");
  const auto& b = match.bindings;
  if (!Check(subjects == expected,
          "every runtime output without a resident target must be reported")) {
    for (const auto& mismatch : match.mismatches) {
      std::cerr << mismatch.subject << ": " << mismatch.reason << '\n';
    }
    return false;
  }
  return Check(b.skins.empty() && b.morphs.size() == 1 &&
                   b.morphs[0].source == 0 && b.materials.size() == 1 &&
                   b.materials[0].source == 0 &&
                   b.materials[0].material == resident.materials[0].material &&
                   b.materials[0].field == Toon::AvatarMaterialField::ShadeColor &&
                   b.visibility.size() == 1 && b.visibility[0].source == 0 &&
                   b.visibility[0].mesh == linear->mesh,
      "matched outputs must still bind beside the reported ones");
}
#endif

int main(int argc, char** argv) {
  if (argc == 3 && std::strcmp(argv[1], "--stage") == 0) {
    TfSetenv("USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS", "1");
    return RunStage(argv[2]);
  }
  if (argc != 2 && argc != 3) {
    std::cerr << "usage: toon-hydra2-skinning-test <skinning.usda> [shader-directory]\n"
                 "       toon-hydra2-skinning-test --stage <file>\n";
    return 2;
  }
  // What toon-viewport sets, so usdSkelImaging hands over authored normals.
  TfSetenv("USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS", "1");
  if (argc == 3) {
    Toon::FrameStatus status;
    std::string error;
    const auto probe = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(argv[2]), status, error, {1});
    if (status == Toon::FrameStatus::Skip) {
      std::cout << error << '\n';
      return 77;
    }
    if (!Check(probe != nullptr, error.c_str())) return 1;
  }
  const UsdStageRefPtr stage = UsdStage::Open(argv[1]);
  if (!Check(stage != nullptr, "cannot open the skinning stage")) {
    return 1;
  }
  UsdImagingCreateSceneIndicesInfo info;
  info.stage = stage;
  const UsdImagingSceneIndices indices = UsdImagingCreateSceneIndices(info);
  HdToonRenderDelegate delegate;
  std::unique_ptr<HdRenderIndex> index(HdRenderIndex::New(&delegate, {}));
  index->InsertSceneIndex(indices.finalSceneIndex,
      SdfPath::AbsoluteRootPath());
  HdTaskSharedPtrVector tasks;
  HdTaskContext context;
  // Rprims sync only for a collection a render pass would draw.
  const HdRprimCollection collection(HdTokens->geometry,
      HdReprSelector(HdReprTokens->smoothHull));
  const auto sync = [&](double time) {
    indices.stageSceneIndex->ApplyPendingUpdates();
    indices.stageSceneIndex->SetTime(UsdTimeCode(time));
    index->EnqueueCollectionToSync(collection);
    index->SyncAll(&tasks, &context);
    return delegate.CommitScene();
  };

  const Toon::FrameSnapshot bind = sync(1.0);
  const Meshes bind_meshes = Find(bind);
  const std::vector<Toon::Float3> rest{{0, 0, 0}, {1, 0, 0}, {1, 1, 0},
      {0, 1, 0}};
  if (!Check(bind.meshes.size() == 3,
          "the quads and the skeleton's guide must become meshes") ||
      !Check(bind_meshes.linear != nullptr && bind_meshes.dual != nullptr,
          "the linear quad must be skinned, the dual quaternion one not") ||
      !Check(bind_meshes.linear->influences_per_point == 1 &&
                 bind_meshes.linear->joints->size() == 2,
          "the skin must carry one influence per point and both joints") ||
      !Check(Near(*bind_meshes.linear->points, rest) &&
                 Near(Skinned(*bind_meshes.linear), rest),
          "the bind pose must draw the rest points") ||
      !Check(Near(*bind_meshes.dual->points, rest),
          "the CPU kernel must give the rest points in the bind pose") ||
      !Check(bind_meshes.linear->authored_normals &&
                 Near(*bind_meshes.linear->normals,
                     std::vector<Toon::Float3>(4, {0.6F, 0.0F, 0.8F})),
          "the linear quad must draw its authored normals") ||
      !Check(!bind_meshes.dual->authored_normals,
          "a quad without authored normals must derive them")) {
    return 1;
  }
  const Toon::MeshSnapshot linear_bind = *bind_meshes.linear;
  const Toon::MeshSnapshot dual_bind = *bind_meshes.dual;

  const HdToonResidentTargets resident = delegate.DescribeResidentTargets();
  const HdToonResidentMesh* linear_target = Described(resident, "/Root/Linear");
  const HdToonResidentMesh* dual_target = Described(resident, "/Root/DualQuaternion");
  const std::vector<HdToonResidentSubshape> subshapes{
      {TfToken("lift"), SdfPath("/Root/Linear/lift"), 0.5F},
      {TfToken("lift"), SdfPath("/Root/Linear/lift"), 1.0F},
      {TfToken("tilt"), SdfPath("/Root/Linear/tilt"), 1.0F},
      {TfToken("plain"), SdfPath("/Root/Linear/plain"), 1.0F}};
  if (!Check(resident.meshes.size() == bind.meshes.size() &&
                 linear_target != nullptr && dual_target != nullptr &&
                 linear_target->mesh == linear_bind.id &&
                 dual_target->mesh == dual_bind.id,
          "every committed mesh must be described by its prim path and id") ||
      !Check(linear_target->skin &&
                 linear_target->skin->skeleton == SdfPath("/Root/Skel") &&
                 linear_target->skin->joints ==
                     VtTokenArray{TfToken("base/tip"), TfToken("base")} &&
                 linear_target->skin->skeleton_joints ==
                     std::vector<std::int32_t>{1, 0},
          "the described palette must follow the mesh's own joint order") ||
      !Check(Near(linear_target->skin->inverse_bind[0], Translation(0, -1, 0)) &&
                 Near(linear_target->skin->inverse_bind[1], Toon::Matrix4{}) &&
                 Near(linear_target->skin->world_to_skeleton, Toon::Matrix4{}) &&
                 Near(linear_target->skin->skeleton_to_mesh,
                     linear_bind.skeleton_to_mesh),
          "the described skin must carry the skeleton's inverse binds and placement") ||
      !Check(linear_target->subshapes == subshapes &&
                 linear_bind.morph_weights->size() == subshapes.size(),
          "described subshapes must name every morph weight slot in order") ||
      !Check(!dual_target->skin && dual_target->subshapes.empty(),
          "a mesh the CPU kernel skins must describe no resident palette") ||
      !Check(resident.materials.size() == 1 &&
                 resident.materials[0].path == SdfPath("/Root/Looks/Skin") &&
                 linear_target->material_path == SdfPath("/Root/Looks/Skin") &&
                 linear_target->material == resident.materials[0].material &&
                 linear_bind.material == linear_target->material &&
                 dual_target->material == 0,
          "the bound material must be described by its path and committed id")) {
    return 1;
  }

  // Only the pose changes.
  const Toon::FrameSnapshot moved = sync(2.0);
  const Meshes moved_meshes = Find(moved);
  const std::vector<Toon::Float3> reached{{0, 0, 0}, {1, 0, 0}, {2, 1, 0},
      {1, 1, 0}};
  if (!Check(moved_meshes.linear != nullptr && moved_meshes.dual != nullptr,
          "both quads must remain") ||
      !Check(Near(Skinned(*moved_meshes.linear), reached),
          "the moved tip must carry its points") ||
      !Check(moved_meshes.linear->pose_revision != linear_bind.pose_revision,
          "a pose change must advance the pose revision") ||
      !Check(moved_meshes.linear->points_revision ==
                     linear_bind.points_revision &&
                 moved_meshes.linear->skin_revision ==
                     linear_bind.skin_revision &&
                 moved_meshes.linear->topology_revision ==
                     linear_bind.topology_revision &&
                 moved_meshes.linear->normals_revision ==
                     linear_bind.normals_revision,
          "a pose change must not touch points, normals, skin or topology") ||
      !Check(Near(*moved_meshes.dual->points, reached) &&
                 moved_meshes.dual->points_revision !=
                     dual_bind.points_revision,
          "dual quaternion skinning must run the CPU kernel")) {
    return 1;
  }
  const Toon::MeshSnapshot linear_moved = *moved_meshes.linear;

  const auto half = sync(2.5);
  const auto half_meshes = Find(half);
  if (!Check(half_meshes.linear != nullptr &&
          std::abs(Skinned(*half_meshes.linear)[0].z - 0.75F) < 1e-4F &&
          half_meshes.linear->points_revision == linear_moved.points_revision &&
          half_meshes.linear->morph_revision == linear_moved.morph_revision &&
          Near(MorphedNormals(*half_meshes.linear),
              {{0.4F, 0.125F, 1.0F}, {0.6F, 0.125F, 0.8F},
                  {0.6F, 0.125F, 0.8F}, {0.6F, 0.125F, 0.8F}}),
          "UsdSkel inbetween weights must deform the GPU targets without editing rest geometry")) {
    return 1;
  }

  // Only the blend shape weight changes.
  const Toon::FrameSnapshot lifted = sync(3.0);
  const Meshes lifted_meshes = Find(lifted);
  if (!Check(lifted_meshes.linear != nullptr, "the linear quad must remain") ||
      !Check(lifted_meshes.linear->points_revision ==
                     linear_moved.points_revision &&
                 lifted_meshes.linear->morph_revision == linear_moved.morph_revision &&
                 lifted_meshes.linear->morph_weights_revision != linear_moved.morph_weights_revision &&
                 (*lifted_meshes.linear->points)[0].z == 0.0F &&
                 Skinned(*lifted_meshes.linear)[0].z == 1.0F,
          "a blend shape weight must move only morph weights, preserving rest points and targets") ||
      !Check(lifted_meshes.linear->authored_normals &&
                 lifted_meshes.linear->normals_revision ==
                     linear_moved.normals_revision,
          "moved rest points must keep the authored normals") ||
      !Check(lifted_meshes.linear->pose_revision ==
                     linear_moved.pose_revision &&
                 lifted_meshes.linear->skin_revision ==
                     linear_moved.skin_revision,
          "a weight change must not touch the pose or skin")) {
    return 1;
  }
  if (!Check(Near(MorphedNormals(*lifted_meshes.linear),
          {{0, 0.25F, 1.4F}, {0.6F, 0.25F, 0.8F},
              {0.6F, 0.25F, 0.8F}, {0.6F, 0.25F, 0.8F}}),
          "sparse, dense normal-only and missing normal offsets must use their packed weight slots")) {
    return 1;
  }
  if (argc == 3 && !GpuNormals({bind, moved, half, lifted}, argv[2])) return 1;
  // The host can submit evaluated expression weights with no SetTime,
  // USD authoring, scene-index update or SyncAll between these commits.
  const auto expression_id = lifted_meshes.linear->id;
  const auto expression_weights = *Find(half).linear->morph_weights;
  if (!Check(delegate.SetMeshMorphWeightsOverride(expression_id, expression_weights),
          "direct host morph override rejected")) return 1;
  const auto expression = delegate.CommitScene();
  const auto expression_mesh = Find(expression).linear;
  if (!Check(expression_mesh && *expression_mesh->morph_weights == expression_weights &&
          expression_mesh->points == lifted_meshes.linear->points &&
          expression_mesh->normals == lifted_meshes.linear->normals &&
          expression_mesh->morph_revision == lifted_meshes.linear->morph_revision &&
          expression_mesh->pose_revision == lifted_meshes.linear->pose_revision,
          "direct expression override touched slow state or failed to reach the host")) return 1;
  const auto expression_synced = sync(3.0);
  if (!Check(*Find(expression_synced).linear->morph_weights == expression_weights,
          "Hydra sync displaced a host expression override")) return 1;
  delegate.ClearMeshMorphWeightsOverride(expression_id);
  const auto expression_cleared = delegate.CommitScene();
  if (!Check(*Find(expression_cleared).linear->morph_weights == *lifted_meshes.linear->morph_weights,
          "clearing a direct expression did not restore Hydra weights")) return 1;
  if (argc == 3 && !GpuNormals({bind, moved, expression, expression_cleared}, argv[2])) return 1;
  const auto before_edit = *Find(expression_cleared).linear;
  const auto normal_attr = stage->GetPrimAtPath(SdfPath("/Root/Linear/lift"))
      .GetAttribute(TfToken("normalOffsets"));
  normal_attr.Set(VtVec3fArray{GfVec3f(-0.4F, 0, 0.4F)});
  const auto edited = sync(3.0);
  const auto edited_mesh = Find(edited).linear;
  if (!Check(edited_mesh && edited_mesh->morph_revision != before_edit.morph_revision &&
          edited_mesh->points_revision == before_edit.points_revision &&
          edited_mesh->normals_revision == before_edit.normals_revision &&
          edited_mesh->skin_revision == before_edit.skin_revision &&
          edited_mesh->pose_revision == before_edit.pose_revision &&
          edited_mesh->morph_weights_revision == before_edit.morph_weights_revision &&
          Near(MorphedNormals(*edited_mesh),
              {{0.2F, 0.25F, 1.2F}, {0.6F, 0.25F, 0.8F},
                  {0.6F, 0.25F, 0.8F}, {0.6F, 0.25F, 0.8F}}),
          "editing normal offsets must refresh targets without touching rest geometry or weights")) {
    return 1;
  }
  normal_attr.Clear();
  const auto cleared = sync(3.0);
  if (!Check(Near(MorphedNormals(*Find(cleared).linear),
          std::vector<Toon::Float3>(4, {0.6F, 0.25F, 0.8F})),
          "removing normal offsets must retain rest normals")) {
    return 1;
  }
  // A malformed sparse normal array must not disable valid positions.
  normal_attr.Set(VtVec3fArray{GfVec3f(1, 0, 0), GfVec3f(1, 0, 0)});
  const auto malformed = sync(3.0);
  if (!Check(Near(MorphedNormals(*Find(malformed).linear),
          std::vector<Toon::Float3>(4, {0.6F, 0.25F, 0.8F})) &&
          std::abs(Skinned(*Find(malformed).linear)[0].z - 1.0F) < 1e-4F,
          "malformed sparse normals must be ignored without losing position morphs")) return 1;
  normal_attr.Clear();
  const auto weight_attr = stage->GetPrimAtPath(SdfPath("/Root/Skel/Anim"))
      .GetAttribute(TfToken("blendShapeWeights"));
  weight_attr.Set(VtFloatArray{-0.5F, -0.25F, 0.5F}, UsdTimeCode(3));
  const auto signed_weights = sync(3.0);
  const auto signed_mesh = Find(signed_weights).linear;
  if (!Check(signed_mesh && std::abs(Skinned(*signed_mesh)[0].z + 0.75F) < 1e-4F &&
          Near(MorphedNormals(*signed_mesh),
              {{0.8F, -0.0625F, 0.6F}, {0.6F, -0.0625F, 0.8F},
                  {0.6F, -0.0625F, 0.8F}, {0.6F, -0.0625F, 0.8F}}),
          "signed fractional weights must extrapolate position and normal inbetweens together")) return 1;
#ifdef TOON_HAS_AVATAR_STATE
  if (!AvatarInbetweenParity(delegate, weight_attr, sync) ||
      !AvatarMismatches(delegate.DescribeResidentTargets())) {
    return 1;
  }
#endif
  weight_attr.Set(VtFloatArray{1, 1, 1}, UsdTimeCode(3));
  stage->GetPrimAtPath(SdfPath("/Root/Linear/lift"))
      .GetAttribute(TfToken("inbetweens:half:normalOffsets")).Clear();
  stage->GetPrimAtPath(SdfPath("/Root/Linear/tilt"))
      .GetAttribute(TfToken("normalOffsets")).Clear();
  const std::vector<Toon::FrameSnapshot> fallback{sync(1), sync(2), sync(2.5), sync(3)};
  for (const auto& frame : fallback) {
    const auto mesh = Find(frame).linear;
    if (!Check(mesh && Near(MorphedNormals(*mesh),
            std::vector<Toon::Float3>(4, {0.6F, 0, 0.8F})) &&
            mesh->normals_revision == Find(fallback[0]).linear->normals_revision,
            "position-only morphs must retain authored rest normals")) return 1;
  }
  if (argc == 3 && !GpuNormals(fallback, argv[2], false)) return 1;
  stage->GetPrimAtPath(SdfPath("/Root/Linear")).RemoveProperty(TfToken("primvars:normals"));
  const std::vector<Toon::FrameSnapshot> derived{sync(1), sync(2), sync(2.5), sync(3)};
  for (const auto& frame : derived) {
    const auto mesh = Find(frame).linear;
    if (!Check(mesh && !mesh->authored_normals && Near(MorphedNormals(*mesh),
            std::vector<Toon::Float3>(4, {0, 0, 1})) &&
            mesh->normals_revision == Find(derived[0]).linear->normals_revision,
            "position-only morphs must retain derived rest normals")) return 1;
  }
  if (argc == 3 && !GpuNormals(derived, argv[2], false, false)) return 1;

  // Moving the skeleton changes only its placement: the described palette
  // identities stay, and composing a pose from them still gives the palette.
  const auto described_skin = *Described(delegate.DescribeResidentTargets(),
      "/Root/Linear")->skin;
  const std::vector<Toon::Matrix4> moved_pose{Toon::Matrix4{}, Translation(1, 1, 0)};
  const auto placed = sync(2.0);
  if (!Check(PaletteFromIdentities(described_skin, moved_pose, Toon::Matrix4{},
                 *Find(placed).linear),
          "described identities must compose the posed palette")) return 1;
#ifdef TOON_HAS_AVATAR_STATE
  if (!AvatarSkinParity(delegate, placed, 0)) return 1;
#endif
  UsdGeomXformable(stage->GetPrimAtPath(SdfPath("/Root/Skel")))
      .AddTranslateOp().Set(GfVec3d(0, 0, 2));
  const auto moved_skeleton = sync(2.0);
  const auto placed_skin = *Described(delegate.DescribeResidentTargets(),
      "/Root/Linear")->skin;
  if (!Check(placed_skin.joints == described_skin.joints &&
                 placed_skin.skeleton_joints == described_skin.skeleton_joints &&
                 placed_skin.inverse_bind == described_skin.inverse_bind &&
                 Near(placed_skin.world_to_skeleton, Translation(0, 0, -2)) &&
                 Near(placed_skin.skeleton_to_mesh, Translation(0, 0, 2)),
          "a skeleton placement change must refresh only the described placement") ||
      !Check(PaletteFromIdentities(placed_skin, moved_pose, Translation(0, 0, 2),
                 *Find(moved_skeleton).linear),
          "a placed skeleton's identities must still compose the palette")) return 1;
#ifdef TOON_HAS_AVATAR_STATE
  if (!AvatarSkinParity(delegate, moved_skeleton, 2)) return 1;
#endif
  index.reset();
  return 0;
}
