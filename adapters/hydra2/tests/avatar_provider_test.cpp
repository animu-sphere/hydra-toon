// SPDX-License-Identifier: Apache-2.0
//
// A renderer-side host over real usd-avatar-runtime providers (roadmap
// v0.3.0). The runtime's VRM USD bindings read the avatar, and its owner
// LookAt/Expression evaluator, registered in the runtime, produces every
// result from host scalar and gaze input. The host matches each result's
// identities to the delegate's resident description and binds the fast
// adapter with them; every runtime output must bind.
//
// Each result then reaches the scene twice: through the fast adapter over
// the bound Hydra scene, and authored into the session layer for UsdImaging
// and usdSkelImaging to compose. Authoring is this check's oracle only; no
// renderer path authors USD per frame. Both must give the same palettes,
// subshape weights and normalized materials, while the fast result keeps
// every static revision of the bound scene. With --shaders, both are also
// drawn on the same Vulkan configuration and their colour and depth must
// agree within the tolerances `Images` declares, while the fast renderer
// uploads no static resource after its first frame.
#include "avatar_binding.hpp"

#include <avatarVrm/ExpressionAdapter.h>
#include <avatarVrmUsd/ExpressionBinding.h>
#include <avatarVrmUsd/LookAtBinding.h>

#include <pxr/pxr.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/range3d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/setenv.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <toon/vulkan_backend.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

