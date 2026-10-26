// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <avatarMmd/PhysicsAdapter.h>
#include <avatarRuntime/physics.h>
#include <usd_physics/jolt/availability.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xform.h>
#include <algorithm>
#include <stdexcept>

// Test-only host: owner control/continuations and body feedback, runtime clock,
// and an installed Jolt world. No solver or pose algorithm lives in rendering.
class MmdAvatarProvider {
  static void Require(bool ok) {
    if (!ok) throw std::runtime_error("generated MMD avatar provider failed");
  }
  std::unique_ptr<avatarMmd::PhysicsAdapter> adapter_;
  std::unique_ptr<usd_physics::core::PhysicsWorld> solver_;
  usd_physics::core::BodyHandle body_;
  ArRuntime runtime_ = 0;
  ArInstance instance_ = 0;
  ArPhysicsWorld world_ = 0;
  ArPhysicsApi physics_{};
  uint64_t request_ = 0, steps_ = 0;
  void Initialize() {
    namespace core = usd_physics::core;
    solver_ = usd_physics::jolt::createWorld();
    core::BodyDescriptor body;
    body.shape = solver_->createShape({});
    body.motionType = core::MotionType::dynamicBody;
    body.collisionFilter = {1, 0};
    body.damping = {0, 0};
    body_ = solver_->createBody(body);
    Require(solver_->setLinearVelocity(body_, {.15, 0, 0}));
    steps_ = 0;
  }
public:
  ArRuntimeApi api{};
  static constexpr const char* mesh = "/Avatar/Body";
  static constexpr const char* skeleton = "/Avatar/Rig";
  static constexpr const char* shape = "lift";
  static constexpr double rootX[]{0, .04, .08, -.04, 0, 0};
  static constexpr float weights[]{0, 1, 1.5f, -.5f, 0, 0};

