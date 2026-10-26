// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <avatarMmd/PhysicsAdapter.h>
#include <avatarRuntime/physics.h>
#include <mmdModel/CanonicalDocument.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <stdexcept>

// Generated UV-only owner configuration. The empty world exercises the real
// runtime pre/post barrier without claiming solver, pose or PMX/VMD intake.
class MmdUvProvider {
  static void Require(bool ok) {
    if (!ok)
      throw std::runtime_error("generated MMD UV provider failed");
  }
  std::unique_ptr<avatarMmd::PhysicsAdapter> adapter_;
  ArRuntime runtime_ = 0;
  ArInstance instance_ = 0;
  ArPhysicsWorld world_ = 0;
  ArPhysicsApi physics_{};
  uint64_t request_ = 0;

public:
  ArRuntimeApi api{};
  MmdUvProvider() {
    using namespace pxr;
    const auto stage = UsdStage::CreateInMemory();
    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
    UsdGeomSetStageMetersPerUnit(stage, 1);
    const auto skeleton = UsdSkelSkeleton::Define(stage, SdfPath("/Rig"));
    skeleton.CreateJointsAttr().Set(VtTokenArray{});
    skeleton.CreateRestTransformsAttr().Set(VtMatrix4dArray{});
    auto coupling = mmd::physics::Adapter::Bind(skeleton, {});
    Require(bool(coupling));
    mmd::CanonicalDocument model;
    model.mesh.points.resize(4);
    model.mesh.st = {{{0, 0}}, {{1, 0}}, {{1, 1}}, {{0, 1}}};
    model.morphs.resize(4);
    model.morphs[0].type = model.morphs[1].type = mmd::MorphType::Uv;
    model.morphs[0].uvOffsets = {{2, {-1, 0, 0, 0}}, {3, {1, 0, 0, 0}}};
    // Owner V deltas have the opposite sign to canonical USD st.
    model.morphs[1].uvOffsets = {{1, {-1, 0, 0, 0}}, {2, {-1, 1, 0, 0}}, {3, {0, 1, 0, 0}}};
    model.morphs[2].type = model.morphs[3].type = mmd::MorphType::Group;
    model.morphs[2].members = {{0, .5f}};
    model.morphs[3].members = {{2, 2.f}};
    auto evaluator = mmd::control::Evaluator::Prepare(model);
    Require(bool(evaluator));
    mmd::binding::BoundMotion motion;
    motion.morphs = {{3, {{1, 0}, {2, 1}, {3, 1.5f}, {4, -.5f}, {5, 0}, {6, 0}}},
        {1, {{1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 1}, {6, 0}}}};
    avatarMmd::PhysicsAdapterConfig config{evaluator.value(), motion, coupling.value()};
    config.preId = "uv.pre";
    config.postId = "uv.post";
    config.layoutId = "mmd.uv";
    config.uvMesh = "/Body";
    config.uvMorphs = {0, 1};
    config.baseUvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    config.submit = [](const ArEvaluationContext&, auto bodies) {
      return bodies.empty() ? AR_OK : AR_PROVIDER_ERROR;
    };
    config.snapshot = [](const ArEvaluationContext&, auto& bodies) {
      bodies.clear();
      return AR_OK;
    };
    adapter_ = std::make_unique<avatarMmd::PhysicsAdapter>(std::move(config));
    Require(arGetApi(AR_ABI_VERSION, sizeof(api), &api) == AR_OK);
    Require(arGetPhysicsApi(AR_PHYSICS_API_VERSION, sizeof(physics_), &physics_) == AR_OK);
    Require(api.create_runtime(&runtime_) == AR_OK);
    const auto pre = adapter_->PreDescriptor(), post = adapter_->PostDescriptor();
    Require(api.register_evaluator(runtime_, &pre, nullptr) == AR_OK);
    Require(api.register_evaluator(runtime_, &post, nullptr) == AR_OK);
    ArVertexUv uvs[]{{"/Body", 2, {1, 1}}, {"/Body", 0, {0, 0}},
        {"/Body", 3, {0, 1}}, {"/Body", 1, {1, 0}}};
    const char* ids[]{pre.id, post.id};
    ArInstanceDesc desc{AR_HEADER(ArInstanceDesc)};
    desc.generation = desc.layout_version = 1;
    desc.layout_id = "mmd.uv";
    desc.initial_state = {AR_HEADER(ArStateView)};
    desc.initial_state.vertex_uvs = uvs;
    desc.initial_state.vertex_uv_count = 4;
    desc.evaluators = ids;
    desc.evaluator_count = 2;
    Require(api.create_instance(runtime_, &desc, nullptr, &instance_) == AR_OK);
    ArPhysicsParticipant participant{instance_, post.id};
    ArPhysicsWorldDesc world{AR_HEADER(ArPhysicsWorldDesc)};
    world.id = "uv.world";
    world.participants = &participant;
    world.participant_count = 1;
    world.fixed_step_seconds = 1. / 30;
    world.max_catch_up_ticks = 1;
    world.step = [](void*, const ArPhysicsTick*, const ArDiagnosticSink*) -> ArStatus { return AR_OK; };
    world.reset = [](void*, const ArDiagnosticSink*) -> ArStatus { return AR_OK; };
    Require(physics_.create_world(runtime_, &world, nullptr, &world_) == AR_OK);
  }
  ~MmdUvProvider() {
    if (runtime_)
      api.destroy_runtime(runtime_);
  }
  MmdUvProvider(const MmdUvProvider&) = delete;
  MmdUvProvider& operator=(const MmdUvProvider&) = delete;
  ArSnapshot Advance() {
    ArInputFrame input{AR_HEADER(ArInputFrame)};
    ArPhysicsInput selected{instance_, &input};
    ArPhysicsAdvanceDesc desc{AR_HEADER(ArPhysicsAdvanceDesc)};
    desc.request_id = ++request_;
    desc.elapsed_seconds = 1. / 30;
    desc.inputs = &selected;
    desc.input_count = 1;
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
