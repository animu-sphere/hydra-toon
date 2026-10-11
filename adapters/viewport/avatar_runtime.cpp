// SPDX-License-Identifier: Apache-2.0
#include "avatar_runtime.hpp"
#include "hydra_scene.hpp"
#include "hydra_scene_parts.hpp"

#include "avatar_binding.hpp"

#include <avatarVrm/ExpressionAdapter.h>
#include <avatarVrmUsd/ExpressionBinding.h>
#include <avatarVrmUsd/LookAtBinding.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/usd/usd/prim.h>

#include <toon/vulkan_backend.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace Toon::viewport {
namespace {

using namespace PXR_NS;

// The host's input identity; the owner maps the channels to expressions.
constexpr const char* kSource = "toon.viewport";
constexpr const char* kActor = "avatar";
constexpr const char* kGaze = "gaze:point";
constexpr const char* kEvaluator = "toon.viewport.vrm";

std::string Channel(const std::string& expression) {
  return "expr:" + expression;
}

double Seconds(std::int64_t nanoseconds) {
  return static_cast<double>(nanoseconds) * 1e-9;
}

std::string Format(const double* value, int count) {
  std::ostringstream text;
  text << std::fixed << std::setprecision(3);
  for (int k = 0; k < count; ++k) text << (k ? " " : "") << value[k];
  return text.str();
}

// Owner diagnostics, counted by origin and code. The producer's thread
// emits them; the frame loop reads the counts.
struct Diagnostics {
  std::mutex mutex;
  std::map<std::string, std::uint64_t> counts;
  static void AR_CALL Emit(void* user, const ArDiagnostic* diagnostic) {
    auto& self = *static_cast<Diagnostics*>(user);
    const std::string key = std::string(diagnostic->origin ? diagnostic->origin : "") +
        ":" + (diagnostic->code ? diagnostic->code : "");
    const std::lock_guard lock(self.mutex);
    ++self.counts[key];
  }
  ArDiagnosticSink Sink() { return {this, &Emit}; }
};

// A runtime joint as a matrix in metres.
GfMatrix4d Matrix(const ArTransform& t) {
  GfMatrix4d scale;
  scale.SetScale(GfVec3d(t.scale[0], t.scale[1], t.scale[2]));
  GfMatrix4d rotation;
  rotation.SetRotate(GfQuatd(t.rotation[3],
      GfVec3d(t.rotation[0], t.rotation[1], t.rotation[2])));
  GfMatrix4d matrix = scale * rotation;
  matrix.SetTranslateOnly(GfVec3d(t.translation[0], t.translation[1], t.translation[2]));
  return matrix;
}

// The head's runtime-world position, composed from the baseline's joints.
std::optional<GfVec3d> HeadPosition(const ArStateView& state,
    const std::string& skeleton, const std::string& head) {
  std::vector<GfMatrix4d> world;
  for (std::uint32_t i = 0; i < state.joint_count; ++i) {
    const ArJoint& joint = state.joints[i];
    GfMatrix4d local = Matrix(joint.local);
    if (joint.parent_index >= 0) local = local * world[static_cast<std::size_t>(joint.parent_index)];
    world.push_back(local);
    if (skeleton == joint.skeleton_id && head == joint.joint_id) {
      return world.back().ExtractTranslation();
    }
  }
  return std::nullopt;
}

const char* FieldName(Toon::AvatarMaterialField field) {
  using F = Toon::AvatarMaterialField;
  switch (field) {
  case F::BaseColor: return "base colour";
  case F::Emissive: return "emissive";
  case F::ShadeColor: return "shade colour";
  case F::OutlineColor: return "outline colour";
  case F::Matcap: return "matcap";
  case F::RimColor: return "rim colour";
  case F::Alpha: return "alpha";
  case F::ShadingShift: return "shading shift";
  case F::ShadingToony: return "shading toony";
  case F::OutlineWidth: return "outline width";
  case F::BaseTextureOffset: return "base texture offset";
  case F::BaseTextureScale: return "base texture scale";
  case F::BaseTextureRotation: return "base texture rotation";
  case F::BaseColorRgb: return "base colour rgb";
  }
  return "?";
}

// Destroyed after every snapshot and before the provider it borrows.
struct Runtime {
  ArRuntimeApi api{};
  ArRuntime handle = 0;
  ArInstance instance = 0;
  Runtime() = default;
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  ~Runtime() {
    if (handle) api.destroy_runtime(handle);
  }
};

class RuntimeHost final : public AvatarRuntime {
public:
  RuntimeHost(const HydraScene& scene, const FrameSnapshot& bound,
      std::uint64_t epoch, const AvatarRuntimeOptions& options)
      : options_(options), epoch_(epoch), origin_(Toon::SteadyNanoseconds()),
        stage_(scene.parts().stage) {
    const UsdPrim root = stage_->GetDefaultPrim();
    if (!root) throw std::runtime_error("the avatar runtime needs a default prim");
    const avatarVrmUsd::HumanoidBindingConfig humanoid{root.GetPath(), {}, "toon.viewport", 1};
    expressions_ = std::make_unique<avatarVrmUsd::ExpressionBinding>(stage_,
        avatarVrmUsd::ExpressionBindingConfig{humanoid, {}});
    look_ = std::make_unique<avatarVrmUsd::LookAtBinding>(stage_,
        avatarVrmUsd::LookAtBindingConfig{humanoid, SdfPath(options.look_at)});
    avatarVrm::ExpressionAdapterConfig config =
        look_->AdapterConfig(kEvaluator, {kSource, kActor, kGaze});
    expressions_->ApplyTo(config);
    for (const auto& expression : expressions_->Rig().GetExpressions()) {
      names_.push_back(expression.name);
      channels_.push_back(Channel(expression.name));
      config.inputs.push_back({{kSource, kActor, channels_.back()}, expression.name});
    }
    provider_ = std::make_unique<avatarVrm::ExpressionAdapter>(config);
    const ArStateView& baseline = expressions_->Baseline();
    const auto head = HeadPosition(baseline, config.headSkeleton, config.headJoint);
    if (!head) throw std::runtime_error("the provider's head joint is not in its layout");
    head_ = *head;

    ArDiagnosticSink sink = diagnostics_.Sink();
    const ArEvaluatorDesc descriptor = provider_->Descriptor();
    if (arGetApi(AR_ABI_VERSION, sizeof(runtime_.api), &runtime_.api) != AR_OK ||
        runtime_.api.create_runtime(&runtime_.handle) != AR_OK ||
        runtime_.api.register_evaluator(runtime_.handle, &descriptor, &sink) != AR_OK) {
      throw std::runtime_error("cannot register the VRM provider in the runtime");
    }
    ArInstanceDesc instance{AR_HEADER(ArInstanceDesc)};
    instance.generation = generation_;
    instance.initial_state = baseline;
    instance.layout_id = baseline.layout_id;
    instance.layout_version = baseline.layout_version;
    const char* evaluators[] = {descriptor.id};
    instance.evaluators = evaluators;
    instance.evaluator_count = 1;
    instance.bound_capabilities = descriptor.supplies;
    instance.bound_capability_count = descriptor.supply_count;
    if (runtime_.api.create_instance(runtime_.handle, &instance, &sink, &runtime_.instance) != AR_OK) {
      throw std::runtime_error("cannot create the runtime instance");
    }

    // The first result's identities, matched to the resident description.
    Evaluate();
    Take();
    if (!current_) throw std::runtime_error("the runtime published no first result");
    const ArStateView& view = *current_->snapshot.View();
    match_ = HdToonMatchAvatarTargets(view, scene.parts().delegate->DescribeResidentTargets(),
        HdToonCanonicalMaterialInputs());
    const auto& b = match_.bindings;
    if (b.skins.empty() && b.morphs.empty() && b.materials.empty() && b.visibility.empty()) {
      throw std::runtime_error("no runtime output matches a resident target");
    }
    std::string error;
    if (!adapter_.Bind(view, bound, epoch_, match_.bindings, error)) {
      throw std::runtime_error("the adapter refused the runtime binding: " + error);
    }
    status_.avatar = root.GetPath().GetString();
    status_.look_at = look_->LookAtId();
    status_.joints = view.joint_count;
    status_.shapes = view.blend_shape_count;
    status_.material_inputs = view.material_count;
    status_.expressions = names_.size();
    status_.skins = b.skins.size();
    status_.morphs = b.morphs.size();
    status_.materials = b.materials.size();
    status_.visibility = b.visibility.size();
    for (const auto& mismatch : match_.mismatches) {
      status_.mismatches.emplace_back(mismatch.subject, mismatch.reason);
    }
    if (options_.rate > 0.0) {
      thread_ = std::thread([this] { Run(); });
    }
  }