  MmdAvatarProvider() {
    using namespace pxr;
    const auto stage = UsdStage::CreateInMemory();
    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
    UsdGeomSetStageMetersPerUnit(stage, 1);
    const auto rig = UsdSkelSkeleton::Define(stage, SdfPath(skeleton));
    rig.CreateJointsAttr().Set(VtTokenArray{TfToken("root"), TfToken("root/dynamic")});
    rig.CreateRestTransformsAttr().Set(VtMatrix4dArray(2, GfMatrix4d(1)));
    auto body = UsdGeomXform::Define(stage, SdfPath("/Avatar/BodyPhysics"));
    UsdPhysicsRigidBodyAPI::Apply(body.GetPrim()).CreateKinematicEnabledAttr().Set(false);
    body.GetPrim().CreateAttribute(TfToken("mmd:physics:bone"), SdfValueTypeNames->Int, true,
        SdfVariabilityUniform).Set(1);
    body.GetPrim().CreateAttribute(TfToken("mmd:physics:mode"), SdfValueTypeNames->Token, true,
        SdfVariabilityUniform).Set(TfToken("dynamic"));
    const std::vector<UsdPrim> bodies{body.GetPrim()};
    auto coupling = mmd::physics::Adapter::Bind(rig, bodies);
    Require(bool(coupling));
    mmd::CanonicalDocument model;
    model.skeleton.bones.resize(2);
    model.rig.bones.resize(2);
    model.skeleton.bones[0].parent = -1;
    model.skeleton.bones[0].jointPath = "root";
    model.skeleton.bones[1].parent = 0;
    model.skeleton.bones[1].jointPath = "root/dynamic";
    model.mesh.points.resize(4);
    model.mesh.st = {{{0,0}}, {{1,0}}, {{1,1}}, {{0,1}}};
    model.morphs.resize(5);
    model.morphs[0].type = model.morphs[1].type = mmd::MorphType::Uv;
    model.morphs[0].uvOffsets = {{2, {-1,0,0,0}}, {3, {1,0,0,0}}};
    model.morphs[1].uvOffsets = {{1, {-1,0,0,0}}, {2, {-1,1,0,0}}, {3, {0,1,0,0}}};
    model.morphs[2].type = mmd::MorphType::Vertex;
    model.morphs[2].vertexOffsets = {{2, {.12f,.05f,.12f}}};
    model.morphs[3].type = model.morphs[4].type = mmd::MorphType::Group;
    model.morphs[3].members = {{0,.5f}, {2,.5f}};
    model.morphs[4].members = {{3,2}};
    auto evaluator = mmd::control::Evaluator::Prepare(model);
    Require(bool(evaluator));
    mmd::binding::BoundMotion motion;
    motion.morphs = {{1, {{1,0},{2,0},{3,0},{4,0},{5,1},{6,0}}},
        {4, {{1,0},{2,1},{3,1.5f},{4,-.5f},{5,0},{6,0}}}};
    mmd::binding::BoneTrack track;
    track.joint = 0;
    for (uint32_t i = 0; i < 6; ++i) {
      mmd::binding::BoneKey key;
      key.frame = i + 1;
      key.translation = {float(rootX[i]), 0, 0};
      track.keys.push_back(key);
    }
    motion.bones.push_back(track);
    avatarMmd::PhysicsAdapterConfig config{evaluator.value(), motion, coupling.value()};
    config.preId = "avatar.pre"; config.postId = "avatar.post"; config.layoutId = "mmd.avatar";
    config.joints = {{skeleton, "root", -1}, {skeleton, "root/dynamic", 0}};
    config.morphs = {{2, mesh, shape}};
    config.uvMesh = mesh; config.uvMorphs = {0,1};
    config.baseUvs = {{0,0},{1,0},{1,1},{0,1}};
    config.submit = [this](const ArEvaluationContext&, auto commands) {
      Require(commands.empty()); // mode 1 has no kinematic commands
      return AR_OK;
    };
    config.snapshot = [this](const ArEvaluationContext& c, auto& out) {
      Require(steps_ == c.input->frame_id);
      const auto t = BodyTransform();
      out = {{SdfPath("/Avatar/BodyPhysics"), {{t.translation.x,t.translation.y,t.translation.z},
          {t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w}}}};
      return AR_OK;
    };
    adapter_ = std::make_unique<avatarMmd::PhysicsAdapter>(std::move(config));
    Require(arGetApi(AR_ABI_VERSION, sizeof(api), &api) == AR_OK);
    Require(arGetPhysicsApi(AR_PHYSICS_API_VERSION, sizeof(physics_), &physics_) == AR_OK);
    Require(api.create_runtime(&runtime_) == AR_OK);
    const auto pre = adapter_->PreDescriptor(), post = adapter_->PostDescriptor();
    Require(api.register_evaluator(runtime_, &pre, nullptr) == AR_OK);
    Require(api.register_evaluator(runtime_, &post, nullptr) == AR_OK);
    ArJoint joints[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
      joints[i].skeleton_id = skeleton;
      joints[i].joint_id = i ? "root/dynamic" : "root";
      joints[i].parent_index = int32_t(i) - 1;
      joints[i].local.rotation[3] = 1;
      std::fill_n(joints[i].local.scale, 3, 1.);
    }
    ArBlendShape morph{mesh, shape, 0};
    ArVertexUv uvs[]{{mesh,2,{1,1}},{mesh,0,{0,0}},{mesh,3,{0,1}},{mesh,1,{1,0}}};
    const char* ids[]{pre.id, post.id};
    ArInstanceDesc desc{AR_HEADER(ArInstanceDesc)};
    desc.generation = desc.layout_version = 1;
    desc.layout_id = "mmd.avatar";
    desc.initial_state = {AR_HEADER(ArStateView)};
    desc.initial_state.joints = joints; desc.initial_state.joint_count = 2;
    desc.initial_state.blend_shapes = &morph; desc.initial_state.blend_shape_count = 1;
    desc.initial_state.vertex_uvs = uvs; desc.initial_state.vertex_uv_count = 4;
    desc.evaluators = ids; desc.evaluator_count = 2;
    Require(api.create_instance(runtime_, &desc, nullptr, &instance_) == AR_OK);
    Initialize();
    ArPhysicsParticipant participant{instance_, post.id};
    ArPhysicsWorldDesc world{AR_HEADER(ArPhysicsWorldDesc)};
    world.id = "avatar.world"; world.user_data = this;
    world.participants = &participant; world.participant_count = 1;
    world.fixed_step_seconds = 1. / 30; world.max_catch_up_ticks = 1;
    world.step = [](void* p, const ArPhysicsTick* tick, const ArDiagnosticSink*) -> ArStatus {
      auto& self = *static_cast<MmdAvatarProvider*>(p);
      self.solver_->step(usd_physics::core::PhysicsWorld::Duration(tick->delta_seconds));
      ++self.steps_;
      return AR_OK;
    };
    world.reset = [](void* p, const ArDiagnosticSink*) -> ArStatus {
      static_cast<MmdAvatarProvider*>(p)->Initialize();
      return AR_OK;
    };
    Require(physics_.create_world(runtime_, &world, nullptr, &world_) == AR_OK);
  }
  ~MmdAvatarProvider() { if (runtime_) api.destroy_runtime(runtime_); }
  MmdAvatarProvider(const MmdAvatarProvider&) = delete;
  MmdAvatarProvider& operator=(const MmdAvatarProvider&) = delete;
  usd_physics::core::Transform BodyTransform() const { return solver_->bodyState(body_).transform; }
  ArSnapshot Advance() {
    ArInputFrame input{AR_HEADER(ArInputFrame)};
    ArPhysicsInput selected{instance_, &input};
    ArPhysicsAdvanceDesc desc{AR_HEADER(ArPhysicsAdvanceDesc)};
    desc.request_id = ++request_; desc.elapsed_seconds = 1. / 30;
    desc.inputs = &selected; desc.input_count = 1;
    ArPhysicsAdvanceResult result{AR_HEADER(ArPhysicsAdvanceResult)};
    ArSnapshot snapshot = 0;
    Require(physics_.advance_world(runtime_, world_, &desc, nullptr, &result, &snapshot) == AR_OK);
    Require(snapshot && result.tick_count == 1);
    return snapshot;
  }
  void Reset(uint64_t generation) {
    Require(physics_.reset_world(runtime_, world_, generation, nullptr) == AR_OK);
  }
};
