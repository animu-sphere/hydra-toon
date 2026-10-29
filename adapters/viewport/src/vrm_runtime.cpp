// SPDX-License-Identifier: Apache-2.0
#include "avatar_runtime.hpp"
#include "hydra_scene.hpp"
#include "hydra_scene_parts.hpp"
#include "avatar_binding.hpp"
#include <avatarVrmSession/Session.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace Toon::viewport {
namespace {
using namespace PXR_NS;
class VrmHost final : public AvatarRuntime {
public:
  VrmHost(const HydraScene& scene, const FrameSnapshot& bound, std::uint64_t epoch,
      const AvatarRuntimeOptions& options)
      : stage_(scene.parts().stage), delegate_(scene.parts().delegate), baseline_(bound),
        epoch_(epoch), options_(options) {
    status_.physics = status_.vrm_session = true;
    status_.secondary_motion = options.secondary_motion;
    status_.motion = options.motion;
    auto clipStage = UsdStage::Open(options.motion);
    if (!clipStage) throw std::runtime_error("cannot open VRM motion " + options.motion);
    SdfPath skeleton;
    for (const auto& prim : clipStage->Traverse()) if (prim.IsA<UsdSkelSkeleton>()) {
      if (!skeleton.IsEmpty()) throw std::runtime_error("VRM motion must contain exactly one skeleton");
      skeleton = prim.GetPath();
    }
    if (skeleton.IsEmpty()) throw std::runtime_error("VRM motion contains no skeleton");
    clip_ = avatarMotionUsd::ReadStageClip(clipStage, skeleton);
    if (clip_.clip.samples.empty()) throw std::runtime_error("VRM motion clip is empty");
    status_.clip_start = clip_.clip.samples.front().timestamp;
    status_.clip_end = clip_.clip.samples.back().timestamp;
    Start();
    if (options.play) Control(Action::Play);
  }
  ~VrmHost() override { current_.Clear(); if (session_) session_->Close(); }
  void Tick(double elapsed) override {
    if (!session_ || !status_.playing) return;
    try {
      // Loop wrap explicitly resets the runtime clock and solver history.
      // No host accumulator, negative step or evaluation in a late read.
      if (options_.loop && status_.clip_end > status_.clip_start &&
          session_->Stats().simulationSeconds >= status_.clip_end) {
        Reset(); session_->Play(); status_.playing = true;
      }
      Check(session_->Advance(elapsed, &sink_));
      Adopt();
      if (options_.reset_every && status_.published % options_.reset_every == 0) {
        Reset(); session_->Play(); status_.playing = true;
      }
    } catch (const std::exception& e) { Refuse(e); }
  }
  bool Apply(FrameSnapshot& scene, bool late, std::string& error) override {
    if (!current_.View()) return true;
    const auto& state = *current_.View();
    FrameSnapshot output;
    if (options_.cache && cached_ && baseline_revision_ == scene.revision &&
        cached_frame_ == state.frame_id && cached_generation_ == state.generation && cached_instance_ == state.instance) {
      output = cached_output_; output.view = scene.view; output.view_revision = scene.view_revision; ++reused_;
    } else {
      if (!adapter_.Apply(current_, scene, epoch_, output, error)) return false;
      cached_output_ = output; baseline_revision_ = scene.revision;
      cached_frame_ = state.frame_id; cached_generation_ = state.generation; cached_instance_ = state.instance;
      cached_ = true; ++converted_;
    }
    output.inputs = scene.inputs; scene = std::move(output);
    if (late) ++status_.held; else ++status_.extracted;
    return true;
  }
  void Produce() override {} // delivery retries never sample motion or step springs
  void Control(Action action) override {
    try {
      if (action == Action::Stop) {
        if (session_) session_->Close();
        current_.Clear(); adapter_.Clear(); cached_ = false; cached_output_ = {};
        session_.reset(); status_.playing = false; status_.stopped = true;
      } else {
        if (!session_) Start();
        if (action == Action::Play) { session_->Play(); status_.playing = true; }
        if (action == Action::Pause) { session_->Pause(); status_.playing = false; }
        if (action == Action::Reset) Reset();
      }
      status_.error.clear();
    } catch (const std::exception& e) { Refuse(e); }
  }
  const AvatarRuntimeStatus& status() const noexcept override { return status_; }
  std::string Report() const override {
    std::ostringstream s;
    s << "VRM session: ticks=" << status_.physics_steps << " frame=" << status_.frame
      << " seconds=" << status_.evaluation_seconds << " pending=" << status_.pending_seconds
      << " generation=" << status_.generation << " secondary=" << status_.secondary_motion
      << " published=" << status_.published << " refused=" << status_.refused << " resets=" << status_.resets
      << " skins=" << status_.skins << " joints=" << status_.joints
      << " mismatches=" << status_.mismatches.size() << " converted=" << converted_ << " reused=" << reused_ << '\n';
    for (const auto& d : status_.diagnostics) s << "VRM session: " << d.first << '\n';
    return s.str();
  }
private:
  static void AR_CALL Emit(void* user, const ArDiagnostic* d) {
    auto& self = *static_cast<VrmHost*>(user);
    if (d->severity == AR_SEVERITY_ERROR) {
      const auto detail = std::string(d->origin) + ':' + d->code + ": " + d->message;
      self.status_.diagnostics.emplace_back(detail, 1); self.status_.error = detail;
    }
  }
  void Check(ArStatus code) {
    if (code != AR_OK) throw std::runtime_error(status_.error.empty() ? "VRM runtime status " + std::to_string(code) : status_.error);
  }
  void Refuse(const std::exception& e) {
    if (session_) session_->Pause();
    status_.playing = false; ++status_.refused; status_.error = e.what();
    std::cerr << "VRM session paused: " << e.what() << '\n';
  }
  void Start() {
    const auto root = stage_->GetDefaultPrim();
    if (!root) throw std::runtime_error("VRM avatar needs a default prim");
    status_.avatar = root.GetPath().GetString();
    session_ = std::make_unique<avatarVrmSession::Session>(stage_, clip_, avatarVrmSession::SessionConfig{
        {root.GetPath(), {}, "toon.viewport.vrm", 1}, {status_.clip_start, 1.0 / 60, 4}, options_.secondary_motion});
    status_.stopped = status_.playing = false;
    Check(session_->Advance(0, &sink_)); Adopt(); Bind();
  }
  void Reset() {
    Check(session_->Reset(status_.clip_start));
    Check(session_->Advance(0, &sink_)); Adopt(); Bind();
    status_.playing = false; ++status_.resets;
  }
  void Adopt() {
    const auto handle = session_->Snapshot();
    if (!handle) return;
    std::string error;
    if (!current_.Reset(session_->Api(), handle, error)) throw std::runtime_error(error);
    const auto& v = *current_.View();
    if (v.frame_id != status_.frame || v.generation != status_.generation || v.instance != status_.instance) ++status_.published;
    status_.instance = v.instance; status_.generation = v.generation; status_.frame = v.frame_id;
    status_.evaluation_seconds = v.evaluation_seconds; status_.input_revision = v.input_revision;
    status_.physics_steps = session_->Stats().tick; status_.pending_seconds = session_->Stats().pendingSeconds;
  }
  void Bind() {
    cached_ = false; cached_output_ = {};
    const auto& state = *current_.View();
    auto match = HdToonMatchAvatarTargets(state, delegate_->DescribeResidentTargets(), HdToonCanonicalMaterialInputs());
    status_.mismatches.clear();
    for (const auto& m : match.mismatches) status_.mismatches.emplace_back(m.subject, m.reason);
    if (!match.mismatches.empty() || match.bindings.skins.empty())
      throw std::runtime_error("VRM output did not completely match resident skin targets");
    status_.skins = match.bindings.skins.size(); status_.joints = state.joint_count;
    std::string error;
    if (!adapter_.Bind(state, baseline_, ++epoch_, std::move(match.bindings), error)) throw std::runtime_error(error);
    status_.binding_epoch = epoch_; status_.layout = state.layout_id; status_.layout_version = state.layout_version;
  }
  UsdStageRefPtr stage_;
  HdToonRenderDelegate* delegate_;
  FrameSnapshot baseline_;
  std::uint64_t epoch_;
  AvatarRuntimeOptions options_;
  openstrata::motion::MotionStageRead clip_;
  std::unique_ptr<avatarVrmSession::Session> session_;
  RetainedAvatarSnapshot current_;
  AvatarStateAdapter adapter_;
  AvatarRuntimeStatus status_;
  ArDiagnosticSink sink_{this, Emit};
  bool cached_ = false;
  FrameSnapshot cached_output_;
  std::uint64_t baseline_revision_ = 0, cached_frame_ = 0, cached_generation_ = 0, cached_instance_ = 0;
  std::uint64_t converted_ = 0, reused_ = 0;
};
}
std::unique_ptr<AvatarRuntime> AvatarRuntime::Start(const HydraScene& scene,
    const FrameSnapshot& bound, std::uint64_t epoch, const AvatarRuntimeOptions& options) {
  return std::make_unique<VrmHost>(scene, bound, epoch, options);
}
} // namespace Toon::viewport