  ~RuntimeHost() override {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
  }

  RuntimeHost(const RuntimeHost&) = delete;
  RuntimeHost& operator=(const RuntimeHost&) = delete;

  bool Apply(FrameSnapshot& scene, bool late, std::string& error) override {
    Take();
    if (!current_) return true;
    if (late) {
      if (current_->sequence != extracted_sequence_) {
        ++status_.late;
        status_.publish_latch.Push(
            static_cast<double>(Toon::SteadyNanoseconds() - current_->published) * 1e-6);
        extracted_sequence_ = current_->sequence;
      } else {
        ++status_.held;
      }
    } else {
      ++status_.extracted;
      extracted_sequence_ = current_->sequence;
    }
    FrameSnapshot output;
    if (!adapter_.Apply(current_->snapshot, scene, epoch_, output, error)) return false;
    output.inputs = scene.inputs;
    output.inputs.expression = current_->expression;
    output.inputs.look_at = current_->look_at;
    output.inputs.pose = current_->pose;
    scene = std::move(output);
    if (current_->sequence != described_) {
      Describe();
      described_ = current_->sequence;
    }
    return true;
  }

  void Produce() override {
    if (options_.rate <= 0.0) Evaluate();
  }

  const AvatarRuntimeStatus& status() const noexcept override { return status_; }

