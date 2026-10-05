// SPDX-License-Identifier: Apache-2.0
#include "hydra_scene.hpp"

#include "adapter.hpp"
#include <toon/vulkan_backend.hpp>

#include <pxr/pxr.h>

#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/setenv.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hd/sceneIndexObserver.h>
#include <pxr/imaging/hd/task.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <memory>
#include <optional>
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
class UsdHydraScene final : public HydraScene, public HdSceneIndexObserver {
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
    indices_.finalSceneIndex->AddObserver(HdSceneIndexObserverPtr(this));
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
    indices_.finalSceneIndex->RemoveObserver(HdSceneIndexObserverPtr(this));
    // Prims are destroyed through the delegate, so the index goes first.
    index_.reset();
  }

  void Update(FrameSnapshot& snapshot) override {
    indices_.stageSceneIndex->ApplyPendingUpdates();
    if (needs_sync_) {
      needs_sync_ = false;
      index_->EnqueueCollectionToSync(collection_);
      index_->SyncAll(&tasks_, &context_);
      ++sync_count_;
    }
    delegate_.CommitScene(snapshot);
    snapshot.inputs = inputs_;
  }

  void ReadFast(FrameSnapshot& snapshot) override {
    delegate_.CommitScene(snapshot);
    snapshot.inputs = inputs_;
  }

  void SetTime(double time) override {
    if (time_ && *time_ == time) {
      return;
    }
    time_ = time;
    inputs_.pose = inputs_.expression = Toon::SteadyNanoseconds();
    needs_sync_ = true;
    indices_.stageSceneIndex->SetTime(UsdTimeCode(time));
    delegate_.SetRenderSetting(TfToken("toon:timeSeconds"),
        VtValue(time / stage_->GetTimeCodesPerSecond()));
  }

  double start_time() const noexcept override {
    return stage_->GetStartTimeCode();
  }

  bool SetMorphWeightsOverride(MeshId mesh, std::vector<float> weights) override {
    if (!delegate_.SetMeshMorphWeightsOverride(mesh, std::move(weights))) return false;
    inputs_.expression = Toon::SteadyNanoseconds();
    return true;
  }

  void ClearMorphWeightsOverride(MeshId mesh) override {
    delegate_.ClearMeshMorphWeightsOverride(mesh);
    inputs_.expression = Toon::SteadyNanoseconds();
  }

  std::vector<SkeletonDebug> ReadSkeletons() override {
    // Cache invalidation follows USD notices, including binding/animation edits.
    // No evaluation or traversal takes place while diagnostics are closed.
    if (skeletons_dirty_) {
      skeleton_cache_.Clear();
      skeleton_queries_.clear();
      for (const UsdPrim& prim : stage_->Traverse()) {
        if (prim.IsA<UsdSkelSkeleton>()) {
          skeleton_queries_.emplace_back(prim.GetPath().GetString(),
              skeleton_cache_.GetSkelQuery(UsdSkelSkeleton(prim)));
        }
      }
      skeletons_dirty_ = false;
    }
    UsdGeomXformCache transforms(time_ ? UsdTimeCode(*time_) :
        stage_->HasAuthoredTimeCodeRange() ? UsdTimeCode(stage_->GetStartTimeCode()) :
                                           UsdTimeCode::Default());
    std::vector<SkeletonDebug> result;
    for (const auto& [path, query] : skeleton_queries_) {
      SkeletonDebug skeleton;
      skeleton.path = path;
      VtMatrix4dArray world;
      if (!query || !query.ComputeJointWorldTransforms(&world, &transforms)) {
        skeleton.error = "Joint transforms unavailable";
      } else {
        const auto names = query.GetJointOrder();
        const auto& parents = query.GetTopology().GetParentIndices();
        if (world.size() != names.size() || parents.size() != names.size()) {
          skeleton.error = "Joint order and transform counts differ";
        } else {
          for (std::size_t i = 0; i < names.size(); ++i) {
            const GfVec3d origin = world[i].Transform(GfVec3d(0));
            skeleton.joints.push_back({names[i].GetString(), parents[i],
                {static_cast<float>(origin[0]), static_cast<float>(origin[1]),
                    static_cast<float>(origin[2])}});
          }
        }
      }
      result.push_back(std::move(skeleton));
    }
    return result;
  }

  double end_time() const noexcept override {
    return stage_->GetEndTimeCode();
  }

  double time_codes_per_second() const noexcept override {
    return stage_->GetTimeCodesPerSecond();
  }

  std::uint64_t sync_count() const noexcept override { return sync_count_; }

  void PrimsAdded(const HdSceneIndexBase&, const AddedPrimEntries&) override {
    skeletons_dirty_ = true;
    needs_sync_ = true;
  }
  void PrimsRemoved(const HdSceneIndexBase&, const RemovedPrimEntries&) override {
    skeletons_dirty_ = true;
    needs_sync_ = true;
  }
  void PrimsDirtied(const HdSceneIndexBase&, const DirtiedPrimEntries&) override {
    skeletons_dirty_ = true;
    needs_sync_ = true;
  }
  void PrimsRenamed(const HdSceneIndexBase&, const RenamedPrimEntries&) override {
    skeletons_dirty_ = true;
    needs_sync_ = true;
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
  bool needs_sync_ = true;
  std::uint64_t sync_count_ = 0;
  std::optional<double> time_;
  FrameSnapshot::InputTimes inputs_;
  UsdSkelCache skeleton_cache_;
  std::vector<std::pair<std::string, UsdSkelSkeletonQuery>> skeleton_queries_;
  bool skeletons_dirty_ = true;
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