bool Check(bool condition, const std::string& message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

struct Options {
  std::string stage;
  SdfPath look_at;
  std::string expect_gaze; // "bone" or "expression"
  bool expect_materials = false;
  bool expect_images = false; // fast images must change with the results
  std::string shaders;
};

// UsdImaging's scene indices over the stage, synced into the delegate as
// toon-viewport syncs them.
class Host {
public:
  explicit Host(UsdStageRefPtr stage)
      : stage_(std::move(stage)), time_(stage_->GetStartTimeCode()) {
    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage_;
    indices_ = UsdImagingCreateSceneIndices(info);
    index_.reset(HdRenderIndex::New(&delegate_, {}));
    index_->InsertSceneIndex(indices_.finalSceneIndex, SdfPath::AbsoluteRootPath());
  }
  ~Host() { index_.reset(); }
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  Toon::FrameSnapshot Sync() {
    static const HdRprimCollection collection(HdTokens->geometry,
        HdReprSelector(HdReprTokens->smoothHull));
    indices_.stageSceneIndex->ApplyPendingUpdates();
    indices_.stageSceneIndex->SetTime(time_);
    index_->EnqueueCollectionToSync(collection);
    index_->SyncAll(&tasks_, &context_);
    // Hydra carries no stage unit; a host states it, as toon-viewport does,
    // and the fast adapter converts runtime metres with it.
    Toon::FrameSnapshot snapshot = delegate_.CommitScene();
    snapshot.meters_per_unit =
        static_cast<float>(UsdGeomGetStageMetersPerUnit(stage_));
    return snapshot;
  }
  [[nodiscard]] HdToonResidentTargets Describe() const {
    return delegate_.DescribeResidentTargets();
  }
  [[nodiscard]] UsdTimeCode Time() const { return time_; }

private:
  UsdStageRefPtr stage_;
  UsdTimeCode time_;
  UsdImagingSceneIndices indices_;
  HdToonRenderDelegate delegate_;
  std::unique_ptr<HdRenderIndex> index_;
  HdTaskSharedPtrVector tasks_;
  HdTaskContext context_;
};

// Provider diagnostics, counted by code; warnings are expected from real
// avatars and reported, not judged.
struct Diagnostics {
  std::map<std::string, std::size_t> counts;
  static void AR_CALL Emit(void* user, const ArDiagnostic* diagnostic) {
    auto& self = *static_cast<Diagnostics*>(user);
    const std::string code = diagnostic->code ? diagnostic->code : "";
    if (self.counts[code]++ == 0) {
      std::cout << "  diagnostic " << code << " ("
                << (diagnostic->subject ? diagnostic->subject : "") << "): "
                << (diagnostic->message ? diagnostic->message : "") << '\n';
    }
  }
  ArDiagnosticSink Sink() { return {this, &Emit}; }
};

struct Runtime {
  ArRuntimeApi api{};
  ArRuntime runtime = 0;
  ArInstance instance = 0;
  Runtime() = default;
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  ~Runtime() {
    if (runtime) api.destroy_runtime(runtime);
  }
};

// Host input for one frame: scalar expression channels and a world gaze
// point in metres. Channel names are the host's; the owner maps them.
struct Frame {
  std::string label;
  std::vector<std::pair<std::string, double>> weights;
  std::optional<GfVec3d> gaze;
  bool reset = false;
};

constexpr const char* kSource = "host";
constexpr const char* kActor = "avatar";
constexpr const char* kGaze = "gaze:point";

std::string Channel(const std::string& expression) {
  return "expr:" + expression;
}

ArSnapshot Evaluate(Runtime& runtime, Diagnostics& diagnostics,
    std::uint64_t frame_id, std::uint64_t generation, const Frame& frame) {
  std::deque<std::string> channels;
  std::vector<ArScalarInput> scalars;
  for (const auto& [name, value] : frame.weights) {
    ArScalarInput scalar{};
    scalar.source_id = kSource;
    scalar.actor_id = kActor;
    scalar.channel_id = channels.emplace_back(Channel(name)).c_str();
    scalar.value = value;
    scalar.clock_scale = 1;
    scalars.push_back(scalar);
  }
  ArGazeInput gaze{};
  gaze.source_id = kSource;
  gaze.actor_id = kActor;
  gaze.channel_id = kGaze;
  gaze.kind = AR_GAZE_POINT;
  gaze.space = AR_GAZE_RUNTIME_WORLD;
  gaze.validity = AR_OBSERVATION_VALID;
  gaze.clock_scale = 1;
  if (frame.gaze) {
    for (int k = 0; k < 3; ++k) gaze.value[k] = (*frame.gaze)[k];
  }
  ArInputFrame input{AR_HEADER(ArInputFrame)};
  input.frame_id = frame_id;
  input.generation = generation;
  input.scalars = scalars.data();
  input.scalar_count = static_cast<std::uint32_t>(scalars.size());
  input.input_revision = frame_id;
  input.gazes = frame.gaze ? &gaze : nullptr;
  input.gaze_count = frame.gaze ? 1 : 0;
  ArDiagnosticSink sink = diagnostics.Sink();
  ArSnapshot snapshot = 0;
  if (runtime.api.evaluate_frame(runtime.runtime, runtime.instance, &input,
          &sink, &snapshot) != AR_OK) {
    return 0;
  }
  return snapshot;
}

// A runtime joint as a USD matrix in stage units.
GfMatrix4d Matrix(const ArTransform& t, double meters_per_unit) {
  GfMatrix4d scale;
  scale.SetScale(GfVec3d(t.scale[0], t.scale[1], t.scale[2]));
  GfMatrix4d rotation;
  rotation.SetRotate(GfQuatd(t.rotation[3],
      GfVec3d(t.rotation[0], t.rotation[1], t.rotation[2])));
  GfMatrix4d matrix = scale * rotation;
  matrix.SetTranslateOnly(GfVec3d(t.translation[0], t.translation[1],
                              t.translation[2]) /
      meters_per_unit);
  return matrix;
}

bool NearMatrix(const GfMatrix4d& a, const GfMatrix4d& b, double tolerance) {
  for (int i = 0; i < 16; ++i) {
    const double x = a.data()[i];
    const double y = b.data()[i];
    if (!(std::abs(x - y) <= tolerance * std::max({1.0, std::abs(x), std::abs(y)}))) {
      return false;
    }
  }
  return true;
}

// The oracle, in the session layer: each runtime skeleton's joints as its
// rest transforms, which matrix4d holds at full precision where a
// SkelAnimation's half scales would not; a SkelAnimation animating no joint
// that carries the result's shape weights; and the result's canonical
// material values on their materials. UsdImaging and usdSkelImaging then
// compose what the fast adapter must reproduce.
class Oracle {
public:
  bool Prepare(const UsdStageRefPtr& stage, UsdTimeCode time,
      const ArStateView& layout) {
    stage_ = stage;
    time_ = time;
    meters_per_unit_ = UsdGeomGetStageMetersPerUnit(stage);
    const UsdEditContext edit(stage, stage->GetSessionLayer());
    for (std::uint32_t i = 0; i < layout.joint_count; ++i) {
      Find(layout.joints[i].skeleton_id).joints.push_back(i);
    }
    for (std::uint32_t i = 0; i < layout.blend_shape_count; ++i) {
      const ArBlendShape& shape = layout.blend_shapes[i];
      const UsdPrim mesh = stage->GetPrimAtPath(SdfPath(shape.mesh_id));
      const UsdSkelSkeleton bound = mesh
          ? UsdSkelBindingAPI(mesh).GetInheritedSkeleton()
          : UsdSkelSkeleton();
      if (!Check(bool(bound), std::string(shape.mesh_id) +
                                  ": a runtime shape's mesh must bind a skeleton")) {
        return false;
      }
      Skeleton& skeleton = Find(bound.GetPath().GetString());
      const TfToken token(shape.target_id);
      const auto found = std::find(skeleton.shapes.begin(), skeleton.shapes.end(), token);
      skeleton.shape_of.push_back(static_cast<std::size_t>(found - skeleton.shapes.begin()));
      if (found == skeleton.shapes.end()) skeleton.shapes.push_back(token);
      shape_skeleton_.push_back(static_cast<std::size_t>(&skeleton - skeletons_.data()));
    }
    for (Skeleton& skeleton : skeletons_) {
      const UsdSkelSkeleton prim(stage->GetPrimAtPath(skeleton.path));
      VtTokenArray order;
      if (!Check(prim && prim.GetJointsAttr().Get(&order) &&
                     prim.GetRestTransformsAttr().Get(&skeleton.rest) &&
                     skeleton.rest.size() == order.size(),
              skeleton.path.GetString() + ": a runtime skeleton must be a Skeleton with rests")) {
        return false;
      }
      for (const std::uint32_t i : skeleton.joints) {
        const auto at = std::find(order.begin(), order.end(),
            TfToken(layout.joints[i].joint_id));
        if (!Check(at != order.end(), skeleton.path.GetString() + " " +
                                          layout.joints[i].joint_id +
                                          ": a runtime joint must be the skeleton's")) {
          return false;
        }
        skeleton.slots.push_back(static_cast<std::size_t>(at - order.begin()));
      }
      skeleton.world = UsdGeomXformable(prim.GetPrim()).ComputeLocalToWorldTransform(time);
      const UsdSkelAnimation animation = UsdSkelAnimation::Define(stage,
          skeleton.path.AppendChild(TfToken("ToonProviderOracle")));
      animation.CreateJointsAttr().Set(VtTokenArray());
      animation.CreateBlendShapesAttr().Set(skeleton.shapes);
      UsdSkelBindingAPI::Apply(prim.GetPrim()).CreateAnimationSourceRel().SetTargets(
          {animation.GetPath()});
      skeleton.animation = animation;
    }
    return true;
  }

  // Joints at rest must give back the skeleton's authored rest transforms,
  // or the root placement convention is not the one this oracle assumes.
  bool CheckRest(const ArStateView& rest) const {
    for (const Skeleton& skeleton : skeletons_) {
      const VtMatrix4dArray locals = Locals(skeleton, rest);
      for (std::size_t i = 0; i < locals.size(); ++i) {
        if (!Check(NearMatrix(locals[i], skeleton.rest[skeleton.slots[i]], 1e-5),
                skeleton.path.GetString() + " " +
                    rest.joints[skeleton.joints[i]].joint_id +
                    ": the runtime rest joint must give the authored rest transform")) {
          return false;
        }
      }
    }
    return true;
  }

  bool Author(const ArStateView& view) {
    const UsdEditContext edit(stage_, stage_->GetSessionLayer());
    std::vector<VtFloatArray> weights;
    std::vector<std::vector<bool>> set;
    for (const Skeleton& skeleton : skeletons_) {
      weights.emplace_back(skeleton.shapes.size(), 0.0F);
      set.emplace_back(skeleton.shapes.size(), false);
    }
    for (std::uint32_t i = 0; i < view.blend_shape_count; ++i) {
      const std::size_t s = shape_skeleton_[i];
      const std::size_t slot = skeletons_[s].shape_of[i];
      const float weight = static_cast<float>(view.blend_shapes[i].weight);
      // A SkelAnimation names shapes, not meshes: two meshes sharing a
      // token can be authored only with one weight.
      if (!Check(!set[s][slot] || weights[s][slot] == weight,
              std::string(view.blend_shapes[i].target_id) +
                  ": meshes sharing a shape token carry different weights")) {
        return false;
      }
      weights[s][slot] = weight;
      set[s][slot] = true;
    }
    for (std::size_t s = 0; s < skeletons_.size(); ++s) {
      const Skeleton& skeleton = skeletons_[s];
      VtMatrix4dArray rest = skeleton.rest;
      const VtMatrix4dArray locals = Locals(skeleton, view);
      for (std::size_t i = 0; i < locals.size(); ++i) rest[skeleton.slots[i]] = locals[i];
      if (!UsdSkelSkeleton(stage_->GetPrimAtPath(skeleton.path))
               .GetRestTransformsAttr()
               .Set(rest) ||
          !skeleton.animation.CreateBlendShapeWeightsAttr().Set(weights[s], time_)) {
        return Check(false, skeleton.path.GetString() + ": cannot author the oracle pose");
      }
    }
    for (std::uint32_t i = 0; i < view.material_count; ++i) {
      const ArMaterialInput& value = view.materials[i];
      const UsdPrim material = stage_->GetPrimAtPath(SdfPath(value.material_id));
      const TfToken name(value.input_id);
      bool authored = false;
      switch (value.value_type) {
      case AR_VALUE_SCALAR:
        authored = material.CreateAttribute(name, SdfValueTypeNames->Float)
                       .Set(static_cast<float>(value.value[0]), time_);
        break;
      case AR_VALUE_VEC2:
        authored = material.CreateAttribute(name, SdfValueTypeNames->Float2)
                       .Set(GfVec2f(static_cast<float>(value.value[0]),
                                static_cast<float>(value.value[1])),
                           time_);
        break;
      case AR_VALUE_VEC3:
        authored = material.CreateAttribute(name, SdfValueTypeNames->Color3f)
                       .Set(GfVec3f(static_cast<float>(value.value[0]),
                                static_cast<float>(value.value[1]),
                                static_cast<float>(value.value[2])),
                           time_);
        break;
      default:
        break;
      }
      if (!Check(material && authored, std::string(value.material_id) + " " +
                                           value.input_id + ": cannot author the oracle value")) {
        return false;
      }
    }
    return true;
  }

private:
  struct Skeleton {
    SdfPath path;
    std::vector<std::uint32_t> joints; // runtime indices, runtime order
    std::vector<std::size_t> slots;    // each one's index in the skeleton
    VtMatrix4dArray rest;              // as authored
    VtTokenArray shapes;
    std::vector<std::size_t> shape_of; // per runtime shape of this skeleton
    GfMatrix4d world{1.0};
    UsdSkelAnimation animation;
  };

  Skeleton& Find(const std::string& path) {
    for (Skeleton& skeleton : skeletons_) {
      if (skeleton.path.GetString() == path) return skeleton;
    }
    Skeleton& skeleton = skeletons_.emplace_back();
    skeleton.path = SdfPath(path);
    return skeleton;
  }

  // Parent-local joint transforms in stage units. A runtime root carries
  // the skeleton's placement in runtime world, which its USD local does not.
  VtMatrix4dArray Locals(const Skeleton& skeleton, const ArStateView& view) const {
    VtMatrix4dArray locals;
    for (const std::uint32_t i : skeleton.joints) {
      const ArJoint& joint = view.joints[i];
      GfMatrix4d local = Matrix(joint.local, meters_per_unit_);
      if (joint.parent_index < 0) local = local * skeleton.world.GetInverse();
      locals.push_back(local);
    }
    return locals;
  }

  UsdStageRefPtr stage_;
  UsdTimeCode time_;
  double meters_per_unit_ = 1.0;
  std::vector<Skeleton> skeletons_;
  std::vector<std::size_t> shape_skeleton_; // per runtime shape
};

const Toon::MeshSnapshot* Mesh(const Toon::FrameSnapshot& scene, Toon::MeshId id) {
  for (const auto& mesh : scene.meshes) {
    if (mesh.id == id) return &mesh;
  }
  return nullptr;
}

const Toon::MaterialSnapshot* Material(const Toon::FrameSnapshot& scene,
    Toon::MaterialId id) {
  for (const auto& material : scene.materials) {
    if (material.id == id) return &material;
  }
  return nullptr;
}

float Relative(const Toon::Matrix4& a, const Toon::Matrix4& b) {
  float error = 0.0F;
  for (std::size_t i = 0; i < a.m.size(); ++i) {
    const float scale = std::max({1.0F, std::abs(a.m[i]), std::abs(b.m[i])});
    error = std::max(error, std::abs(a.m[i] - b.m[i]) / scale);
  }
  return error;
}

struct Totals {
  float palette_error = 0.0F;
  float weight_error = 0.0F;
  std::size_t frames = 0;
  std::size_t posed = 0;     // frames whose fast palettes left the rest
  std::size_t weighted = 0;  // frames with a nonzero fast subshape weight
  std::size_t coloured = 0;  // frames whose fast materials left the scene's
};

// The fast result over the bound scene against the oracle's Hydra commit,
// for every bound target; and the fast result's static state against the
// bound scene's.
bool Compare(const Toon::AvatarBindings& bindings, const Toon::FrameSnapshot& bound,
    const Toon::FrameSnapshot& rest, const Toon::FrameSnapshot& direct,
    const Toon::FrameSnapshot& hydra, const std::string& label, Totals& totals) {
  constexpr float kPalette = 1e-4F;
  constexpr float kWeight = 1e-6F;
  bool posed = false;
  bool weighted = false;
  bool coloured = false;
  for (const auto& skin : bindings.skins) {
    const auto* a = Mesh(direct, skin.mesh);
    const auto* b = Mesh(hydra, skin.mesh);
    const auto* r = Mesh(rest, skin.mesh);
    if (!Check(a && b && r && a->joints->size() == b->joints->size(),
            label + ": a bound skin must be committed by both paths")) {
      return false;
    }
    float error = Relative(a->skeleton_to_mesh, b->skeleton_to_mesh);
    std::size_t worst = a->joints->size();
    for (std::size_t i = 0; i < a->joints->size(); ++i) {
      const float entry = Relative((*a->joints)[i], (*b->joints)[i]);
      if (entry > error) {
        error = entry;
        worst = i;
      }
      posed = posed || Relative((*a->joints)[i], (*r->joints)[i]) > kPalette;
    }
    totals.palette_error = std::max(totals.palette_error, error);
    if (error > kPalette) {
      std::cerr << label << ": mesh " << skin.mesh << " palette entry " << worst
                << " (fast, then usdSkelImaging):\n";
      for (const auto* matrix : worst < a->joints->size()
               ? std::array{&(*a->joints)[worst], &(*b->joints)[worst]}
               : std::array{&a->skeleton_to_mesh, &b->skeleton_to_mesh}) {
        for (const float value : matrix->m) std::cerr << ' ' << value;
        std::cerr << '\n';
      }
    }
    if (!Check(error <= kPalette, label + ": mesh " + std::to_string(skin.mesh) +
                                      " palette differs from usdSkelImaging's by " +
                                      std::to_string(error))) {
      return false;
    }
  }
  std::set<Toon::MeshId> morphed;
  for (const auto& morph : bindings.morphs) morphed.insert(morph.mesh);
  for (const Toon::MeshId id : morphed) {
    const auto* a = Mesh(direct, id);
    const auto* b = Mesh(hydra, id);
    if (!Check(a && b && a->morph_weights->size() == b->morph_weights->size(),
            label + ": a bound morph must be committed by both paths")) {
      return false;
    }
    for (std::size_t i = 0; i < a->morph_weights->size(); ++i) {
      const float x = (*a->morph_weights)[i];
      const float error = std::abs(x - (*b->morph_weights)[i]);
      totals.weight_error = std::max(totals.weight_error, error);
      weighted = weighted || x != 0.0F;
      if (!Check(error <= kWeight, label + ": mesh " + std::to_string(id) + " subshape " +
                                       std::to_string(i) + " weight " + std::to_string(x) +
                                       " is not usdSkelImaging's " +
                                       std::to_string((*b->morph_weights)[i]))) {
        return false;
      }
    }
  }
  std::set<Toon::MaterialId> materials;
  for (const auto& material : bindings.materials) materials.insert(material.material);
  for (const Toon::MaterialId id : materials) {
    const auto* a = Material(direct, id);
    const auto* b = Material(hydra, id);
    const auto* s = Material(bound, id);
    if (!Check(a && b && s && a->material == b->material,
            label + ": material " + std::to_string(id) +
                " differs from the delegate's normalization of the same values")) {
      return false;
    }
    coloured = coloured || !(a->material == s->material);
  }
  // Value-only: the fast result keeps every static array and revision.
  for (const auto& mesh : direct.meshes) {
    const auto* s = Mesh(bound, mesh.id);
    if (!Check(s && mesh.points == s->points && mesh.indices == s->indices &&
                   mesh.normals == s->normals && mesh.uvs == s->uvs &&
                   mesh.influences == s->influences &&
                   mesh.morph_offsets == s->morph_offsets &&
                   mesh.points_revision == s->points_revision &&
                   mesh.topology_revision == s->topology_revision &&
                   mesh.normals_revision == s->normals_revision &&
                   mesh.skin_revision == s->skin_revision &&
                   mesh.morph_revision == s->morph_revision &&
                   mesh.material == s->material,
            label + ": the fast result changed a mesh's static state")) {
      return false;
    }
  }
  for (const auto& material : direct.materials) {
    const auto* s = Material(bound, material.id);
    if (!Check(s && material.structure_revision == s->structure_revision &&
                   !Toon::IsStructuralChange(s->material, material.material),
            label + ": the fast result changed a material's structure")) {
      return false;
    }
  }
  ++totals.frames;
  totals.posed += posed;
  totals.weighted += weighted;
  totals.coloured += coloured;
  return true;
}

// Orthographic views framing the rest points of `framed` meshes (every mesh
// when empty), from +Z and from -Z: avatars face either way.
std::array<Toon::ToonView, 2> Framing(const Toon::FrameSnapshot& scene,
    const std::set<Toon::MeshId>& framed) {
  GfRange3d box;
  for (const auto& mesh : scene.meshes) {
    if (!framed.empty() && !framed.contains(mesh.id)) continue;
    const auto& m = mesh.transform.m;
    for (const auto& p : *mesh.points) {
      box.UnionWith(GfVec3d(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
          m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
          m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]));
    }
  }
  const GfVec3d center = box.GetMidpoint();
  const GfVec3d half = box.GetSize() * 0.5;
  const double extent = std::max(half[0], half[1]) * 1.15 + 1e-6;
  // Deep enough for the whole avatar in front of and behind the framed part.
  const double depth = std::max(half[2] * 2.0 + extent, 4.0 / scene.meters_per_unit);
  std::array<Toon::ToonView, 2> views;
  for (std::size_t side = 0; side < views.size(); ++side) {
    const float turn = side == 0 ? 1.0F : -1.0F; // a half turn about +Y
    Toon::ToonView& view = views[side];
    view.view.m[0] = turn;
    view.view.m[10] = turn;
    view.projection.m[0] = static_cast<float>(1.0 / extent);
    view.projection.m[5] = static_cast<float>(1.0 / extent);
    view.projection.m[10] = static_cast<float>(-1.0 / depth);
    view.projection.m[12] = static_cast<float>(-turn * center[0] / extent);
    view.projection.m[13] = static_cast<float>(-center[1] / extent);
    view.projection.m[14] = static_cast<float>(turn * center[2] / depth);
  }
  return views;
}