  std::string Report() const override {
    std::ostringstream report;
    report << "Avatar runtime: avatar=" << status_.avatar << " look_at=" << status_.look_at
           << " source=generated results=" << status_.published
           << " extracted=" << status_.extracted << " late=" << status_.late
           << " held=" << status_.held << " refused=" << status_.refused
           << " resets=" << status_.resets << " epoch=" << status_.binding_epoch
           << " generation=" << status_.generation
           << " frame=" << status_.frame << '\n'
           << "Avatar runtime: layout joints=" << status_.joints
           << " shapes=" << status_.shapes << " material_inputs=" << status_.material_inputs
           << " expressions=" << status_.expressions << " matched skins=" << status_.skins
           << " morphs=" << status_.morphs << " materials=" << status_.materials
           << " visibility=" << status_.visibility
           << " mismatches=" << status_.mismatches.size() << '\n';
    for (const auto& [subject, reason] : status_.mismatches) {
      report << "Avatar runtime: mismatch " << subject << ": " << reason << '\n';
    }
    for (const auto& [code, count] : status_.diagnostics) {
      report << "Avatar runtime: diagnostic " << code << " x" << count << '\n';
    }
    report << std::fixed << std::setprecision(3);
    for (const auto& [name, series] : {std::pair{"sample_publish", &status_.sample_publish},
             std::pair{"publish_latch", &status_.publish_latch}}) {
      const Summary s = series->Summarize();
      if (s.count == 0) continue;
      report << "Timing: avatar " << name << " (generated source) frames=" << s.count
             << " mean=" << s.mean << " p50=" << s.p50 << " p95=" << s.p95
             << " p99=" << s.p99 << " max=" << s.max << " ms\n";
    }
    return report.str();
  }

private:
  struct Published {
    Toon::RetainedAvatarSnapshot snapshot;
    std::uint64_t sequence = 0;
    std::int64_t published = 0;
    // Newest retained sample times on the steady clock; zero when absent.
    std::int64_t expression = 0, look_at = 0, pose = 0;
    std::vector<std::pair<std::string, std::vector<double>>> inputs;
  };

  void Run() {
    const auto period = std::chrono::duration<double>(1.0 / options_.rate);
    auto next = std::chrono::steady_clock::now();
    while (!stop_) {
      Evaluate();
      next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
      const auto now = std::chrono::steady_clock::now();
      if (next < now) next = now;
      std::this_thread::sleep_until(next);
    }
  }

