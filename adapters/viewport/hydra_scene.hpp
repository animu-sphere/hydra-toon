// SPDX-License-Identifier: Apache-2.0
// A USD stage hosted through Hydra in the viewport's own frame loop (design
// policy §31). Built only with the Hydra adapter; OpenUSD types stay in
// hydra_scene.cpp.
#pragma once

#include "camera.hpp"
#include "skeleton_debug.hpp"

#include <toon/render_world.hpp>

#include <memory>
#include <string>

namespace Toon::viewport {

class HydraScene {
public:
  // Opens the stage and populates a render index through UsdImaging's scene
  // index chain with hdToon's render delegate, as `usdview` populates one,
  // so a format plugin's scene index contributions reach the delegate.
  // Throws std::runtime_error when the stage cannot be opened.
  static std::unique_ptr<HydraScene> Open(const std::string& path);
  virtual ~HydraScene() = default;

  // Applies pending stage updates; syncs the render index only after time or
  // scene-index changes, then commits the delegate's world into `snapshot`.
  // The snapshot's camera is not the viewport's; the caller sets its own.
  virtual void Update(FrameSnapshot& snapshot) = 0;
  // Commit already evaluated values only, after the GPU/acquire wait.
  // Does not apply pending USD changes, change time or sync Hydra.
  virtual void ReadFast(FrameSnapshot& snapshot) = 0;
  // Explicit USD time codes for repeatable animation evaluation.
  virtual void SetTime(double time) = 0;
  // Evaluated subshape debugging, through the delegate's transient override
  // route. Calls never author USD or request Hydra sync. Update or ReadFast
  // publishes the result; ids and overrides belong to this scene instance.
  virtual bool SetMorphWeightsOverride(MeshId mesh, std::vector<float> weights) = 0;
  virtual void ClearMorphWeightsOverride(MeshId mesh) = 0;
  // Read the selected USD time's joint hierarchy and evaluated world origins.
  // Diagnostic only: never authors USD, commits renderer state or syncs Hydra.
  [[nodiscard]] virtual std::vector<SkeletonDebug> ReadSkeletons() = 0;
  [[nodiscard]] virtual double start_time() const noexcept = 0;
  [[nodiscard]] virtual double end_time() const noexcept = 0;
  [[nodiscard]] virtual double time_codes_per_second() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t sync_count() const noexcept = 0;

  // The stage's metersPerUnit and upAxis, which Hydra does not carry.
  [[nodiscard]] virtual float meters_per_unit() const noexcept = 0;
  [[nodiscard]] virtual UpAxis up_axis() const noexcept = 0;
};

} // namespace Toon::viewport
