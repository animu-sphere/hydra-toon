// SPDX-License-Identifier: Apache-2.0
#include "avatar_runtime.hpp"
#include "hydra_scene.hpp"
#include "hydra_scene_parts.hpp"
#include "avatar_binding.hpp"
#include <avatarVrmSession/Session.h>
#include <avatarVrmUsd/ExpressionBinding.h>
#include <avatarVrmUsd/LookAtBinding.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace Toon::viewport {
namespace {
using namespace PXR_NS;
// The host's input identity; the owner maps the channels to expressions.
constexpr const char* kSource = "toon.viewport";
constexpr const char* kActor = "avatar";
constexpr const char* kGaze = "gaze:point";
// The head's runtime-world position, composed from the baseline's joints.
GfVec3d HeadPosition(const ArStateView& state, const std::string& skeleton, const std::string& head) {
  std::vector<GfMatrix4d> world;
  for (std::uint32_t i = 0; i < state.joint_count; ++i) {
    const auto& joint = state.joints[i];
    GfMatrix4d scale, rotation;
    scale.SetScale(GfVec3d(joint.local.scale[0], joint.local.scale[1], joint.local.scale[2]));
    rotation.SetRotate(GfQuatd(joint.local.rotation[3],
        GfVec3d(joint.local.rotation[0], joint.local.rotation[1], joint.local.rotation[2])));
    GfMatrix4d local = scale * rotation;
    local.SetTranslateOnly(GfVec3d(joint.local.translation[0], joint.local.translation[1], joint.local.translation[2]));
    if (joint.parent_index >= 0) local = local * world[static_cast<std::size_t>(joint.parent_index)];
    world.push_back(local);
    if (skeleton == joint.skeleton_id && head == joint.joint_id) return local.ExtractTranslation();
  }
  throw std::runtime_error("the avatar's head joint is not in its layout");
}
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
      Advance(elapsed);
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
      << " face=" << options_.face << " expressions=" << status_.expressions
      << " morphs=" << status_.morphs << " materials=" << status_.materials
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
    // Selecting the face adds morph and material outputs: a different layout.
    avatarVrmSession::SessionConfig config{{root.GetPath(), {}, "toon.viewport.vrm", options_.face ? 2u : 1u},
        {status_.clip_start, 1.0 / 60, 4}, options_.secondary_motion};
    if (options_.face) {
      const avatarVrmUsd::ExpressionBinding expressions(stage_, {config.avatar, {}});
      const avatarVrmUsd::LookAtBinding look(stage_, {config.avatar, {}});
      config.expressions = config.lookAt = true;
      config.gaze = {kSource, kActor, kGaze};
      channels_.clear();
      for (const auto& expression : expressions.Rig().GetExpressions()) {
        channels_.push_back("expr:" + expression.name);
        config.expressionInputs.push_back({{kSource, kActor, channels_.back()}, expression.name});
      }
      const auto gaze = look.AdapterConfig("toon.viewport.vrm", config.gaze, {});
      head_ = HeadPosition(expressions.Baseline(), gaze.headSkeleton, gaze.headJoint);
      status_.look_at = look.LookAtId();
      status_.expressions = channels_.size();
    }
    session_ = std::make_unique<avatarVrmSession::Session>(stage_, clip_, config);
    status_.stopped = status_.playing = false;
    Advance(0); Adopt(); Bind();
  }
  void Reset() {
    Check(session_->Reset(status_.clip_start));
    Advance(0); Adopt(); Bind();
    status_.playing = false; ++status_.resets;
  }
  // Without the face, nothing is selected. With it, a generated rolling wave of
  // expression weights and a gaze point circling in front of the head. They follow
  // the runtime's clock, so a reset replays them exactly, and are held across a
  // catch-up request. The session resolves them; this host interprets nothing.
  void Advance(double elapsed) {
    if (!options_.face) { Check(session_->Advance(elapsed, &sink_)); return; }
    const auto& stats = session_->Stats();
    const double seconds = stats.simulationSeconds, t = seconds - status_.clip_start;
    std::vector<ArScalarInput> scalars;
    const double count = static_cast<double>(std::max<std::size_t>(channels_.size(), 1));
    std::size_t index = 0;
    for (const auto& channel : channels_) {
      ArScalarInput scalar{};
      scalar.source_id = kSource; scalar.actor_id = kActor; scalar.channel_id = channel.c_str();
      scalar.value = std::max(0.0, std::sin(2.0 * std::numbers::pi * (0.5 * t + static_cast<double>(index++) / count)));
      scalar.source_seconds = seconds; scalar.clock_scale = 1;
      scalars.push_back(scalar);
    }
    const double angle = 0.5 * std::numbers::pi * t;
    const GfVec3d point = head_ + GfVec3d(0.6 * std::cos(angle), 0.3 * std::sin(angle), 1.0);
    ArGazeInput gaze{};
    gaze.source_id = kSource; gaze.actor_id = kActor; gaze.channel_id = kGaze;
    gaze.kind = AR_GAZE_POINT; gaze.space = AR_GAZE_RUNTIME_WORLD; gaze.validity = AR_OBSERVATION_VALID;
    for (int k = 0; k < 3; ++k) gaze.value[k] = point[k];
    gaze.source_seconds = seconds; gaze.clock_scale = 1;
    ArInputFrame input{AR_HEADER(ArInputFrame)};
    input.scalars = scalars.data(); input.scalar_count = static_cast<std::uint32_t>(scalars.size());
    input.gazes = &gaze; input.gaze_count = 1;
    input.input_revision = stats.tick + 1;
    Check(session_->Advance(elapsed, input, &sink_));
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
    status_.shapes = state.blend_shape_count; status_.material_inputs = state.material_count;
    status_.morphs = match.bindings.morphs.size(); status_.materials = match.bindings.materials.size();
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
  std::deque<std::string> channels_; // stable c_str() for the inputs
  GfVec3d head_{0.0};
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
