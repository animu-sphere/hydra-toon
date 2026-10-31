// SPDX-License-Identifier: Apache-2.0
#include <toon/fast/avatar_state.hpp>
#include <toon/vulkan_backend.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
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
void MmdMaterials(const char* shaders = nullptr) {
  using Field = Toon::AvatarMaterialField;
  Scene scene;
  scene.values.model = Toon::ToonShadingModel::MMD;
  scene.values.outline = false;
  const auto texture = scene.world.CreateTexture();
  Toon::ToonTexture texels; texels.width = texels.height = 1;
  texels.encoding = Toon::ToonTextureEncoding::Linear;
  texels.pixels = std::make_shared<const std::vector<std::uint8_t>>(std::vector<std::uint8_t>{64,128,192,255});
  scene.world.SetTexture(texture,texels);
  scene.world.SetMeshUVs(scene.mesh,{{0,0},{0,0},{0,0}});
  scene.values.base_texture.texture = texture;
  scene.values.mmd.sphere_texture.texture = texture;
  scene.values.mmd.sphere_mode = Toon::ToonSphereMode::Multiply;
  scene.values.mmd.toon_texture.texture = texture;
  scene.values.mmd.toon_source = Toon::ToonRampSource::Individual;
  scene.world.SetMaterial(scene.material,scene.values);
  const Field fields[]{Field::BaseColor,Field::MmdSpecularColor,Field::MmdSpecularPower,
      Field::MmdAmbientColor,Field::MmdEdgeColor,Field::MmdEdgeSize,
      Field::MmdTextureMultiply,Field::MmdTextureAdd,Field::MmdSphereMultiply,Field::MmdSphereAdd,
      Field::MmdToonMultiply,Field::MmdToonAdd};
  const std::uint32_t types[]{4,3,1,3,4,1,4,4,4,4,4,4};
  const char* names[]{"diffuse","specular","power","ambient","edge","size","tm","ta","sm","sa","rm","ra"};
  ArMaterialInput materials[12]{};
  auto bindings = scene.Bindings(); bindings.materials.clear();
  for (std::uint32_t i = 0; i < 12; ++i) {
    materials[i] = {"surface",names[i],types[i],1,{}};
    for (std::uint32_t a = 0; a < types[i]; ++a) materials[i].value[a] = (i+1+a)/32.;
    bindings.materials.push_back({i,scene.material,fields[i]});
  }
  Values values; auto view = values.View(); view.materials = materials; view.material_count = 12;
  auto base = scene.world.Commit();
  Toon::AvatarStateAdapter adapter; std::string error;
  Check(adapter.Bind(view,base,1,bindings,error),error.c_str());
  auto retained = Fake::Snapshot(view); Toon::FrameSnapshot output;
  Check(adapter.Apply(retained,base,1,output,error),error.c_str());
  auto expected = scene.values;
  const auto rgb = [&](int n) { return Toon::Float3{float(materials[n].value[0]),float(materials[n].value[1]),float(materials[n].value[2])}; };
  const auto rgba = [&](int n) { return Toon::Float4{float(materials[n].value[0]),float(materials[n].value[1]),float(materials[n].value[2]),float(materials[n].value[3])}; };
  expected.base_color = rgb(0); expected.alpha = float(materials[0].value[3]);
  expected.alpha_mode = Toon::ToonAlphaMode::Blend;
  expected.mmd.specular_color = rgb(1); expected.mmd.specular_power = float(materials[2].value[0]);
  expected.mmd.ambient_color = rgb(3); expected.outline_color = rgb(4); expected.mmd.edge_alpha = float(materials[4].value[3]);
  expected.outline_width = float(materials[5].value[0]);
  expected.mmd.texture_tint = {rgba(6),rgba(7)}; expected.mmd.sphere_tint = {rgba(8),rgba(9)};
  expected.mmd.toon_tint = {rgba(10),rgba(11)};
  Check(output.materials[0].material == expected,"MMD material mapping lost a component");
  Check(output.materials[0].structure_revision > base.materials[0].structure_revision,"MMD alpha draw membership did not invalidate");
  if (shaders) {
    Toon::FrameStatus status; std::string detail; Toon::RenderOptions options; options.samples = 4;
    auto renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders),status,detail,options);
    auto oracle = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders),status,detail,options);
    Check(renderer && oracle,detail.c_str());
    Toon::ColorProduct image,reference,disabled; Toon::DepthProduct depth,referenceDepth;
    auto draws = Toon::ExtractDrawList(output); draws.mmd_morph_tints = true;
    auto manual = output; manual.materials[0].material = expected;
    auto referenceDraws = Toon::ExtractDrawList(manual); referenceDraws.mmd_morph_tints = true;
    Check(renderer->Render(draws,96,96,image,depth,error) && oracle->Render(referenceDraws,96,96,reference,referenceDepth,error),error.c_str());
    Check(image.payload == reference.payload,"MMD runtime material GPU/reference parity");
    draws.mmd_morph_tints = false;
    Check(renderer->Render(draws,96,96,disabled,depth,error),error.c_str());
    Check(image.payload != disabled.payload,"MMD affine tint coefficients did not reach sampled textures");
    Check(renderer->statistics().validation_message_count == 0,"MMD GPU validation");
  }
  for (auto& m : materials) m.overridden = 0;
  ++view.frame_id; retained = Fake::Snapshot(view);
  Check(adapter.Apply(retained,base,1,output,error) && output.materials[0].material == scene.values,"MMD release did not restore baseline");
  // A translucent authored base can also be made opaque by a negative morph.
  auto translucent = base;
  translucent.materials[0].material.alpha = .5F;
  translucent.materials[0].material.alpha_mode = Toon::ToonAlphaMode::Blend;
  materials[0].overridden = 1;
  materials[0].value[3] = 1.25;
  Check(adapter.Bind(view,translucent,2,bindings,error),error.c_str());
  ++view.frame_id; retained = Fake::Snapshot(view);
  Check(adapter.Apply(retained,translucent,2,output,error) &&
      output.materials[0].material.alpha == 1.25F &&
      output.materials[0].material.alpha_mode == Toon::ToonAlphaMode::Opaque,
      "MMD diffuse override must classify an opaque result from a translucent base");
  materials[0].overridden = 0;
  ++view.frame_id; retained = Fake::Snapshot(view);
  Check(adapter.Apply(retained,translucent,2,output,error) &&
      output.materials[0].material == translucent.materials[0].material,
      "MMD diffuse release must restore translucent authored membership");
  auto wrong = base; wrong.materials[0].material.model = Toon::ToonShadingModel::MToon;
  Check(!adapter.Bind(view,wrong,2,bindings,error),"MMD-only fields accepted on MToon");
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
// A binding's scale multiplies what it writes, as the Hydra delegate scales
// `emissiveFactor` by `emissiveStrength`; release restores the scaled
// baseline, and a non-finite scale is refused.
void ScaledEmissive() {
  Scene scene;
  scene.values.emissive = {.2F, .1F, 0};
  scene.world.SetMaterial(scene.material, scene.values);
  Values v;
  v.materials[0] = {"surface", "owner:emission", AR_VALUE_VEC3, 1, {.3, .7, .1, 0}};
  auto bindings = scene.Bindings();
  bindings.materials = {{0, scene.material, Toon::AvatarMaterialField::Emissive, 2.5F},
      {1, scene.material, Toon::AvatarMaterialField::ShadeColor}};
  auto base = scene.world.Commit();
  Toon::AvatarStateAdapter adapter;
  std::string error;
  Check(adapter.Bind(v.View(), base, 1, bindings, error), error.c_str());
  auto retained = Fake::Snapshot(v.View());
  Toon::FrameSnapshot output;
  Check(adapter.Apply(retained, base, 1, output, error), error.c_str());
  const auto& emissive = output.materials[0].material.emissive;
  Check(emissive.x == .3F * 2.5F && emissive.y == .7F * 2.5F && emissive.z == .1F * 2.5F,
      "scaled emissive mapping lost its factor");
  v.materials[0].overridden = 0;
  retained = Fake::Snapshot(v.View(2));
  Check(adapter.Apply(retained, base, 1, output, error) &&
            output.materials[0].material.emissive == scene.values.emissive,
      "emissive release did not restore the baseline");
  bindings.materials[0].scale = std::numeric_limits<float>::infinity();
  Check(!adapter.Bind(v.View(), base, 2, bindings, error), "non-finite binding scale accepted");
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
  structural.meshes[0].tangents_revision++;
  reject(changed.View(3), structural, 1);
  structural = base;
  structural.meshes[0].tangents = std::make_shared<const std::vector<Toon::Float4>>();
  reject(changed.View(3), structural, 1);
  structural = base;
  structural.meshes[0].points = std::make_shared<const std::vector<Toon::Float3>>(*base.meshes[0].points);
  reject(changed.View(3), structural, 1);
  structural = base;
  structural.materials[0].material.alpha_mode = Toon::ToonAlphaMode::Blend;
  reject(changed.View(3), structural, 1);
  // Malformed values are refused before exposing a retained view.
#if AR_ABI_VERSION >= 5
  ArVertexUv unsupportedUv{"mesh", 0, {std::numeric_limits<double>::infinity(), .75}};
  auto uvState = changed.View(3);
  uvState.vertex_uvs = &unsupportedUv; uvState.vertex_uv_count = 1;
  Fake::entries[998] = {uvState, 0};
  Toon::RetainedAvatarSnapshot uvSnapshot;
  Check(!uvSnapshot.Reset(Fake::Api(), 998, error) && Fake::entries.at(998).second == 0,
      "invalid primary UV output was accepted or leaked retention");
#endif
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

#if AR_ABI_VERSION >= 5
struct UvScene {
  Toon::RenderWorld world;
  Toon::MeshId mesh;
  UvScene(Toon::ToonShadingModel model = Toon::ToonShadingModel::MMD) {
    mesh = world.CreateMesh();
    world.SetMeshPoints(mesh, {{-.8F, -.8F, 0}, {.8F, -.8F, 0}, {0, .8F, 0}, {0, .8F, 0}});
    world.SetMeshNormals(mesh, std::vector<Toon::Float3>(4, {0, 0, 1}));
    world.SetMeshTopology(mesh, {0, 1, 2, 3, 1, 0});
    world.SetMeshUVs(mesh, {{.1F, .2F}, {.1F, .2F}, {.1F, .2F}, {.1F, .2F}});
    auto image = world.CreateTexture();
    Toon::ToonTexture texture;
    texture.width = texture.height = 2;
    texture.encoding = Toon::ToonTextureEncoding::Linear;
    texture.pixels = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255});
    world.SetTexture(image, texture);
    auto material = world.CreateMaterial();
    Toon::ToonMaterial values;
    values.model = model;
    values.base_texture.texture = image;
    values.mtoon.shade_color = {1, 1, 1};
    world.SetMaterial(material, values);
    world.SetMeshMaterial(mesh, material);
  }
  Toon::AvatarBindings Bindings() const {
    Toon::AvatarBindings b;
    b.uvs = {{mesh, {1, 2, 0, 0}}}; // reordered runtime slots and split vertex
    return b;
  }
};
void PrimaryUvTangents(const char* shaders = nullptr) {
  UvScene scene(Toon::ToonShadingModel::MToon);
  auto& world = scene.world;
  const auto id = scene.mesh;
  world.SetMeshPoints(id, {{-.6F,-.6F,0},{.6F,-.6F,0},{.6F,.6F,0},{-.6F,.6F,0}});
  world.SetMeshTopology(id, {0,1,2,0,2,3});
  const std::vector<Toon::Float2> ordinary{{0,0},{1,0},{1,1},{0,1}};
  const std::vector<Toon::Float2> mirrored{{0,0},{1,0},{0,1},{1,1}};
  world.SetMeshUVs(id, ordinary);
  world.SetMeshTangents(id, std::vector<Toon::Float4>(4, {0,1,0,-1}));
  world.SetMeshEdgeScales(id, {1,2,3,4});
  world.SetMeshSubTextureUVs(id, ordinary);
  world.SetMeshSkin(id, {1, false, {{0,1},{1,1},{0,1},{1,1}}, {}});
  Toon::Matrix4 translated;
  translated.m[12] = .05F;
  world.SetMeshSkinPose(id, {{Toon::Matrix4{}, translated}, {}});
  world.SetMeshMorph(id, {{{{.05F,0,0},0,{},0}}, {{0,1},{0,0},{0,1},{0,0}}});
  world.SetMeshMorphWeights(id, {.5F});
  Toon::ToonTexture normal;
  normal.width = normal.height = 1;
  normal.encoding = Toon::ToonTextureEncoding::Linear;
  normal.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{191,230,204,255});
  const auto normalId = world.CreateTexture(); world.SetTexture(normalId, normal);
  auto material = world.Commit().materials[0].material;
  material.normal_texture.texture = normalId;
  world.SetMaterial(1, material);
  const auto base = world.Commit();
  ArVertexUv values[4]{{"mesh",2,{}},{"mesh",0,{}},{"mesh",3,{}},{"mesh",1,{}}};
  ArStateView view{AR_HEADER(ArStateView)};
  view.instance = view.generation = view.layout_version = 1;
  view.layout_id = "uv-tangents";
  view.vertex_uvs = values; view.vertex_uv_count = 4;
  Toon::AvatarBindings bindings;
  bindings.uvs = {{id, {1,3,0,2}}};
  Toon::AvatarStateAdapter adapter;
  Toon::FrameSnapshot output;
  std::string error;
  Check(adapter.Bind(view, base, 1, bindings, error), error.c_str());
  std::unique_ptr<Toon::OffscreenRenderer> renderer, reference;
  if (shaders) {
    Toon::FrameStatus status;
    renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, error);
    reference = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, error);
    Check(renderer && reference, error.c_str());
  }
  const std::vector<std::vector<Toon::Float2>> frames{ordinary, mirrored,
      {{0,0},{-1,0},{0,1},{-1,1}}, std::vector<Toon::Float2>(4), ordinary,
      {{0,0},{2,0},{2,1},{0,1}}};
  for (std::size_t frame = 0; frame < frames.size(); ++frame) {
    view.frame_id = frame + 1;
    for (auto& value : values) {
      value.value[0] = frames[frame][value.vertex_index].x;
      value.value[1] = frames[frame][value.vertex_index].y;
    }
    auto held = Fake::Snapshot(view);
    Check(adapter.Apply(held, base, 1, output, error), error.c_str());
    const bool seam = frame == 1 || frame == 2;
    const std::vector<unsigned> sources = seam ? std::vector<unsigned>{0,1,2,3,0,2} : std::vector<unsigned>{0,1,2,3};
    const std::vector<unsigned> indices = seam ? std::vector<unsigned>{0,1,2,4,5,3} : std::vector<unsigned>{0,1,2,0,2,3};
    const auto& mesh = output.meshes[0];
    Check(*mesh.indices == indices && mesh.points->size() == sources.size() && mesh.index_bound == sources.size(),
        "UV tangent seam topology differs");
    // Independent expected rest frames; no call to the tangent generator.
    std::vector<Toon::Float4> tangents;
    if (frame != 3) {
      for (std::size_t i = 0; i < sources.size(); ++i) {
        const bool negative = i == 3 || i >= 4;
        tangents.push_back(frame == 5 ? Toon::Float4{1,0,0,1} : !seam ? Toon::Float4{0,1,0,-1} :
            frame == 1 ? Toon::Float4{negative ? -1.F : 1.F,0,0,negative ? -1.F : 1.F} :
            Toon::Float4{negative ? 1.F : -1.F,0,0,negative ? 1.F : -1.F});
      }
    }
    Check(*mesh.tangents == tangents, "runtime UV tangent direction/handedness differs");
    if (frame == 5)
      Check(mesh.points == base.meshes[0].points && mesh.indices == base.meshes[0].indices &&
                mesh.influences == base.meshes[0].influences && mesh.morph_ranges == base.meshes[0].morph_ranges,
          "UV-only frame rebuilt unchanged geometry");
    for (std::size_t i = 0; i < sources.size(); ++i) {
      const auto source = sources[i];
      Check((*mesh.points)[i] == (*base.meshes[0].points)[source] &&
                (*mesh.normals)[i] == (*base.meshes[0].normals)[source] &&
                (*mesh.uvs)[i] == frames[frame][source] &&
                (*mesh.influences)[i] == (*base.meshes[0].influences)[source] &&
                (*mesh.morph_ranges)[i] == (*base.meshes[0].morph_ranges)[source] &&
                (*mesh.edge_scales)[i] == float(source + 1) &&
                (*mesh.sub_texture_uvs)[i] == ordinary[source], "UV seam lost a resident stream");
    }
    Check(mesh.morph_offsets == base.meshes[0].morph_offsets && mesh.parts == base.meshes[0].parts &&
              mesh.joints == base.meshes[0].joints, "UV seam changed non-vertex streams");
    const auto prior = output;
    Check(adapter.Apply(held, base, 1, output, error) &&
              output.revision == prior.revision && output.meshes[0].points == prior.meshes[0].points &&
              output.meshes[0].indices == prior.meshes[0].indices && output.meshes[0].tangents == prior.meshes[0].tangents &&
              output.meshes[0].influences == prior.meshes[0].influences && output.meshes[0].morph_ranges == prior.meshes[0].morph_ranges,
        "duplicate UV tangent frame rewrote streams");
    if (renderer) {
      // Independently expand an oracle snapshot; the renderer must upload
      // every changed stream on seam creation, rotation and removal.
      auto expected = base;
      auto& wanted = expected.meshes[0];
      auto expand = [&](const auto& array) {
        using T = typename std::remove_cvref_t<decltype(*array)>::value_type;
        std::vector<T> result;
        for (auto source : sources) result.push_back((*array)[source]);
        return std::make_shared<const std::vector<T>>(std::move(result));
      };
      wanted.points = expand(wanted.points); wanted.normals = expand(wanted.normals);
      wanted.influences = expand(wanted.influences); wanted.morph_ranges = expand(wanted.morph_ranges);
      wanted.edge_scales = expand(wanted.edge_scales); wanted.sub_texture_uvs = expand(wanted.sub_texture_uvs);
      auto uvArray = std::make_shared<const std::vector<Toon::Float2>>(frames[frame]);
      wanted.uvs = expand(uvArray);
      wanted.tangents = std::make_shared<const std::vector<Toon::Float4>>(tangents);
      wanted.indices = std::make_shared<const std::vector<unsigned>>(indices);
      wanted.index_bound = static_cast<unsigned>(sources.size());
      wanted.points_revision = wanted.normals_revision = wanted.topology_revision = wanted.uvs_revision =
          wanted.tangents_revision = wanted.skin_revision = wanted.morph_revision =
          wanted.edge_scales_revision = wanted.sub_texture_uvs_revision = 1000 + frame;
      Toon::ColorProduct image, wantedImage;
      Toon::DepthProduct depth, wantedDepth;
      Check(renderer->Render(Toon::ExtractDrawList(output),96,96,image,depth,error) &&
                reference->Render(Toon::ExtractDrawList(expected),96,96,wantedImage,wantedDepth,error), error.c_str());
      Check(image.payload == wantedImage.payload && depth.payload == wantedDepth.payload &&
                renderer->statistics().validation_message_count == 0, "UV seam GPU differs from independent streams");
    }
  }
  // A reset may introduce seams again; Release must restore all streams,
  // including their revisions, from the immutable authored baseline.
  view.generation = 2; view.frame_id = 1;
  for (auto& value : values) {
    value.value[0] = mirrored[value.vertex_index].x;
    value.value[1] = mirrored[value.vertex_index].y;
  }
  auto reset = Fake::Snapshot(view);
  Check(adapter.Apply(reset, base, 1, output, error), error.c_str());
  const auto seamFrame = output;
  Check(adapter.Release(base, 1, output, error) && output.meshes[0].points->size() == 4 &&
            *output.meshes[0].tangents == *base.meshes[0].tangents &&
            *output.meshes[0].influences == *base.meshes[0].influences &&
            *output.meshes[0].morph_ranges == *base.meshes[0].morph_ranges &&
            output.meshes[0].topology_revision > seamFrame.meshes[0].topology_revision &&
            output.meshes[0].tangents_revision > seamFrame.meshes[0].tangents_revision,
      "UV tangent release lost authored streams or revisions");
  const auto released = output;
  Check(adapter.Release(base, 1, output, error) && output.revision == released.revision &&
            output.meshes[0].tangents == released.meshes[0].tangents, "repeated tangent release rewrote arrays");
  auto incomplete = base;
  incomplete.meshes[0].edge_scales = std::make_shared<const std::vector<float>>(3, 1.F);
  Toon::AvatarStateAdapter invalid;
  Check(invalid.Bind(view, incomplete, 2, bindings, error), error.c_str());
  Check(!invalid.Apply(reset, incomplete, 2, output, error) && output.revision == released.revision &&
            output.meshes[0].points == released.meshes[0].points && !invalid.IdentityInfo().active,
      "incomplete seam streams partially published output/history");
  // A rigid skin stores only one influence set, even after splitting.
  auto constant = base;
  constant.meshes[0].constant_influences = true;
  constant.meshes[0].influences = std::make_shared<const std::vector<Toon::ToonJointInfluence>>(1, Toon::ToonJointInfluence{0,1});
  Check(invalid.Bind(view, constant, 3, bindings, error) && invalid.Apply(reset, constant, 3, output, error) &&
            output.meshes[0].points->size() == 6 && output.meshes[0].influences == constant.meshes[0].influences,
      "UV tangent seam expanded a constant skin");
  Check(seamFrame.meshes[0].points->size() == 6 && *seamFrame.meshes[0].indices == std::vector<unsigned>{0,1,2,4,5,3},
      "retained tangent seam frame mutated");
}
void DensePrimaryUvs() {
  constexpr std::uint32_t count = 32768;
  const std::string owners[]{"dense.mesh.a.with.a.long.opaque.owner.identity",
      "dense.mesh.b.with.a.long.opaque.owner.identity"};
  Toon::RenderWorld world;
  Toon::AvatarBindings bindings;
  std::vector<ArVertexUv> values;
  for (std::uint32_t m = 0; m < 2; ++m) {
    const auto mesh = world.CreateMesh();
    world.SetMeshPoints(mesh, std::vector<Toon::Float3>(count, {0, 0, 0}));
    world.SetMeshTopology(mesh, {0, 1, 2});
    std::vector<Toon::Float2> uvs;
    Toon::AvatarUvBinding binding{mesh, {}};
    for (std::uint32_t v = 0; v < count; ++v) {
      const Toon::Float2 uv{.25F + v / 65536.F, .75F};
      uvs.push_back(uv);
      values.push_back({owners[m].c_str(), v, {uv.x, uv.y}});
      binding.sources.push_back(2 * count - 1 - (m * count + v));
    }
    world.SetMeshUVs(mesh, uvs);
    bindings.uvs.push_back(std::move(binding));
  }
  // The layout order need not be mesh/vertex order; bindings retain the map.
  std::reverse(values.begin(), values.end());
  const auto base = world.Commit();
  ArStateView view{AR_HEADER(ArStateView)};
  view.instance = view.generation = view.frame_id = view.layout_version = 1;
  view.layout_id = "dense.uv.layout";
  view.vertex_uvs = values.data(); view.vertex_uv_count = static_cast<std::uint32_t>(values.size());
  Toon::AvatarStateAdapter adapter;
  Toon::FrameSnapshot output;
  std::string error;
  Check(adapter.Bind(view, base, 1, bindings, error), error.c_str());
  auto held = Fake::Snapshot(view);
  Check(adapter.Apply(held, base, 1, output, error), error.c_str());
  for (std::size_t m = 0; m < 2; ++m)
    Check(output.meshes[m].uvs == base.meshes[m].uvs &&
              output.meshes[m].uvs_revision == base.meshes[m].uvs_revision,
        "unchanged dense UVs lost baseline storage/revision");
  const auto unchanged = output;
  // Equal identities may have different storage in successive snapshots.
  const std::string copiedOwners[]{owners[0], owners[1]};
  for (auto& uv : values) {
    uv.mesh_id = copiedOwners[uv.mesh_id == owners[0] ? 0 : 1].c_str();
    uv.value[0] = -.125 - uv.vertex_index / 65536.;
  }
  ++view.frame_id;
  held = Fake::Snapshot(view);
  Check(adapter.Apply(held, base, 1, output, error), error.c_str());
  for (const auto& mesh : output.meshes)
    for (std::uint32_t v = 0; v < count; ++v)
      Check((*mesh.uvs)[v] == Toon::Float2{static_cast<float>(-.125 - v / 65536.), .75F},
          "dense UV mapping/value differs");
  const auto changed = output;
  auto reject = [&] {
    auto invalid = Fake::Snapshot(view);
    Check(!adapter.Apply(invalid, base, 1, output, error) &&
              output.revision == changed.revision && output.meshes[0].uvs == changed.meshes[0].uvs &&
              adapter.IdentityInfo().frame == 2, "dense UV refusal changed output/history");
  };
  ++view.frame_id;
  values.back().mesh_id = "dense.mesh.renamed.with.a.long.opaque.owner.identity";
  reject();
  values.back().mesh_id = copiedOwners[0].c_str();
  std::swap(values.front(), values[1]);
  reject();
  std::swap(values.front(), values[1]);
  // Restore through a new generation and check retained arrays independently.
  for (auto& uv : values) uv.value[0] = .25 + uv.vertex_index / 65536.;
  view.generation = 2; view.frame_id = 1;
  held = Fake::Snapshot(view);
  Check(adapter.Apply(held, base, 1, output, error), error.c_str());
  for (std::size_t m = 0; m < 2; ++m)
    Check(output.meshes[m].uvs == base.meshes[m].uvs &&
              unchanged.meshes[m].uvs == base.meshes[m].uvs &&
              (*changed.meshes[m].uvs)[count - 1].x == static_cast<float>(-.125 - (count - 1) / 65536.),
        "dense UV reset mutated retained output or failed to restore baseline");
}
void PrimaryUvs(const char* shaders = nullptr) {
  for (const auto model : {Toon::ToonShadingModel::MMD, Toon::ToonShadingModel::MToon}) {
    UvScene scene(model), oracle(model);
    const auto base = scene.world.Commit();
    ArVertexUv values[3]{{"mesh", 2, {.1, .2}}, {"mesh", 0, {.1, .2}}, {"mesh", 1, {.1, .2}}};
    auto view = Values{}.View();
    view.joints = nullptr;
    view.joint_count = 0;
    view.blend_shapes = nullptr;
    view.blend_shape_count = 0;
    view.materials = nullptr;
    view.material_count = 0;
    view.visibility = nullptr;
    view.visibility_count = 0;
    view.capabilities = nullptr;
    view.capability_count = 0;
    view.vertex_uvs = values;
    view.vertex_uv_count = 3;
    {
      std::vector<ArVertexUv> dense;
      for (const char* mesh : {"dense.a", "dense.b"})
        for (std::uint32_t vertex = 0; vertex < 32768; ++vertex)
          dense.push_back({mesh, vertex, {.25, .75}});
      auto layout = view;
      layout.vertex_uvs = dense.data(); layout.vertex_uv_count = static_cast<std::uint32_t>(dense.size());
      auto retained = Fake::Snapshot(layout);
      Check(retained.View() != nullptr, "ordered dense UV layout refused");
      std::reverse(dense.begin(), dense.end());
      retained = Fake::Snapshot(layout);
      Check(retained.View() != nullptr, "reverse dense UV layout refused");
      dense.back() = dense.front();
      Fake::entries[996] = {layout, 0};
      Toon::RetainedAvatarSnapshot malformed;
      std::string failure;
      Check(!malformed.Reset(Fake::Api(), 996, failure) && Fake::entries[996].second == 0,
          "nonadjacent dense UV duplicate accepted or leaked");
    }
    Toon::AvatarStateAdapter adapter;
    Toon::FrameSnapshot output;
    std::string error;
    auto bindings = scene.Bindings();
    Check(adapter.Bind(view, base, 1, bindings, error), error.c_str());
    // Every invalid binding must leave the previous binding usable.
    auto badBinding = [&](Toon::AvatarBindings b, Toon::FrameSnapshot s = {}) {
      Check(!adapter.Bind(view, s.meshes.empty() ? base : s, 2, b, error), "invalid UV binding accepted");
    };
    badBinding({});
    auto b = bindings;
    b.uvs.push_back(b.uvs[0]);
    badBinding(b);
    b = bindings;
    b.uvs[0].mesh = 999;
    badBinding(b);
    b = bindings;
    b.uvs[0].sources.pop_back();
    badBinding(b);
    b = bindings;
    b.uvs[0].sources[0] = 3;
    badBinding(b);
    b = bindings;
    b.uvs[0].sources = {0, 0, 1, 1};
    badBinding(b);
    auto malformed = base;
    malformed.meshes[0].uvs.reset();
    badBinding(bindings, malformed);
    malformed = base;
    malformed.meshes[0].index_bound = 5;
    badBinding(bindings, malformed);
    malformed = base;
    malformed.meshes[0].tangents = std::make_shared<const std::vector<Toon::Float4>>(3, Toon::Float4{1, 0, 0, 1});
    badBinding(bindings, malformed);
    values[0].vertex_index = 3;
    Check(!adapter.Bind(view, base, 2, bindings, error), "sparse UV vertex identities accepted");
    values[0].vertex_index = 2;
    values[0].mesh_id = "other";
    badBinding(bindings);
    values[0].mesh_id = "mesh";
    std::unique_ptr<Toon::OffscreenRenderer> renderer, reference;
    if (shaders) {
      Toon::FrameStatus status;
      renderer = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, error);
      reference = Toon::CreateOffscreenRenderer(Toon::SceneShadersIn(shaders), status, error);
      Check(renderer && reference, error.c_str());
    }
    std::vector<std::uint8_t> firstPixels;
    Toon::FrameSnapshot first;
    const double weights[]{0, 1.5, -.5, 0};
    for (std::size_t frame = 0; frame < 4; ++frame) {
      const auto w = weights[frame];
      for (auto& uv : values) {
        uv.value[0] = .1 + (.2 + .05 * uv.vertex_index) * w;
        uv.value[1] = .2 + (.15 + .025 * uv.vertex_index) * w;
      }
      view.frame_id = frame + 1;
      auto snapshot = Fake::Snapshot(view);
      Check(adapter.Apply(snapshot, base, 1, output, error), error.c_str());
      const auto& mesh = output.meshes[0];
      std::vector<Toon::Float2> expected;
      for (const unsigned source : {0U, 1U, 2U, 2U})
        expected.push_back({static_cast<float>(.1 + (.2 + .05 * source) * w),
            static_cast<float>(.2 + (.15 + .025 * source) * w)});
      Check(*mesh.uvs == expected, "UV source reordering/split mapping differs");
      Check(mesh.points == base.meshes[0].points && mesh.indices == base.meshes[0].indices &&
                mesh.topology_revision == base.meshes[0].topology_revision,
          "UV update rebuilt topology");
      const auto prior = output;
      Check(adapter.Apply(snapshot, base, 1, output, error) && output.meshes[0].uvs == prior.meshes[0].uvs &&
                output.meshes[0].uvs_revision == prior.meshes[0].uvs_revision,
          "duplicate UV frame rewrote arrays");
      auto draws = Toon::ExtractDrawList(base);
      Check(Toon::ApplyFastSnapshot(output, draws, error) == (frame == 0),
          "UV edits crossed late structural boundary");
      if (frame == 0)
        first = output;
      if (renderer) {
        oracle.world.SetMeshUVs(oracle.mesh, expected);
        Toon::ColorProduct image, wanted;
        Toon::DepthProduct depth, wantedDepth;
        Check(renderer->Render(Toon::ExtractDrawList(output), 96, 96, image, depth, error) &&
                  reference->Render(Toon::ExtractDrawList(oracle.world.Commit()), 96, 96, wanted, wantedDepth, error),
            error.c_str());
        Check(image.payload == wanted.payload && depth.payload == wantedDepth.payload, "UV pixels differ from independent scene");
        if (!frame)
          firstPixels = image.payload;
        else if (frame == 1)
          Check(image.payload != firstPixels, "UV edit did not change texture pixels");
        else if (frame == 3)
          Check(image.payload == firstPixels, "UV zero return did not restore pixels");
        Check(renderer->statistics().validation_message_count == 0, "UV GPU validation message");
      }
    }
    Check(*first.meshes[0].uvs == *base.meshes[0].uvs, "retained UV frame mutated");
    const auto prior = output;
    auto reject = [&](ArStateView bad, const Toon::FrameSnapshot& baseline) {
      auto held = Fake::Snapshot(bad);
      Check(!adapter.Apply(held, baseline, 1, output, error) && output.meshes[0].uvs == prior.meshes[0].uvs &&
                output.revision == prior.revision && adapter.IdentityInfo().frame == 4,
          "UV rejection was not atomic");
    };
    view.frame_id = 5;
    auto bad = view;
    bad.vertex_uv_count = 2;
    reject(bad, base);
    values[0].vertex_index = 3;
    reject(view, base);
    values[0].vertex_index = 2;
    values[0].mesh_id = "renamed";
    reject(view, base);
    values[0].mesh_id = "mesh";
    malformed = base;
    malformed.meshes[0].uvs_revision++;
    reject(view, malformed);
    reject(view, output); // an overridden snapshot is never the source baseline
    for (const double invalid : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),
             double(std::numeric_limits<float>::max()) * 2}) {
      values[0].value[0] = invalid;
      Fake::entries[997] = {view, 0};
      Toon::RetainedAvatarSnapshot held;
      Check(!held.Reset(Fake::Api(), 997, error) && Fake::entries[997].second == 0, "invalid UV retained or leaked");
    }
    values[0].value[0] = .8;
    view.frame_id = 1;
    view.generation = 2;
    auto reset = Fake::Snapshot(view);
    Check(adapter.Apply(reset, base, 1, output, error) && output.meshes[0].uvs_revision > prior.meshes[0].uvs_revision,
        "UV reset generation failed");
    Check(adapter.Release(base, 1, output, error) && *output.meshes[0].uvs == *base.meshes[0].uvs, "UV release failed");
    const auto released = output;
    Check(adapter.Release(base, 1, output, error) && output.meshes[0].uvs == released.meshes[0].uvs &&
              output.revision == released.revision,
        "repeated UV release rewrote arrays");
  }
}
#endif

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
    MmdMaterials();
    SplitBaseColor();
    TextureTransform();
    ScaledEmissive();
    Inbetweens();
    State();
#if AR_ABI_VERSION >= 5
    DensePrimaryUvs();
    PrimaryUvs();
    PrimaryUvTangents();
#endif
    if (argc == 3 && std::string(argv[1]) == "--runtime")
      Runtime(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "--gpu") {
      if (!Toon::ProbeVulkanBackend().available)
        return 77;
      Gpu(argv[2]);
      MmdMaterials(argv[2]);
#if AR_ABI_VERSION >= 5
      PrimaryUvs(argv[2]);
      PrimaryUvTangents(argv[2]);
#endif
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