// Both paths on the same Vulkan configuration. Float palettes that round
// differently move a vertex by a fraction of a pixel, so a pixel may differ
// by one 8-bit colour step and 1e-4 of the views' -1..1 depth anywhere; at
// most 16 pixels of a 256x256 view (0.025%) may differ by more, where an
// edge's coverage flips.
class Images {
public:
  bool Start(const std::string& directory, const Toon::FrameSnapshot& bound,
      const std::set<Toon::MeshId>& framed) {
    Toon::FrameStatus status;
    std::string error;
    const auto shaders = Toon::SceneShadersIn(directory);
    fast_ = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
    oracle_ = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
    if (status == Toon::FrameStatus::Skip) {
      std::cout << error << '\n';
      return false;
    }
    views_ = Framing(bound, framed);
    return Check(fast_ && oracle_, error);
  }

  bool Compare(const Toon::FrameSnapshot& direct, const Toon::FrameSnapshot& hydra,
      const std::string& label) {
    bool changed = false;
    for (std::size_t side = 0; side < views_.size(); ++side) {
      auto a = direct;
      auto b = hydra;
      a.view = b.view = views_[side];
      std::string error;
      if (!Check(fast_->Render(Toon::ExtractDrawList(a), 256, 256, color_, depth_, error) &&
                  oracle_->Render(Toon::ExtractDrawList(b), 256, 256, oracle_color_,
                      oracle_depth_, error),
              error)) {
        return false;
      }
      // Per pixel: a colour channel more than one step apart, or depth
      // more than 1e-4 apart, flips that pixel.
      const std::size_t pixels = depth_.payload.size();
      const std::size_t channels = pixels ? color_.payload.size() / pixels : 0;
      std::size_t flipped = 0;
      float depth = 0.0F;
      for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        int step = 0;
        for (std::size_t c = 0; c < channels; ++c) {
          const std::size_t i = pixel * channels + c;
          step = std::max(step, std::abs(int(color_.payload[i]) - int(oracle_color_.payload[i])));
        }
        const float d = std::abs(depth_.payload[pixel] - oracle_depth_.payload[pixel]);
        largest_ = std::max(largest_, step);
        depth = std::max(depth, d);
        deep_ += d > 1e-6F;
        flipped += step > 1 || d > 1e-4F;
      }
      flipped_ += flipped;
      most_flipped_ = std::max(most_flipped_, flipped);
      depth_error_ = std::max(depth_error_, depth);
      if (frames_ == 0) {
        rest_[side] = color_.payload;
        if (side == 0) first_ = fast_->statistics();
      } else {
        changed = changed || color_.payload != rest_[side];
      }
      if (!Check(flipped <= kFlips,
              label + ": " + std::to_string(flipped) +
                  " pixels differ from the oracle's beyond one colour step or 1e-4 depth")) {
        return false;
      }
    }
    ++frames_;
    changed_ += changed;
    // The first frame's second view still uploads nothing new.
    const auto& stats = fast_->statistics();
    return Check(stats.point_uploads == first_.point_uploads &&
                     stats.topology_uploads == first_.topology_uploads &&
                     stats.skin_uploads == first_.skin_uploads &&
                     stats.morph_uploads == first_.morph_uploads &&
                     stats.texture_uploads == first_.texture_uploads &&
                     stats.pipelines_created == first_.pipelines_created,
               label + ": the fast renderer uploaded a static resource") &&
        Check(stats.validation_message_count == 0 &&
                  oracle_->statistics().validation_message_count == 0,
            label + ": Vulkan validation reported an error");
  }

  void Report() const {
    const auto& stats = fast_->statistics();
    std::cout << "  images: " << frames_ << " results drawn from two sides at 256x256, "
              << "largest colour step " << largest_ << ", depth " << depth_error_ << " ("
              << deep_ << " depth samples over 1e-6), " << flipped_
              << " flipped pixels (at most " << most_flipped_ << " in a view); " << changed_
              << " fast results differ from the rest images; after the first, "
              << stats.pose_writes - first_.pose_writes << " joint, "
              << stats.morph_weight_writes - first_.morph_weight_writes << " weight and "
              << stats.material_writes - first_.material_writes
              << " material writes, no static uploads (" << stats.device_name << ")\n";
  }
  [[nodiscard]] std::size_t Changed() const { return changed_; }

