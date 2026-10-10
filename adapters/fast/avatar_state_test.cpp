// SPDX-License-Identifier: Apache-2.0
#include <toon/fast/avatar_state.hpp>
#include <toon/vulkan_backend.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
void Check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Values {
  ArJoint joints[2]{{"rig", "root", -1, {{0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1}}},
      {"rig", "eye", 0, {{0, .1, 0}, {0, 0, 0, 1}, {1, 1, 1}}}};
  ArBlendShape morphs[2]{{"face", "resolved.a", 0}, {"face", "resolved.b", 0}};
  ArMaterialInput materials[2]{{"surface", "owner:base", AR_VALUE_VEC4, 0, {.7, .3, .2, 1}},
      {"surface", "owner:shade", AR_VALUE_VEC3, 0, {.2, .1, .05, 0}}};
  ArVisibility visibility{"face", 1};
  ArCapability capability{"test.resolved", 1};
  ArStateView View(std::uint64_t frame = 1, std::uint64_t generation = 1) const {
    ArStateView s{AR_HEADER(ArStateView)};
    s.instance = 7;
    s.frame_id = frame;
    s.generation = generation;
    s.joints = joints;
    s.joint_count = 2;
    s.blend_shapes = morphs;
    s.blend_shape_count = 2;
    s.materials = materials;
    s.material_count = 2;
    s.visibility = &visibility;
    s.visibility_count = 1;
    s.layout_id = "test.avatar";
    s.layout_version = 1;
    s.input_revision = frame * 10;
    s.capabilities = &capability;
    s.capability_count = 1;
    return s;
  }
  void Sample(double t) {
    joints[0].local.translation[0] = .05 * t;
    joints[0].local.rotation[2] = std::sin(.1 * t);
    joints[0].local.rotation[3] = std::cos(.1 * t);
    joints[1].local.translation[0] = .02 * t;
    morphs[0].weight = -.2 * t;
    morphs[1].weight = .3 * t;
    materials[0].overridden = 1;
    materials[0].value[0] = .7 - .1 * t;
    materials[1].overridden = 1;
    materials[1].value[1] = .1 + .05 * t;
  }
};
struct Scene {
  Toon::RenderWorld world;
  Toon::MeshId mesh;
  Toon::MaterialId material;
  Toon::ToonMaterial values;
  explicit Scene(Toon::ToonAlphaMode mode = Toon::ToonAlphaMode::Opaque) {
    mesh = world.CreateMesh();
    material = world.CreateMaterial();
    world.SetMeshPoints(mesh, {{-.4F, -.4F, 0}, {.4F, -.4F, 0}, {0, .4F, 0}});
    world.SetMeshNormals(mesh, std::vector<Toon::Float3>(3, {0, 0, 1}));
    world.SetMeshTopology(mesh, {0, 1, 2, 2, 1, 0});
    Toon::ToonSkin skin;
    skin.constant = true;
    skin.influences_per_point = 1;
    skin.influences = {{0, 1}};
    world.SetMeshSkin(mesh, skin);
    world.SetMeshSkinPose(mesh, {{Toon::Matrix4{}, Toon::Matrix4{}}, {}});
    world.SetMeshMorph(mesh, {{{{.2F, 0, 0}, 0, {}, 0}, {{0, .2F, 0}, 1, {}, 0}}, {{0, 2}, {0, 2}, {0, 2}}});
    world.SetMeshMorphWeights(mesh, {0, 0});
    values.model = Toon::ToonShadingModel::MToon;
    values.alpha_mode = mode;
    values.base_color = {.7F, .3F, .2F};
    values.mtoon.shade_color = {.2F, .1F, .05F};
    values.outline = true;
    values.outline_width = .02F;
    values.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
    world.SetMaterial(material, values);
    world.SetMeshMaterial(mesh, material);
  }
  Toon::AvatarBindings Bindings(float unit = 1) const {
    Toon::AvatarBindings b;
    Toon::Matrix4 inverse;
    inverse.m[13] = -.1F / unit;
    b.skins = {{mesh, {1, 0}, {inverse, {}}, {}, {}}};
    b.morphs = {{0, mesh, 1}, {1, mesh, 0}};
    b.materials = {{0, material, Toon::AvatarMaterialField::BaseColor}, {1, material, Toon::AvatarMaterialField::ShadeColor}};
    b.visibility = {{0, mesh}};
    return b;
  }
};
struct Fake {
  static inline std::map<ArSnapshot, std::pair<ArStateView, int>> entries;
  static ArStatus AR_CALL Get(ArSnapshot h, ArStateView* v) {
    if (!entries.contains(h))
      return AR_INVALID_HANDLE;
    *v = entries.at(h).first;
    return AR_OK;
  }
  static ArStatus AR_CALL Retain(ArSnapshot h) {
    if (!entries.contains(h))
      return AR_INVALID_HANDLE;
    ++entries.at(h).second;
    return AR_OK;
  }
  static ArStatus AR_CALL Release(ArSnapshot h) {
    if (!entries.contains(h))
      return AR_INVALID_HANDLE;
    --entries.at(h).second;
    return AR_OK;
  }
  static ArRuntimeApi Api() {
    ArRuntimeApi a{AR_HEADER(ArRuntimeApi)};
    a.get_snapshot = Get;
    a.retain_snapshot = Retain;
    a.release_snapshot = Release;
    return a;
  }
  static Toon::RetainedAvatarSnapshot Snapshot(const ArStateView& v) {
    static ArSnapshot next = 1;
    const auto h = next++;
    entries[h] = {v, 0};
    Toon::RetainedAvatarSnapshot s;
    std::string error;
    Check(s.Reset(Api(), h, error), error.c_str());
    return s;
  }
};
void Slow(const Toon::FrameSnapshot& a, const Toon::FrameSnapshot& b) {
  const auto& x = a.meshes[0];
  const auto& y = b.meshes[0];
  Check(x.points == y.points && x.normals == y.normals && x.indices == y.indices && x.uvs == y.uvs &&
            x.influences == y.influences && x.morph_offsets == y.morph_offsets && x.morph_ranges == y.morph_ranges &&
            x.points_revision == y.points_revision && x.normals_revision == y.normals_revision &&
            x.topology_revision == y.topology_revision && x.skin_revision == y.skin_revision && x.morph_revision == y.morph_revision &&
            a.materials[0].structure_revision == b.materials[0].structure_revision,
      "runtime update changed static resources");
}
void SplitBaseColor() {
  Scene scene;
  Values v;
  v.materials[0] = {"surface", "owner:rgb", AR_VALUE_VEC3, 1, {.1, .2, .3, 0}};
  v.materials[1] = {"surface", "owner:alpha", AR_VALUE_SCALAR, 1, {.4, 0, 0, 0}};
  auto bindings = scene.Bindings();
  bindings.materials = {{0, scene.material, Toon::AvatarMaterialField::BaseColorRgb},
      {1, scene.material, Toon::AvatarMaterialField::Alpha}};
  auto base = scene.world.Commit();
  Toon::AvatarStateAdapter adapter;
  std::string error;
  Check(adapter.Bind(v.View(), base, 1, bindings, error), error.c_str());
  auto retained = Fake::Snapshot(v.View());
  Toon::FrameSnapshot output;
  Check(adapter.Apply(retained, base, 1, output, error), error.c_str());
  Check(output.materials[0].material.base_color == Toon::Float3{.1F, .2F, .3F} &&
            output.materials[0].material.alpha == .4F,
      "separate RGB/alpha mapping lost values");
  v.materials[0].overridden = 0;
  retained = Fake::Snapshot(v.View(2));
  Check(adapter.Apply(retained, base, 1, output, error) &&
            output.materials[0].material.base_color == scene.values.base_color &&
            output.materials[0].material.alpha == .4F,
      "RGB release cleared independently active alpha");
  v.materials[0].overridden = 1;
  v.materials[1].overridden = 0;
  retained = Fake::Snapshot(v.View(3));
  Check(adapter.Apply(retained, base, 1, output, error) &&
            output.materials[0].material.base_color == Toon::Float3{.1F, .2F, .3F} &&
            output.materials[0].material.alpha == scene.values.alpha,
      "alpha release cleared independently active RGB");
  auto layout = v.View();
  ArMaterialInput overlapping[3]{v.materials[0], v.materials[1],
      {"surface", "owner:rgba", AR_VALUE_VEC4, 1, {.2, .3, .4, .5}}};
  layout.materials = overlapping;
  layout.material_count = 3;
  bindings.materials.push_back({2, scene.material, Toon::AvatarMaterialField::BaseColor});
  Check(!adapter.Bind(layout, base, 2, bindings, error), "overlapping RGB/RGBA accepted");
  bindings.materials.erase(bindings.materials.begin() + 1);
  layout.materials = &overlapping[0];
  overlapping[1] = overlapping[2];
  layout.material_count = 2;
  bindings.materials.back().source = 1;
  Check(!adapter.Bind(layout, base, 2, bindings, error), "RGB/RGBA overlap accepted without alpha");
}
// Runtime revision 4 publishes two-component inputs as vec2; the former
// vec3-with-zero-z convention is refused rather than accepted as an alias.
void TextureTransform() {
  Scene scene;
  Values v;
  v.materials[0] = {"surface", "owner:uvOffset", AR_VALUE_VEC2, 1, {.25, .5, 0, 0}};
  v.materials[1] = {"surface", "owner:uvScale", AR_VALUE_VEC2, 1, {2, 3, 0, 0}};
  auto bindings = scene.Bindings();
  bindings.materials = {{0, scene.material, Toon::AvatarMaterialField::BaseTextureOffset},
      {1, scene.material, Toon::AvatarMaterialField::BaseTextureScale}};
  auto base = scene.world.Commit();
  Toon::AvatarStateAdapter adapter;
  std::string error;
  Check(adapter.Bind(v.View(), base, 1, bindings, error), error.c_str());
  auto retained = Fake::Snapshot(v.View());
  Toon::FrameSnapshot output;
  Check(adapter.Apply(retained, base, 1, output, error), error.c_str());
  const auto& texture = output.materials[0].material.base_texture;
  Check(texture.offset.x == .25F && texture.offset.y == .5F && texture.scale.x == 2 && texture.scale.y == 3,
      "vec2 texture transform lost values");
  v.materials[0].overridden = 0;
  retained = Fake::Snapshot(v.View(2));
  Check(adapter.Apply(retained, base, 1, output, error) &&
            output.materials[0].material.base_texture.offset.x == scene.values.base_texture.offset.x &&
            output.materials[0].material.base_texture.scale.y == 3,
      "offset release cleared independently active scale");
  auto legacy = v;
  legacy.materials[0] = {"surface", "owner:uvOffset", AR_VALUE_VEC3, 1, {.25, .5, 0, 0}};
  Check(!adapter.Bind(legacy.View(), base, 2, bindings, error), "vec3 texture offset accepted");
  legacy.materials[0] = {"surface", "owner:uvOffset", AR_VALUE_VEC2, 1, {.25, .5, 1, 0}};
  Check(!adapter.Bind(legacy.View(), base, 2, bindings, error), "nonzero unused vec2 component accepted");
}
// One shape with inbetweens at -0.5 and 0.5 fills three subshape slots the
// way usdSkelImaging does; a plain shape keeps its one slot. Expected values
// are worked by hand from the interpolation, not from the adapter.
void Inbetweens() {
  Toon::RenderWorld world;
  const auto mesh = world.CreateMesh();
  world.SetMeshPoints(mesh, {{-.4F, -.4F, 0}, {.4F, -.4F, 0}, {0, .4F, 0}});
  world.SetMeshTopology(mesh, {0, 1, 2});
  world.SetMeshMorph(mesh, {{{{.1F, 0, 0}, 0, {}, 0}, {{.2F, 0, 0}, 1, {}, 0}, {{.3F, 0, 0}, 2, {}, 0},
                                {{0, .1F, 0}, 3, {}, 0}},
                               {{0, 4}, {0, 4}, {0, 4}}});
  world.SetMeshMorphWeights(mesh, {.9F, .9F, .9F, .4F});
  const auto base = world.Commit();
  ArBlendShape shapes[2]{{"face", "smile", 0}, {"face", "plain", 0}};
  ArStateView view{AR_HEADER(ArStateView)};
  view.instance = 3;
  view.generation = 1;
  view.blend_shapes = shapes;
  view.blend_shape_count = 2;
  view.layout_id = "test.inbetweens";
  view.layout_version = 1;
  Toon::AvatarBindings bindings;
  // Slots follow usdSkelImaging's numbering: the shape's subshapes by weight.
  bindings.morphs = {{0, mesh, 2, {{1, .5F}, {0, -.5F}}}, {1, mesh, 3}};
  Toon::AvatarStateAdapter adapter;
  std::string error;
  Check(adapter.Bind(view, base, 1, bindings, error), error.c_str());
  struct Case {
    double smile;
    std::vector<float> expected;
  };
  const Case cases[]{{.25, {0, .5F, 0, .7F}}, {.5, {0, 1, 0, .7F}}, {.75, {0, .5F, .5F, .7F}},
      {1, {0, 0, 1, .7F}}, {1.5, {0, -1, 2, .7F}}, {0, {0, 0, 0, .7F}}, {-.25, {.5F, 0, 0, .7F}},
      {-1, {2, 0, 0, .7F}}};
  std::uint64_t frame = 0;
  for (const auto& c : cases) {
    shapes[0].weight = c.smile;
    shapes[1].weight = .7;
    view.frame_id = ++frame;
    auto held = Fake::Snapshot(view);
    Toon::FrameSnapshot output;
    Check(adapter.Apply(held, base, 1, output, error), error.c_str());
    Check(*output.meshes[0].morph_weights == c.expected, "inbetween subshape weights differ from usdSkelImaging's");
  }
  // Positions usdSkelImaging would drop, or slots another subshape owns, fail.
  auto refuse = [&](std::vector<Toon::AvatarInbetweenSlot> inbetweens, const char* message) {
    auto bad = bindings;
    bad.morphs[0].inbetweens = std::move(inbetweens);
    Check(!adapter.Bind(view, base, 2, bad, error) && !error.empty(), message);
  };
  refuse({{1, 0}}, "inbetween at the rest accepted");
  refuse({{1, 1}}, "inbetween at the primary accepted");
  refuse({{1, 1 + 5e-7F}}, "inbetween within 1e-6 of the primary accepted");
  refuse({{1, .5F}, {0, .5F}}, "coincident inbetweens accepted");
  refuse({{1, std::numeric_limits<float>::quiet_NaN()}}, "non-finite inbetween accepted");
  refuse({{4, .5F}}, "inbetween slot beyond the weights accepted");
  refuse({{2, .5F}}, "inbetween sharing the primary slot accepted");
  refuse({{3, .5F}}, "inbetween sharing another shape's slot accepted");
  Check(adapter.IdentityInfo().layout == "test.inbetweens", "failed rebind replaced the binding");
}
void State() {
  Scene scene;
  Values v;
  std::string error;
  auto base = scene.world.Commit();
  Toon::AvatarStateAdapter adapter;
  Check(adapter.Bind(v.View(), base, 1, scene.Bindings(), error), error.c_str());
  auto snapshot = Fake::Snapshot(v.View());
  const auto oldView = snapshot.View();
  Check(!snapshot.Reset(Fake::Api(), 99999, error) && snapshot.View() == oldView, "failed retain discarded previous snapshot");
  auto api = Fake::Api();
  api.abi_version = 1;
  Check(!snapshot.Reset(api, 1, error) && snapshot.View() == oldView, "wrong ABI accepted");
  Toon::RetainedAvatarSnapshot moved(std::move(snapshot));
  Check(!snapshot.View() && moved.View(), "move lost retention");
  Toon::FrameSnapshot output;
  Check(adapter.Apply(moved, base, 1, output, error), error.c_str());
  Check(output.meshes[0].pose_revision == base.meshes[0].pose_revision && output.meshes[0].morph_weights == base.meshes[0].morph_weights,
      "equal values wrote dynamic resources");
  Values changed;
  changed.Sample(1);
  auto s = Fake::Snapshot(changed.View(2));
  auto draws = Toon::ExtractDrawList(base);
  Check(adapter.Apply(s, base, 1, output, error) && Toon::ApplyFastSnapshot(output, draws, error), error.c_str());
  Slow(base, output);
  Check(*output.meshes[0].morph_weights == std::vector<float>{.3F, -.2F}, "subshape mapping or signed weight lost");
  Check(output.materials[0].material.base_color.x == .6F && output.materials[0].material.mtoon.shade_color.y == .15F,
      "material inputs not combined");
  Check(adapter.IdentityInfo().frame == 2 && adapter.IdentityInfo().input_revision == 20 && adapter.IdentityInfo().active,
      "runtime identity missing");
  const auto prior = output;
  Check(adapter.Apply(s, base, 1, output, error) && output.meshes[0].joints == prior.meshes[0].joints &&
            output.meshes[0].pose_revision == prior.meshes[0].pose_revision && output.materials[0].parameters_revision == prior.materials[0].parameters_revision,
      "duplicate frame caused dynamic writes");
  auto reject = [&](const ArStateView& bad, const Toon::FrameSnapshot& baseline, std::uint64_t epoch) {
    auto held = Fake::Snapshot(bad);
    const auto identity = adapter.IdentityInfo();
    Check(!adapter.Apply(held, baseline, epoch, output, error) && !error.empty(), "bad state accepted");
    Check(output.revision == prior.revision && output.meshes[0].joints == prior.meshes[0].joints &&
              output.materials[0].material == prior.materials[0].material && adapter.IdentityInfo().frame == identity.frame,
        "rejection was not atomic");
  };
  reject(changed.View(1), base, 1);
  auto bad = changed.View(3);
  bad.instance = 8;
  reject(bad, base, 1);
  bad = changed.View(3);
  bad.layout_version = 2;
  reject(bad, base, 1);
  bad = changed.View(3);
  bad.layout_id = "replacement";
  reject(bad, base, 1);
  reject(changed.View(3), base, 2);
  Values renamed = changed;
  renamed.morphs[1].target_id = "replacement";
  reject(renamed.View(3), base, 1);
  renamed = changed;
  renamed.joints[1].parent_index = -1;
  reject(renamed.View(3), base, 1);
  renamed = changed;
  renamed.materials[1].value_type = AR_VALUE_VEC4;
  reject(renamed.View(3), base, 1);
  bad = changed.View(3);
  bad.blend_shape_count = 1;
  reject(bad, base, 1);
  Values cap = changed;
  cap.capability.version = 2;
  reject(cap.View(3), base, 1);
  auto structural = base;
  structural.meshes[0].morph_revision++;
  reject(changed.View(3), structural, 1);
  structural = base;
  structural.meshes[0].points = std::make_shared<const std::vector<Toon::Float3>>(*base.meshes[0].points);
  reject(changed.View(3), structural, 1);
  structural = base;
  structural.materials[0].material.alpha_mode = Toon::ToonAlphaMode::Blend;
  reject(changed.View(3), structural, 1);
  // Malformed values are refused before exposing a retained view.
  auto refs = Fake::entries.at(1).second;
  Values invalid = changed;
  invalid.joints[0].local.rotation[3] = 2;
  Fake::entries[999] = {invalid.View(3), 0};
  Check(!moved.Reset(Fake::Api(), 999, error) && Fake::entries.at(999).second == 0 && Fake::entries.at(1).second == refs,
      "invalid snapshot leaked or replaced retention");
  invalid = changed;
  invalid.morphs[0].weight = std::numeric_limits<double>::infinity();
  Fake::entries[999] = {invalid.View(3), 0};
  Check(!moved.Reset(Fake::Api(), 999, error), "non-finite weight accepted");
  invalid = changed;
  invalid.materials[1].value[3] = 1;
  Fake::entries[999] = {invalid.View(3), 0};
  Check(!moved.Reset(Fake::Api(), 999, error), "unused material component accepted");
  // A reset advances generation without rebinding static resources.
  auto reset = Fake::Snapshot(changed.View(1, 2));
  Check(adapter.Apply(reset, base, 1, output, error), error.c_str());
  const auto resetOutput = output;
  auto stale = Fake::Snapshot(changed.View(99, 1));
  Check(!adapter.Apply(stale, base, 1, output, error) && output.revision == resetOutput.revision, "old generation accepted");
  // Complete material snapshots release individual fields to latest scene.
  changed.materials[0].overridden = 0;
  auto releasedField = Fake::Snapshot(changed.View(2, 2));
  scene.values.base_color = {.8F, .2F, .1F};
  scene.world.SetMaterial(scene.material, scene.values);
  auto current = scene.world.Commit();
  Check(adapter.Apply(releasedField, current, 1, output, error) && output.materials[0].material.base_color == scene.values.base_color &&
            output.materials[0].material.mtoon.shade_color.y == .15F,
      "field release did not restore latest baseline");
  Check(adapter.Release(current, 1, output, error) && !output.materials[0].parameters_overridden &&
            !output.meshes[0].morph_weights_overridden && !adapter.IdentityInfo().active,
      "release retained overrides");
  auto releaseRevision = output.revision;
  Check(adapter.Release(current, 1, output, error) && output.revision == releaseRevision, "repeated release wrote resources");
  // Visibility is resolved but requires extraction when membership changes.
  changed.visibility.visible = 0;
  auto hidden = Fake::Snapshot(changed.View(3, 2));
  Check(adapter.Apply(hidden, current, 1, output, error), error.c_str());
  draws = Toon::ExtractDrawList(current);
  Check(!Toon::ApplyFastSnapshot(output, draws, error) && Toon::ExtractDrawList(output).draws.empty(), "visibility was late-applied as value-only");
  // Failed rebind retains prior valid bindings; unsupported channels cannot vanish.
  auto bindings = scene.Bindings();
  bindings.materials.pop_back();
  Check(!adapter.Bind(changed.View(3, 2), current, 2, bindings, error), "unmapped material accepted");
  Check(adapter.Release(current, 1, output, error), "failed bind discarded prior binding");
  bindings = scene.Bindings();
  bindings.materials[1].field = Toon::AvatarMaterialField::Alpha;
  Check(!adapter.Bind(changed.View(3, 2), current, 2, bindings, error), "wrong material type accepted");
  // Units, parent hierarchy, reordered palette and explicit space conversions.
  Scene cm;
  cm.world.SetMetersPerUnit(.01F);
  auto cmBase = cm.world.Commit();
  Values posed;
  posed.Sample(1);
  auto cmBindings = cm.Bindings(.01F);
  cmBindings.skins[0].world_to_skeleton.m[12] = 10;
  cmBindings.skins[0].skeleton_to_mesh.m[12] = -10;
  Check(adapter.Bind(posed.View(), cmBase, 2, cmBindings, error), error.c_str());
  auto cmState = Fake::Snapshot(posed.View());
  Check(adapter.Apply(cmState, cmBase, 2, output, error), error.c_str());
  const auto& p = *output.meshes[0].joints;
  Check(std::abs(p[1].m[12] - 15) < 1e-5F && std::abs(p[0].m[12] - (15 + 2 * std::cos(.2))) < 1e-5 &&
            std::abs(p[0].m[13] - 2 * std::sin(.2)) < 1e-5 && output.meshes[0].skeleton_to_mesh.m[12] == -10,
      "pose hierarchy/unit/palette mapping incorrect");
  adapter.Clear();
  Check(!adapter.Apply(cmState, cmBase, 2, output, error), "cleared binding remained active");
}