  // One generated input frame, evaluated and published to the mailbox. A
  // rolling wave of expression weights, several active at once, and a gaze
  // point circling in front of the head. Values follow the host clock or,
  // at a zero rate, the result count; times are always when they were made.
  void Evaluate() {
    if (options_.reset_every != 0 && results_ != 0 && results_ % options_.reset_every == 0) {
      if (runtime_.api.reset_instance(runtime_.handle, runtime_.instance, generation_ + 1) == AR_OK) {
        ++generation_;
        frame_id_ = 0;
        const std::lock_guard lock(mailbox_mutex_);
        ++resets_;
      }
    }
    const std::int64_t produced = Toon::SteadyNanoseconds();
    const double now = Seconds(produced - origin_);
    const double t = options_.rate > 0.0 ? now : static_cast<double>(results_) / 60.0;
    std::vector<ArScalarInput> scalars;
    std::vector<std::pair<std::string, std::vector<double>>> values;
    const double count = static_cast<double>(std::max<std::size_t>(names_.size(), 1));
    for (std::size_t i = 0; i < names_.size(); ++i) {
      const double weight = std::max(0.0,
          std::sin(2.0 * std::numbers::pi * (0.5 * t + static_cast<double>(i) / count)));
      ArScalarInput scalar{};
      scalar.source_id = kSource;
      scalar.actor_id = kActor;
      scalar.channel_id = channels_[i].c_str();
      scalar.value = weight;
      scalar.source_seconds = now;
      scalar.clock_scale = 1;
      scalars.push_back(scalar);
      values.push_back({channels_[i], {weight}});
    }
    const double angle = 0.5 * std::numbers::pi * t;
    const GfVec3d point = head_ + GfVec3d(0.6 * std::cos(angle), 0.3 * std::sin(angle), 1.0);
    ArGazeInput gaze{};
    gaze.source_id = kSource;
    gaze.actor_id = kActor;
    gaze.channel_id = kGaze;
    gaze.kind = AR_GAZE_POINT;
    gaze.space = AR_GAZE_RUNTIME_WORLD;
    gaze.validity = AR_OBSERVATION_VALID;
    for (int k = 0; k < 3; ++k) gaze.value[k] = point[k];
    gaze.source_seconds = now;
    gaze.clock_scale = 1;
    values.push_back({kGaze, {point[0], point[1], point[2]}});
    ArInputFrame input{AR_HEADER(ArInputFrame)};
    input.frame_id = ++frame_id_;
    input.generation = generation_;
    input.evaluation_seconds = now;
    input.scalars = scalars.data();
    input.scalar_count = static_cast<std::uint32_t>(scalars.size());
    input.input_revision = results_ + 1;
    input.gazes = &gaze;
    input.gaze_count = 1;
    ArDiagnosticSink sink = diagnostics_.Sink();
    ArSnapshot handle = 0;
    Published result;
    std::string error;
    const bool evaluated =
        runtime_.api.evaluate_frame(runtime_.handle, runtime_.instance, &input, &sink, &handle) == AR_OK;
    const bool retained = evaluated && result.snapshot.Reset(runtime_.api, handle, error);
    if (handle) runtime_.api.release_snapshot(handle);
    if (!retained) {
      --frame_id_;
      const std::lock_guard lock(mailbox_mutex_);
      ++refused_;
      return;
    }
    ++results_;
    result.published = Toon::SteadyNanoseconds();
    result.inputs = std::move(values);
    const ArStateView& view = *result.snapshot.View();
    for (std::uint32_t i = 0; i < view.sample_count; ++i) {
      const ArSourceSample& sample = view.samples[i];
      // Runtime seconds are the host's steady clock from origin_: every
      // input here maps with scale 1 and offset 0.
      const auto time = origin_ +
          static_cast<std::int64_t>(std::llround(sample.runtime_seconds * 1e9));
      std::int64_t* slot = sample.kind == AR_SOURCE_SCALAR ? &result.expression
          : sample.kind == AR_SOURCE_GAZE                  ? &result.look_at
          : sample.kind == AR_SOURCE_POSE                  ? &result.pose
                                                           : nullptr;
      if (slot) *slot = std::max(*slot, time);
    }
    std::optional<Published> replaced;
    {
      const std::lock_guard lock(mailbox_mutex_);
      result.sequence = ++published_;
      replaced = std::exchange(mailbox_, std::move(result));
    }
    // An untaken result is released outside the lock.
  }