private:
  std::unique_ptr<Toon::OffscreenRenderer> fast_, oracle_;
  std::array<Toon::ToonView, 2> views_;
  Toon::ColorProduct color_, oracle_color_;
  Toon::DepthProduct depth_, oracle_depth_;
  std::array<std::vector<std::uint8_t>, 2> rest_;
  Toon::OffscreenStatistics first_;
  std::size_t frames_ = 0, changed_ = 0;
  int largest_ = 0;
  float depth_error_ = 0.0F;
  std::size_t deep_ = 0;
  std::size_t flipped_ = 0, most_flipped_ = 0;
  static constexpr std::size_t kFlips = 16;
};

// The head's world position in metres, composed from the baseline's joints.
std::optional<GfVec3d> HeadPosition(const ArStateView& state,
    const std::string& skeleton, const std::string& head) {
  std::vector<GfMatrix4d> world;
  for (std::uint32_t i = 0; i < state.joint_count; ++i) {
    const ArJoint& joint = state.joints[i];
    GfMatrix4d local = Matrix(joint.local, 1.0);
    if (joint.parent_index >= 0) local = local * world[static_cast<std::size_t>(joint.parent_index)];
    world.push_back(local);
    if (skeleton == joint.skeleton_id && head == joint.joint_id) {
      return world.back().ExtractTranslation();
    }
  }
  return std::nullopt;
}