ArStatus AR_CALL Evaluate(void*, void*, const ArEvaluationContext* c, const ArStateWriter* w) {
  Values values;
  values.Sample(c->input->evaluation_seconds);
  for (std::uint32_t i = 0; i < 2; ++i) {
    auto status = w->set_joint(w->context, i, &values.joints[i].local);
    if (status != AR_OK)
      return status;
    status = w->set_blend_shape(w->context, i, values.morphs[i].weight);
    if (status != AR_OK)
      return status;
    status = w->set_material(w->context, i, 1, values.materials[i].value);
    if (status != AR_OK)
      return status;
  }
  return w->set_visibility(w->context, 0, 1);
}
void Runtime(const char* path) {
#ifdef _WIN32
  const auto library = LoadLibraryA(path);
  Check(library != nullptr, "runtime DLL load failed");
  const auto get = reinterpret_cast<ArStatus(AR_CALL*)(std::uint32_t, std::uint32_t, ArRuntimeApi*)>(GetProcAddress(library, "arGetApi"));
#else
  const auto library = dlopen(path, RTLD_NOW);
  Check(library != nullptr, "runtime library load failed");
  const auto get = reinterpret_cast<ArStatus(AR_CALL*)(std::uint32_t, std::uint32_t, ArRuntimeApi*)>(dlsym(library, "arGetApi"));
#endif
  Check(get != nullptr, "runtime entry point missing");
  ArRuntimeApi api{AR_HEADER(ArRuntimeApi)};
  Check(get(AR_ABI_VERSION, sizeof(api), &api) == AR_OK, "runtime API negotiation failed");
  ArRuntime runtime = 0;
  Check(api.create_runtime(&runtime) == AR_OK, "runtime creation failed");
  ArEvaluatorDesc evaluator{AR_HEADER(ArEvaluatorDesc)};
  evaluator.id = "test.resolved";
  evaluator.provider_id = "test.provider";
  evaluator.provider_version = "1";
  evaluator.phase = AR_PHASE_APPEARANCE;
  evaluator.writes = AR_DOMAIN_ALL;
  evaluator.evaluate = Evaluate;
  Check(api.register_evaluator(runtime, &evaluator, nullptr) == AR_OK, "provider registration failed");
  const char* selected[] = {evaluator.id};
  Values values;
  ArInstanceDesc desc{AR_HEADER(ArInstanceDesc)};
  desc.generation = 1;
  desc.evaluators = selected;
  desc.evaluator_count = 1;
  desc.initial_state = values.View();
  desc.layout_id = "test.avatar";
  desc.layout_version = 1;
  ArInstance instance = 0;
  Check(api.create_instance(runtime, &desc, nullptr, &instance) == AR_OK, "instance creation failed");
  ArInputFrame input{AR_HEADER(ArInputFrame)};
  input.frame_id = 1;
  input.generation = 1;
  input.evaluation_seconds = 1;
  input.input_revision = 42;
  ArSnapshot handle = 0;
  Check(api.evaluate_frame(runtime, instance, &input, nullptr, &handle) == AR_OK, "runtime evaluation failed");
  std::string error;
  Toon::RetainedAvatarSnapshot old;
  Check(old.Reset(api, handle, error), error.c_str());
  Check(api.release_snapshot(handle) == AR_OK, "host reference release failed");
  Scene scene;
  auto base = scene.world.Commit();
  Toon::AvatarStateAdapter adapter;
  Toon::FrameSnapshot output;
  Check(adapter.Bind(*old.View(), base, 1, scene.Bindings(), error) && adapter.Apply(old, base, 1, output, error), error.c_str());
  Check(adapter.IdentityInfo().input_revision == 42 && std::abs((*output.meshes[0].morph_weights)[0] - .3F) < 1e-6F,
      "real runtime output mapping failed");
  Check(api.reset_instance(runtime, instance, 2) == AR_OK, "runtime reset failed");
  input.generation = 2;
  input.evaluation_seconds = 2;
  input.input_revision = 43;
  Check(api.evaluate_frame(runtime, instance, &input, nullptr, &handle) == AR_OK, "reset evaluation failed");
  Toon::RetainedAvatarSnapshot current;
  Check(current.Reset(api, handle, error), error.c_str());
  api.release_snapshot(handle);
  Check(adapter.Apply(current, base, 1, output, error), error.c_str());
  Check(!adapter.Apply(old, base, 1, output, error), "old retained generation accepted");
  Check(api.destroy_instance(runtime, instance) == AR_OK && api.destroy_runtime(runtime) == AR_OK, "runtime teardown failed");
  Check(old.View()->input_revision == 42 && current.View()->input_revision == 43 && old.View()->joints[0].local.translation[0] == .05,
      "snapshot lifetime depended on runtime or reset");
  Check(adapter.Apply(current, base, 1, output, error), error.c_str());
  current.Clear();
  old.Clear();
#ifdef _WIN32
  FreeLibrary(library);
#else
  dlclose(library);
#endif
}

