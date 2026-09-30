// SPDX-License-Identifier: Apache-2.0
#include "hydra_scene.hpp"

#include "adapter.hpp"

#include <pxr/pxr.h>

#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/setenv.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace Toon::viewport {
namespace {

constexpr char kNormalComputations[] =
    "USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS";

// Draws nothing: it states the render tags a frame shows, as usdview's
// default does — geometry and proxy, not guides such as usdSkelImaging's
// skeleton mesh — so only those rprims sync.
class RenderTagTask final : public HdTask {
public:
  RenderTagTask()
      : HdTask(SdfPath("/__toonViewportRenderTags")),
        tags_{HdRenderTagTokens->geometry, HdRenderTagTokens->proxy} {
  }

  void Sync(HdSceneDelegate* delegate, HdTaskContext* context,
      HdDirtyBits* dirty_bits) override {
    (void)delegate;
    (void)context;
    *dirty_bits = 0;
  }

  void Prepare(HdTaskContext* context, HdRenderIndex* index) override {
    (void)context;
    (void)index;
  }

  void Execute(HdTaskContext* context) override {
    (void)context;
  }

  const TfTokenVector& GetRenderTags() const override {
    return tags_;
  }

private:
  TfTokenVector tags_;
};

// No Hydra task draws: the render index is synced for the geometry
// collection a render pass would draw, and the viewport draws the committed
// scene itself, so nothing is rendered offscreen and read back.
class UsdHydraScene final : public HydraScene {
public:
  explicit UsdHydraScene(UsdStageRefPtr stage)
      : stage_(std::move(stage)),
        collection_(HdTokens->geometry,
            HdReprSelector(HdReprTokens->smoothHull)),
        tasks_{std::make_shared<RenderTagTask>()} {
    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage_;
    indices_ = UsdImagingCreateSceneIndices(info);
    index_.reset(HdRenderIndex::New(&delegate_, {}));
    if (index_ == nullptr) {
      throw std::runtime_error("could not create the Hydra render index");
    }
    index_->InsertSceneIndex(indices_.finalSceneIndex,
        SdfPath::AbsoluteRootPath());
    // Leave static stages at Default until an explicit time is requested.
    indices_.stageSceneIndex->SetTime(stage_->HasAuthoredTimeCodeRange()
                                          ? UsdTimeCode(
                                                stage_->GetStartTimeCode())
                                          : UsdTimeCode::Default());
    if (stage_->HasAuthoredTimeCodeRange()) {
      SetTime(stage_->GetStartTimeCode());
    }
    meters_per_unit_ =
        static_cast<float>(UsdGeomGetStageMetersPerUnit(stage_));
    up_axis_ = UsdGeomGetStageUpAxis(stage_) == UsdGeomTokens->z ? UpAxis::Z
                                                                 : UpAxis::Y;
  }

  ~UsdHydraScene() override {
    // Prims are destroyed through the delegate, so the index goes first.
    index_.reset();
  }

  void Update(FrameSnapshot& snapshot) override {
    indices_.stageSceneIndex->ApplyPendingUpdates();
    index_->EnqueueCollectionToSync(collection_);
    index_->SyncAll(&tasks_, &context_);
    delegate_.CommitScene(snapshot);
  }

  void SetTime(double time) override {
    indices_.stageSceneIndex->SetTime(UsdTimeCode(time));
    delegate_.SetRenderSetting(TfToken("toon:timeSeconds"),
        VtValue(time / stage_->GetTimeCodesPerSecond()));
  }

  double start_time() const noexcept override {
    return stage_->GetStartTimeCode();
  }

  float meters_per_unit() const noexcept override {
    return meters_per_unit_;
  }

  UpAxis up_axis() const noexcept override {
    return up_axis_;
  }

private:
  UsdStageRefPtr stage_;
  UsdImagingSceneIndices indices_;
  HdToonRenderDelegate delegate_;
  std::unique_ptr<HdRenderIndex> index_;
  HdRprimCollection collection_;
  HdTaskSharedPtrVector tasks_;
  HdTaskContext context_;
  float meters_per_unit_ = 1.0F;
  UpAxis up_axis_ = UpAxis::Y;
};

} // namespace

std::unique_ptr<HydraScene> HydraScene::Open(const std::string& path) {
  // usdSkelImaging hands a skinned mesh's authored normals to Hydra only with
  // this setting, and hdToon draws derived smooth normals without them, which
  // split where a model's meshes meet. It is read once, when the first scene
  // index needs it, so it is set before any is created; a value the user set
  // is kept.
  if (TfGetenv(kNormalComputations).empty()) {
    TfSetenv(kNormalComputations, "1");
  }
  UsdStageRefPtr stage = UsdStage::Open(path);
  if (stage == nullptr) {
    throw std::runtime_error("could not open the USD stage " + path);
  }
  return std::make_unique<UsdHydraScene>(std::move(stage));
}

} // namespace Toon::viewport
