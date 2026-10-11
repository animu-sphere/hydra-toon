// SPDX-License-Identifier: Apache-2.0
//
// A mesh's GeomSubset material bindings, through UsdImaging's scene indices
// and scene index emulation as a host drives them: each subset that binds a
// material becomes one part of the mesh's triangle list, and faces no such
// subset names draw with the mesh's own binding.
#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

Toon::MaterialId MaterialAt(const HdToonResidentTargets& targets,
    const char* path) {
  for (const HdToonResidentMaterial& material : targets.materials) {
    if (material.path == SdfPath(path)) {
      return material.material;
    }
  }
  return 0;
}

// The points a part's triangles index.
std::set<std::uint32_t> PointsOf(const Toon::MeshSnapshot& mesh,
    const Toon::MeshPart& part) {
  return {mesh.indices->begin() + part.first_index,
      mesh.indices->begin() + part.first_index + part.index_count};
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: toon-hydra2-subset-test <geom-subsets.usda>\n";
    return 2;
  }
  const UsdStageRefPtr stage = UsdStage::Open(argv[1]);
  if (!Check(stage != nullptr, "cannot open the subset stage")) {
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
  const auto sync = [&]() {
    indices.stageSceneIndex->ApplyPendingUpdates();
    index->EnqueueCollectionToSync(collection);
    index->SyncAll(&tasks, &context);
    return delegate.CommitScene();
  };

  const Toon::FrameSnapshot first = sync();
  const HdToonResidentTargets targets = delegate.DescribeResidentTargets();
  const Toon::MaterialId base = MaterialAt(targets, "/Root/Looks/Base");
  const Toon::MaterialId red = MaterialAt(targets, "/Root/Looks/Red");
  const Toon::MaterialId blue = MaterialAt(targets, "/Root/Looks/Blue");
  if (!Check(first.meshes.size() == 1 && first.meshes[0].parts != nullptr,
          "the stage must commit one mesh with parts") ||
      !Check(base != 0 && red != 0 && blue != 0,
          "each material prim must be resident")) {
    return 1;
  }
  const Toon::MeshSnapshot& mesh = first.meshes[0];
  const std::vector<Toon::MeshPart> expected{{0, 9, red}, {9, 3, blue},
      {12, 3, base}};
  if (!Check(mesh.material == base,
          "the mesh must keep its own binding") ||
      !Check(*mesh.parts == expected,
          "each bound subset must be a part, in the subsets' order, and the "
          "faces of no bound subset must follow with the mesh's material")) {
    for (const Toon::MeshPart& part : *mesh.parts) {
      std::cerr << "  part first=" << part.first_index
                << " count=" << part.index_count
                << " material=" << part.material << '\n';
    }
    return 1;
  }
  if (!Check(PointsOf(mesh, expected[0]) ==
                     std::set<std::uint32_t>{0, 1, 2, 3, 7, 8, 9} &&
                 PointsOf(mesh, expected[1]) ==
                     std::set<std::uint32_t>{10, 11, 12} &&
                 PointsOf(mesh, expected[2]) ==
                     std::set<std::uint32_t>{4, 5, 6},
          "each part must hold its own faces' triangles")) {
    return 1;
  }

  // A material prim that goes away leaves its subset unlit, and the
  // topology as it was.
  stage->RemovePrim(SdfPath("/Root/Looks/Blue"));
  const Toon::FrameSnapshot removed = sync();
  if (!Check(removed.meshes.size() == 1 &&
                 *removed.meshes[0].parts ==
                     std::vector<Toon::MeshPart>{{0, 9, red}, {9, 3, 0},
                         {12, 3, base}},
          "a removed material must leave its subset's part unbound") ||
      !Check(removed.meshes[0].topology_revision == mesh.topology_revision,
          "a removed material must not touch the topology")) {
    return 1;
  }

  // Rebinding a subset reaches the parts. Two subsets of one material stay
  // two parts, in the subsets' order.
  UsdShadeMaterialBindingAPI::Apply(
      stage->GetPrimAtPath(SdfPath("/Root/Body/Blue")))
      .Bind(UsdShadeMaterial::Get(stage, SdfPath("/Root/Looks/Base")));
  const Toon::FrameSnapshot rebound = sync();
  if (!Check(rebound.meshes.size() == 1 &&
                 *rebound.meshes[0].parts ==
                     std::vector<Toon::MeshPart>{{0, 9, red}, {9, 6, base}},
          "a subset rebound to the mesh's material must merge with its "
          "faces")) {
    for (const Toon::MeshPart& part : *rebound.meshes[0].parts) {
      std::cerr << "  part first=" << part.first_index
                << " count=" << part.index_count
                << " material=" << part.material << '\n';
    }
    return 1;
  }

  // Without subsets the mesh is one part again.
  stage->RemovePrim(SdfPath("/Root/Body/Red"));
  stage->RemovePrim(SdfPath("/Root/Body/Blue"));
  stage->RemovePrim(SdfPath("/Root/Body/Unbound"));
  const Toon::FrameSnapshot whole = sync();
  if (!Check(whole.meshes.size() == 1 &&
                 *whole.meshes[0].parts ==
                     std::vector<Toon::MeshPart>{{0, 15, base}},
          "a mesh without subsets must draw whole with its own binding")) {
    return 1;
  }
  index.reset();
  std::cout << "subsets: 3 parts, removal, rebinding and removal of the "
               "subsets reached the mesh\n";
  return 0;
}