void Gpu(const char* shaders) {
  for (const auto mode : {Toon::ToonAlphaMode::Opaque, Toon::ToonAlphaMode::Mask, Toon::ToonAlphaMode::Blend}) {
    Scene scene(mode), oracle(mode);
    auto base = scene.world.Commit();
    Values initial;
    Toon::AvatarStateAdapter adapter;
    std::string error;
    Check(adapter.Bind(initial.View(), base, 1, scene.Bindings(), error), error.c_str());
    Toon::FrameStatus status;
    std::string detail;
    Toon::RenderOptions options;
    options.samples = 4;
    auto renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, detail, options);
    Check(renderer != nullptr, detail.c_str());
    auto reference = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, detail, options);
    Check(reference != nullptr, detail.c_str());
    Toon::ColorProduct image, expected;
    Toon::DepthProduct depth, expectedDepth;
    auto draws = Toon::ExtractDrawList(base);
    struct LateHost {
      Toon::AvatarStateAdapter* adapter;
      const Toon::FrameSnapshot* base;
      const Toon::RetainedAvatarSnapshot* snapshot = nullptr;
      unsigned reads = 0;
      bool release = false;
      static bool Read(void* context, Toon::FrameSnapshot& latest, std::string& error) {
        auto& host = *static_cast<LateHost*>(context);
        ++host.reads;
        return host.release ? host.adapter->Release(*host.base, 1, latest, error) : host.adapter->Apply(*host.snapshot, *host.base, 1, latest, error);
      }
    } late{&adapter, &base};
    renderer->SetLateFrameSource({LateHost::Read, &late});
    Toon::OffscreenStatistics stats;
    for (std::uint64_t frame = 1; frame <= 12; ++frame) {
      const double t = static_cast<double>(frame - 1) * .1;
      Values v;
      v.Sample(t);
      auto view = v.View(frame);
      view.evaluation_seconds = t;
      auto held = Fake::Snapshot(view);
      late.snapshot = &held;
      // Independent analytic palette for root rotation and child translation.
      Toon::Matrix4 root;
      const float c = static_cast<float>(std::cos(.2 * t)), s = static_cast<float>(std::sin(.2 * t));
      root.m[0] = c;
      root.m[1] = s;
      root.m[4] = -s;
      root.m[5] = c;
      root.m[12] = static_cast<float>(.05 * t);
      auto child = root;
      child.m[12] += c * static_cast<float>(.02 * t);
      child.m[13] += s * static_cast<float>(.02 * t);
      oracle.world.SetMeshSkinPose(oracle.mesh, {{child, root}, {}});
      oracle.world.SetMeshMorphWeights(oracle.mesh, {static_cast<float>(.3 * t), static_cast<float>(-.2 * t)});
      auto material = oracle.values;
      material.base_color.x = static_cast<float>(.7 - .1 * t);
      material.mtoon.shade_color.y = static_cast<float>(.1 + .05 * t);
      oracle.world.SetMaterial(oracle.material, material);
      oracle.world.SetTimeSeconds(t);
      Check(renderer->Render(draws, 96, 96, image, depth, error) && reference->Render(Toon::ExtractDrawList(oracle.world.Commit()), 96, 96, expected, expectedDepth, error), error.c_str());
      Check(image.payload == expected.payload, "runtime mapped colour differs from analytic scene oracle");
      Check(depth.payload.size() == expectedDepth.payload.size(), "depth extent differs");
      for (std::size_t i = 0; i < depth.payload.size(); ++i)
        Check(std::abs(depth.payload[i] - expectedDepth.payload[i]) <= 1e-6F, "runtime mapped depth differs");
      const auto now = renderer->statistics();
      Check(late.reads == frame && now.late_samples_applied == frame, "runtime result missed backend late read");
      Check(now.validation_message_count == 0, "runtime GPU Vulkan validation message");
      if (frame > 1)
        Check(now.point_uploads == stats.point_uploads && now.topology_uploads == stats.topology_uploads && now.skin_uploads == stats.skin_uploads &&
                  now.morph_uploads == stats.morph_uploads && now.texture_uploads == stats.texture_uploads &&
                  now.pipelines_created == stats.pipelines_created && now.target_allocations == stats.target_allocations,
            "runtime value update rebuilt static GPU resources");
      if (frame > 1)
        Check(now.pose_writes == stats.pose_writes + 1 && now.morph_weight_writes == stats.morph_weight_writes + 1 &&
                  now.material_writes == stats.material_writes + 1,
            "runtime update missed affected dynamic writes");
      stats = now;
    }
    bool visible = false;
    for (std::size_t i = 4; i < image.payload.size(); i += 4)
      visible |= image.payload[i] != image.payload[0] || image.payload[i + 1] != image.payload[1] || image.payload[i + 2] != image.payload[2];
    Check(visible, "GPU comparison rendered only background colour");
    if (mode != Toon::ToonAlphaMode::Blend)
      Check(std::any_of(depth.payload.begin(), depth.payload.end(), [](float d) { return d < 1; }), "opaque GPU comparison wrote no depth");
    Values duplicate;
    duplicate.Sample(11 * .1);
    auto duplicateView = duplicate.View(12);
    duplicateView.evaluation_seconds = 11 * .1;
    auto held = Fake::Snapshot(duplicateView);
    late.snapshot = &held;
    Check(renderer->Render(draws, 96, 96, image, depth, error), error.c_str());
    auto same = renderer->statistics();
    Check(same.pose_writes == stats.pose_writes && same.morph_weight_writes == stats.morph_weight_writes &&
              same.material_writes == stats.material_writes,
        "held runtime frame wrote dynamic buffers again");
    late.release = true;
    Check(renderer->Render(draws, 96, 96, image, depth, error) && reference->Render(Toon::ExtractDrawList(base), 96, 96, expected, expectedDepth, error), error.c_str());
    Check(image.payload == expected.payload && depth.payload == expectedDepth.payload, "late release did not restore scene image");
    auto released = renderer->statistics();
    Check(released.pose_writes == same.pose_writes + 1 && released.morph_weight_writes == same.morph_weight_writes + 1 &&
              released.material_writes == same.material_writes + 1,
        "late release missed dynamic restoration");
    Check(renderer->Render(draws, 96, 96, image, depth, error), error.c_str());
    auto final = renderer->statistics();
    Check(final.pose_writes == released.pose_writes && final.morph_weight_writes == released.morph_weight_writes &&
              final.material_writes == released.material_writes && final.point_uploads == stats.point_uploads &&
              final.topology_uploads == stats.topology_uploads && final.validation_message_count == 0,
        "repeated release rewrote resources");
    renderer->SetLateFrameSource({});
  }
}
} // namespace
int main(int argc, char** argv) {
  try {
    SplitBaseColor();
    TextureTransform();
    Inbetweens();
    State();
    if (argc == 3 && std::string(argv[1]) == "--runtime")
      Runtime(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "--gpu") {
      if (!Toon::ProbeVulkanBackend().available)
        return 77;
      Gpu(argv[2]);
    }
    for (const auto& [id, entry] : Fake::entries) {
      (void)id;
      Check(entry.second == 0, "retained snapshot leaked");
    }
    std::cout << "avatar state adapter checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
