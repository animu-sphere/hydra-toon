// SPDX-License-Identifier: Apache-2.0
//
// hdToon's skinned meshes, through UsdImaging and usdSkelImaging as a host
// drives them (design policy §11). skinning.usda's linear quad becomes a
// skin and a pose; moving to a time where only the pose changes sets the
// pose alone, and a blend shape weight change sets only morph weights. The linear
// quad's authored normals arrive as its normals computation's rest normals,
// which the pose does not touch. Its dual quaternion quad runs
// usdSkelImaging's CPU kernel instead. No GPU is used: the check reads the
// scene the next frame would draw.
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/tf/setenv.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <toon/render_world.hpp>

#include <cmath>
#include <iostream>
#include <memory>
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


} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: toon-hydra2-skinning-test <skinning.usda>\n";
    return 2;
  }
  // What toon-viewport sets, so usdSkelImaging hands over authored normals.
  TfSetenv("USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS", "1");
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
          half_meshes.linear->morph_revision == linear_moved.morph_revision,
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
  index.reset();
  return 0;
}
