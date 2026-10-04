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
// comparisons against independently deformed rest geometry.
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/tf/setenv.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <toon/render_world.hpp>
#include <toon/vulkan_backend.hpp>

#include <cmath>
#include <iostream>
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

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    std::cerr << "usage: toon-hydra2-skinning-test <skinning.usda> [shader-directory]\n";
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
  index.reset();
  return 0;
}
