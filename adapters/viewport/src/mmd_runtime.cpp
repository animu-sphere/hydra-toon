// SPDX-License-Identifier: Apache-2.0
#include "avatar_runtime.hpp"
#include "hydra_scene.hpp"
#include "hydra_scene_parts.hpp"
#include "avatar_binding.hpp"
#include <avatarMmdSession/Session.h>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace Toon::viewport {
namespace {
using namespace PXR_NS;
class MmdHost final : public AvatarRuntime {
public:
  MmdHost(const HydraScene& scene, const FrameSnapshot& bound, std::uint64_t epoch,
      const AvatarRuntimeOptions& options)
      : stage_(scene.parts().stage), delegate_(scene.parts().delegate), baseline_(bound),
        epoch_(epoch), options_(options) {
    status_.physics = true;
    status_.avatar = "/Asset";
    Start();
    if (options.play) Control(Action::Play);
  }
  ~MmdHost() override {
    current_.Clear();
    if (session_) {
      try { session_->Close(); std::cout << "MMD physics: closed bodies=" << session_->GetStats().bodies << '\n'; }
      catch (const std::exception& e) { std::cerr << "MMD physics close: " << e.what() << '\n'; }
    }
  }
  void Tick(double elapsed) override {
    if (!session_ || !status_.playing) return;
    try {
      const auto before = session_->GetStats().steps;
      const auto started = std::chrono::steady_clock::now();
      const auto stats = session_->Advance(elapsed);
      tick_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      measured_ticks_ += stats.steps - before;
      status_.physics_steps = stats.steps;
      status_.generation = stats.generation;
      if (stats.steps > before) {
        std::string error;
        if (!current_.Reset(session_->Api(), session_->Snapshot(), error)) throw std::runtime_error(error);
        const auto& view = *current_.View();
        if (view.frame_id != stats.steps) throw std::runtime_error("physics tick/snapshot mismatch");
        status_.frame = view.frame_id;
        status_.evaluation_seconds = view.evaluation_seconds;
        ++status_.published;
        if (options_.reset_every && status_.published % options_.reset_every == 0) {
          Control(Action::Reset);
          Control(Action::Play);
        }
      }
    } catch (const std::exception& e) {
      session_->Pause(); status_.playing = false; ++status_.refused; status_.error = e.what();
      std::cerr << "MMD physics paused: " << e.what() << '\n';
    }
  }
  bool Apply(FrameSnapshot& scene, bool late, std::string& error) override {
    if (!current_.View()) return true;
    const auto& state = *current_.View();
    FrameSnapshot output;
    // The immutable result and unchanged renderer baseline need conversion
    // only once. The late read and paused frames reuse that exact result;
    // any scene commit or new runtime identity invalidates the cache.
    if (options_.cache && cached_ && baseline_revision_ == scene.revision && cached_frame_ == state.frame_id &&
        cached_generation_ == state.generation && cached_instance_ == state.instance) {
      output = cached_output_;
      output.view = scene.view; output.view_revision = scene.view_revision;
      ++reused_;
    } else {
      const auto started = std::chrono::steady_clock::now();
      if (!adapter_.Apply(current_, scene, epoch_, output, error)) return false;
      apply_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
      ++converted_;
      cached_output_ = output; baseline_revision_ = scene.revision;
      cached_frame_ = state.frame_id; cached_generation_ = state.generation; cached_instance_ = state.instance;
      cached_ = true;
    }
    output.inputs = scene.inputs;
    scene = std::move(output);
    if (late) ++status_.held; else ++status_.extracted;
    return true;
  }
  void Produce() override {} // late reads never evaluate or step physics
  void Control(Action action) override {
    try {
      if (action == Action::Stop) {
        if (session_) session_->Close();
        current_.Clear(); adapter_.Clear(); cached_ = false; cached_output_ = {};
        session_.reset(); status_.playing = false; status_.stopped = true; status_.bodies = 0;
      } else {
        if (!session_) Start();
        if (action == Action::Play) { session_->Play(); status_.playing = true; }
        if (action == Action::Pause) { session_->Pause(); status_.playing = false; }
        if (action == Action::Reset) {
          session_->Reset(); current_.Clear(); ++status_.resets; Bind(); status_.playing = false;
          status_.physics_steps = status_.frame = 0; status_.evaluation_seconds = 0;
        }
      }
      status_.error.clear();
    } catch (const std::exception& e) {
      if (session_) session_->Pause();
      status_.playing = false; ++status_.refused; status_.error = e.what();
      std::cerr << "MMD physics control: " << e.what() << '\n';
    }
  }
  const AvatarRuntimeStatus& status() const noexcept override { return status_; }
  std::string Report() const override {
    std::ostringstream s;
    s << "MMD physics: steps=" << status_.physics_steps << " frame=" << status_.frame
      << " seconds=" << status_.evaluation_seconds << " generation=" << status_.generation
      << " published=" << status_.published << " refused=" << status_.refused << " resets=" << status_.resets
      << " skins=" << status_.skins << " morphs=" << status_.morphs << " uv_meshes=" << status_.uvs
      << " mismatches=" << status_.mismatches.size() << '\n';
    s << "MMD physics timing: runtime_ms_per_tick=" << (measured_ticks_ ? tick_ms_ / measured_ticks_ : 0)
      << " resident_ms_per_result=" << (converted_ ? apply_ms_ / converted_ : 0)
      << " converted=" << converted_ << " reused=" << reused_ << '\n';
    for (const auto& [subject, reason] : status_.mismatches) s << "MMD physics: mismatch " << subject << ": " << reason << '\n';
    return s.str();
  }
private:
  void Start() {
    session_ = std::make_unique<avatarMmdSession::Session>(stage_, options_.model.empty() ? stage_->GetRootLayer()->GetRealPath() : options_.model,
        options_.motion, false, false, false);
    status_.stopped = false;
    status_.playing = false;
    status_.physics_steps = status_.frame = 0;
    status_.evaluation_seconds = 0;
    Bind();
  }
  void Bind() {
    cached_ = false; cached_output_ = {};
    const auto& layout = session_->Baseline();
    auto match = HdToonMatchAvatarTargets(layout, delegate_->DescribeResidentTargets(), HdToonCanonicalMaterialInputs());
    status_.mismatches.clear();
    for (const auto& mismatch : match.mismatches)
      status_.mismatches.emplace_back(mismatch.subject, mismatch.reason);
    if (!match.mismatches.empty() || match.bindings.skins.empty()) {
      std::string reason = "MMD runtime output did not completely match resident targets";
      for (const auto& mismatch : match.mismatches) reason += "; " + mismatch.subject + ": " + mismatch.reason;
      throw std::runtime_error(reason);
    }
    status_.skins = match.bindings.skins.size(); status_.morphs = match.bindings.morphs.size();
    status_.uvs = match.bindings.uvs.size();
    std::string error;
    if (!adapter_.Bind(layout, baseline_, ++epoch_, std::move(match.bindings), error)) throw std::runtime_error(error);
    status_.instance = layout.instance; status_.generation = layout.generation;
    status_.binding_epoch = epoch_; status_.layout = layout.layout_id; status_.layout_version = layout.layout_version;
    status_.joints = layout.joint_count; status_.shapes = layout.blend_shape_count;
    status_.bodies = session_->GetStats().bodies;
    status_.diagnostics.clear();
    for (const auto& warning : session_->UnappliedMetadata()) status_.diagnostics.emplace_back(warning, 1);
  }
  UsdStageRefPtr stage_;
  HdToonRenderDelegate* delegate_;
  FrameSnapshot baseline_;
  std::uint64_t epoch_;
  AvatarRuntimeOptions options_;
  // Retained output dies before the supplying session/runtime library.
  std::unique_ptr<avatarMmdSession::Session> session_;
  RetainedAvatarSnapshot current_;
  AvatarStateAdapter adapter_;
  AvatarRuntimeStatus status_;
  bool cached_ = false;
  FrameSnapshot cached_output_;
  std::uint64_t baseline_revision_ = 0, cached_frame_ = 0, cached_generation_ = 0, cached_instance_ = 0;
  std::uint64_t measured_ticks_ = 0, converted_ = 0, reused_ = 0;
  double tick_ms_ = 0, apply_ms_ = 0;
};
}
std::unique_ptr<AvatarRuntime> AvatarRuntime::Start(const HydraScene& scene,
    const FrameSnapshot& bound, std::uint64_t epoch, const AvatarRuntimeOptions& options) {
  return std::make_unique<MmdHost>(scene, bound, epoch, options);
}
} // namespace Toon::viewport