int Run(const Options& options) {
  const UsdStageRefPtr stage = UsdStage::Open(options.stage);
  if (!Check(stage && stage->GetDefaultPrim(), "cannot open the stage or it has no default prim")) {
    return 1;
  }
  Host host(stage);
  const Toon::FrameSnapshot bound = host.Sync();
  const HdToonResidentTargets resident = host.Describe();

  // The real providers: owner bindings read once, one registered evaluator.
  Diagnostics diagnostics;
  const avatarVrmUsd::HumanoidBindingConfig humanoid{
      stage->GetDefaultPrim().GetPath(), {}, "toon.provider-check", 1};
  const avatarVrmUsd::ExpressionBinding expressions(stage, {humanoid, {}});
  const avatarVrmUsd::LookAtBinding look(stage, {humanoid, options.look_at});
  avatarVrm::ExpressionAdapterConfig config =
      look.AdapterConfig("toon.provider-check.vrm", {kSource, kActor, kGaze});
  expressions.ApplyTo(config);
  for (const auto& expression : expressions.Rig().GetExpressions()) {
    config.inputs.push_back({{kSource, kActor, Channel(expression.name)}, expression.name});
  }
  const std::string head_skeleton = config.headSkeleton;
  const std::string head_joint = config.headJoint;
  const avatarVrm::ExpressionAdapter provider(config);
  const ArEvaluatorDesc descriptor = provider.Descriptor();

  Runtime runtime;
  ArDiagnosticSink sink = diagnostics.Sink();
  const ArStateView& baseline = expressions.Baseline();
  if (!Check(arGetApi(AR_ABI_VERSION, sizeof(runtime.api), &runtime.api) == AR_OK &&
                 runtime.api.create_runtime(&runtime.runtime) == AR_OK &&
                 runtime.api.register_evaluator(runtime.runtime, &descriptor, &sink) == AR_OK,
          "cannot register the provider in the runtime")) {
    return 1;
  }
  ArInstanceDesc instance{AR_HEADER(ArInstanceDesc)};
  instance.generation = 1;
  instance.initial_state = baseline;
  instance.layout_id = baseline.layout_id;
  instance.layout_version = baseline.layout_version;
  const char* evaluators[] = {descriptor.id};
  instance.evaluators = evaluators;
  instance.evaluator_count = 1;
  instance.bound_capabilities = descriptor.supplies;
  instance.bound_capability_count = descriptor.supply_count;
  if (!Check(runtime.api.create_instance(runtime.runtime, &instance, &sink,
                 &runtime.instance) == AR_OK,
          "cannot create the runtime instance")) {
    return 1;
  }

  // Frames: rest, every expression alone at full and half weight, a mix
  // with gaze, gaze alone the other way, and rest again after a reset.
  const auto head = HeadPosition(baseline, head_skeleton, head_joint);
  if (!Check(head.has_value(), "the provider's head joint must be in its layout")) return 1;
  std::vector<Frame> frames{{"rest", {}, std::nullopt, false}};
  std::vector<std::string> names;
  for (const auto& expression : expressions.Rig().GetExpressions()) {
    names.push_back(expression.name);
  }
  for (const auto& name : names) {
    frames.push_back({name + "=1", {{name, 1.0}}, std::nullopt, false});
    frames.push_back({name + "=0.5", {{name, 0.5}}, std::nullopt, false});
  }
  Frame mix{"mix+gaze", {}, *head + GfVec3d(0.6, 0.3, 1.0), false};
  const double mixed[] = {0.4, 0.8, 0.6};
  for (std::size_t i = 0; i < names.size() && i < 3; ++i) {
    mix.weights.push_back({names[i], mixed[i]});
  }
  frames.push_back(mix);
  frames.push_back({"gaze", {}, *head + GfVec3d(-0.6, -0.3, 1.0), false});
  frames.push_back({"reset", {}, std::nullopt, true});

  Toon::AvatarStateAdapter adapter;
  HdToonAvatarTargetMatch match;
  Oracle oracle;
  Images images;
  const bool gpu = !options.shaders.empty();
  Toon::FrameSnapshot rest;
  Totals totals;
  std::uint64_t generation = 1, frame_id = 0;
  std::size_t gaze_posed = 0, gaze_weighted = 0;
  for (const Frame& frame : frames) {
    if (frame.reset) {
      if (!Check(runtime.api.reset_instance(runtime.runtime, runtime.instance, ++generation) == AR_OK,
              "cannot reset the runtime instance")) {
        return 1;
      }
      frame_id = 0;
    }
    const ArSnapshot handle = Evaluate(runtime, diagnostics, ++frame_id, generation, frame);
    Toon::RetainedAvatarSnapshot held;
    std::string error;
    const bool retained = handle != 0 && held.Reset(runtime.api, handle, error);
    if (handle) runtime.api.release_snapshot(handle);
    if (!Check(retained, frame.label + ": the runtime published no result " + error)) {
      return 1;
    }
    const ArStateView& view = *held.View();
    if (frame_id == 1 && generation == 1) {
      // Match the published result's identities, then bind them once.
      match = HdToonMatchAvatarTargets(view, resident, HdToonCanonicalMaterialInputs());
      for (const auto& mismatch : match.mismatches) {
        std::cerr << "  mismatch " << mismatch.subject << ": " << mismatch.reason << '\n';
      }
      if (!Check(match.mismatches.empty(), "every runtime output must match a resident target") ||
          !Check(adapter.Bind(view, bound, 1, match.bindings, error), "bind: " + error) ||
          !Check(oracle.Prepare(stage, host.Time(), view) && oracle.CheckRest(view),
              "cannot prepare the oracle")) {
        return 1;
      }
      std::size_t inbetweens = 0;
      for (const auto& morph : match.bindings.morphs) inbetweens += morph.inbetweens.size();
      std::cout << "  provider layout: " << view.joint_count << " joints, "
                << view.blend_shape_count << " shapes, " << view.material_count
                << " material inputs from " << names.size() << " expressions\n"
                << "  matched: " << match.bindings.skins.size() << " skins, "
                << match.bindings.morphs.size() << " morphs (" << inbetweens
                << " inbetween slots), " << match.bindings.materials.size()
                << " material fields, " << match.bindings.visibility.size()
                << " visibility targets\n";
    }
    Toon::FrameSnapshot direct;
    if (!Check(adapter.Apply(held, bound, 1, direct, error), frame.label + ": apply: " + error) ||
        !Check(adapter.IdentityInfo().frame == view.frame_id &&
                   adapter.IdentityInfo().generation == view.generation,
            frame.label + ": the applied identity must be the result's") ||
        !Check(oracle.Author(view), frame.label + ": cannot author the oracle")) {
      return 1;
    }
    const Toon::FrameSnapshot hydra = host.Sync();
    if (frame_id == 1 && generation == 1) {
      rest = direct;
      // Frame what the results change most finely: the morphed meshes.
      std::set<Toon::MeshId> framed;
      for (const auto& morph : match.bindings.morphs) framed.insert(morph.mesh);
      if (gpu && !images.Start(options.shaders, bound, framed)) return 77;
    }
    const std::size_t posed = totals.posed, weighted = totals.weighted;
    if (!Compare(match.bindings, bound, rest, direct, hydra, frame.label, totals) ||
        (gpu && !images.Compare(direct, hydra, frame.label))) {
      return 1;
    }
    if (frame.gaze && frame.weights.empty()) {
      gaze_posed += totals.posed - posed;
      gaze_weighted += totals.weighted - weighted;
    }
  }

  std::cout << "  " << totals.frames << " results compared: palettes within "
            << totals.palette_error << " relative, subshape weights within "
            << totals.weight_error << "; " << totals.posed << " posed, "
            << totals.weighted << " weighted and " << totals.coloured
            << " recoloured fast frames\n";
  if (gpu) images.Report();
  for (const auto& [code, count] : diagnostics.counts) {
    std::cout << "  " << code << " x" << count << '\n';
  }
  if (!Check(totals.frames == frames.size(), "every result must be compared") ||
      !Check(options.expect_gaze != "bone" || gaze_posed > 0,
          "bone gaze must move a bound palette") ||
      !Check(options.expect_gaze != "expression" || (gaze_weighted > 0 && gaze_posed == 0),
          "expression gaze must weight shapes without posing") ||
      !Check(!options.expect_materials ||
                 (!match.bindings.materials.empty() && totals.coloured > 0),
          "material outputs must bind and change a material") ||
      !Check(!options.expect_images || (gpu && images.Changed() > 0),
          "the fast images must show the results")) {
    return 1;
  }
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--lookat" && i + 1 < argc) {
      options.look_at = SdfPath(argv[++i]);
    } else if (arg == "--expect-gaze" && i + 1 < argc) {
      options.expect_gaze = argv[++i];
    } else if (arg == "--expect-materials") {
      options.expect_materials = true;
    } else if (arg == "--expect-images") {
      options.expect_images = true;
    } else if (arg == "--shaders" && i + 1 < argc) {
      options.shaders = argv[++i];
    } else if (options.stage.empty() && arg.rfind("--", 0) != 0) {
      options.stage = arg;
    } else {
      options.stage.clear();
      break;
    }
  }
  if (options.stage.empty()) {
    std::cerr << "usage: toon-hydra2-avatar-provider-test <stage> [--lookat <prim>]\n"
                 "         [--expect-gaze bone|expression] [--expect-materials]\n"
                 "         [--shaders <directory> [--expect-images]]\n";
    return 2;
  }
  // What toon-viewport sets, so usdSkelImaging hands over authored normals.
  TfSetenv("USDSKELIMAGING_ENABLE_NORMAL_COMPUTATIONS", "1");
  std::cout << std::unitbuf << options.stage << '\n';
  try {
    return Run(options);
  } catch (const std::exception& error) {
    // Owner bindings and the registration adapter refuse by throwing.
    std::cerr << "a provider refused the avatar: " << error.what() << '\n';
    return 1;
  }
}
