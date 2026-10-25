// SPDX-License-Identifier: Apache-2.0
// A USD stage through UsdImaging into hdToon's delegate, synced into a
// FrameSnapshot: the scene of the measurement host and the Tessera host.
// Test and example code only.
#pragma once

#include "adapter.hpp"
#include "camera.hpp"

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <memory>
#include <stdexcept>

namespace toon_stage {

// The stage through UsdImaging into hdToon's delegate, synced once.
struct Scene {
  PXR_NS::HdToonRenderDelegate delegate;
  PXR_NS::UsdImagingSceneIndices indices;
  std::unique_ptr<PXR_NS::HdRenderIndex> index;
  Toon::viewport::UpAxis up = Toon::viewport::UpAxis::Y;

  explicit Scene(const PXR_NS::UsdStageRefPtr& stage) {
    using namespace PXR_NS;
    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage;
    indices = UsdImagingCreateSceneIndices(info);
    index.reset(HdRenderIndex::New(&delegate, {}));
    if (index == nullptr) throw std::runtime_error("could not create the Hydra render index");
    index->InsertSceneIndex(indices.finalSceneIndex, SdfPath::AbsoluteRootPath());
    indices.stageSceneIndex->SetTime(stage->HasAuthoredTimeCodeRange()
                                         ? UsdTimeCode(stage->GetStartTimeCode())
                                         : UsdTimeCode::Default());
    up = UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z
             ? Toon::viewport::UpAxis::Z
             : Toon::viewport::UpAxis::Y;
  }
  ~Scene() { index.reset(); }
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  Toon::FrameSnapshot Sync() {
    using namespace PXR_NS;
    indices.stageSceneIndex->ApplyPendingUpdates();
    index->EnqueueCollectionToSync(HdRprimCollection(HdTokens->geometry,
        HdReprSelector(HdReprTokens->smoothHull)));
    HdTaskSharedPtrVector tasks;
    HdTaskContext context;
    index->SyncAll(&tasks, &context);
    return delegate.CommitScene();
  }
};

} // namespace toon_stage