  // Moves the newest published result, if any, into current_.
  bool Take() {
    std::optional<Published> taken;
    {
      const std::lock_guard lock(mailbox_mutex_);
      taken = std::exchange(mailbox_, std::nullopt);
      status_.published = published_;
      status_.refused = refused_;
      status_.resets = resets_;
    }
    if (!taken) return false;
    const std::int64_t newest = std::max(taken->expression, taken->look_at);
    if (newest != 0) {
      status_.sample_publish.Push(static_cast<double>(taken->published - newest) * 1e-6);
    }
    current_ = std::move(taken);
    return true;
  }

  // The applied result's identity, inputs and bound outputs for the panel.
  void Describe() {
    const ArStateView& view = *current_->snapshot.View();
    const auto& identity = adapter_.IdentityInfo();
    status_.instance = identity.instance;
    status_.frame = identity.frame;
    status_.generation = identity.generation;
    status_.input_revision = identity.input_revision;
    status_.layout = identity.layout;
    status_.layout_version = identity.layout_version;
    status_.binding_epoch = identity.binding_epoch;
    status_.evaluation_seconds = view.evaluation_seconds;
    {
      const std::lock_guard lock(diagnostics_.mutex);
      status_.diagnostics.assign(diagnostics_.counts.begin(), diagnostics_.counts.end());
    }
    status_.inputs.clear();
    for (const auto& [channel, value] : current_->inputs) {
      const bool expression = channel.rfind("expr:", 0) == 0;
      status_.inputs.push_back({channel, expression ? channel.substr(5) : status_.look_at,
          Format(value.data(), static_cast<int>(value.size()))});
    }
    status_.morph_rows.clear();
    for (const auto& morph : match_.bindings.morphs) {
      const ArBlendShape& shape = view.blend_shapes[morph.source];
      std::ostringstream target;
      target << "mesh " << morph.mesh << " slot " << morph.weight;
      if (!morph.inbetweens.empty()) target << " +" << morph.inbetweens.size() << " inbetween";
      status_.morph_rows.push_back({std::string(shape.mesh_id) + " " + shape.target_id,
          target.str(), Format(&shape.weight, 1)});
    }
    status_.material_rows.clear();
    for (const auto& material : match_.bindings.materials) {
      const ArMaterialInput& input = view.materials[material.source];
      const int count = input.value_type == AR_VALUE_SCALAR ? 1
          : input.value_type == AR_VALUE_VEC2               ? 2
          : input.value_type == AR_VALUE_VEC3               ? 3
                                                            : 4;
      std::ostringstream target;
      target << "material " << material.material << ' ' << FieldName(material.field);
      if (material.scale != 1.0F) target << " x" << material.scale;
      status_.material_rows.push_back({std::string(input.material_id) + " " + input.input_id,
          target.str(), Format(input.value, count)});
    }
  }

  AvatarRuntimeOptions options_;
  std::uint64_t epoch_ = 0;
  std::int64_t origin_ = 0;
  UsdStageRefPtr stage_;
  std::unique_ptr<avatarVrmUsd::ExpressionBinding> expressions_;
  std::unique_ptr<avatarVrmUsd::LookAtBinding> look_;
  // Registration borrows the provider until the runtime is destroyed.
  std::unique_ptr<avatarVrm::ExpressionAdapter> provider_;
  std::vector<std::string> names_;
  std::deque<std::string> channels_; // stable c_str() for the inputs
  GfVec3d head_{0.0};
  Diagnostics diagnostics_;
  Runtime runtime_;

  // The producer's: the frame loop with a zero rate, else the thread.
  std::uint64_t generation_ = 1, frame_id_ = 0, results_ = 0;
  std::thread thread_;
  std::atomic<bool> stop_{false};

  std::mutex mailbox_mutex_;
  std::optional<Published> mailbox_;
  std::uint64_t published_ = 0, refused_ = 0, resets_ = 0;

  // The frame loop's.
  Toon::AvatarStateAdapter adapter_;
  HdToonAvatarTargetMatch match_;
  std::optional<Published> current_;
  std::uint64_t extracted_sequence_ = 0, described_ = 0;
  AvatarRuntimeStatus status_;
};

} // namespace

std::unique_ptr<AvatarRuntime> AvatarRuntime::Start(const HydraScene& scene,
    const FrameSnapshot& bound, std::uint64_t epoch, const AvatarRuntimeOptions& options) {
  return std::make_unique<RuntimeHost>(scene, bound, epoch, options);
}

} // namespace Toon::viewport
