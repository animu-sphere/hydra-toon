// SPDX-License-Identifier: Apache-2.0
// The core's dirty routing (design policy §14): each kind of change advances
// only its own revision, and extraction drops what cannot be drawn.
#include <toon/extraction.hpp>
#include <toon/render_world.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace {

bool Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main() {
  Toon::RenderWorld world;
  const Toon::MeshId mesh = world.CreateMesh();
  world.SetMeshTopology(mesh, {0, 1, 2, 0, 2, 3});
  world.SetMeshPoints(mesh, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}});
  const Toon::FrameSnapshot first = world.Commit();
  const Toon::DrawList first_draws = Toon::ExtractDrawList(first);
  if (!Check(first.meshes.size() == 1 && first.meshes[0].index_bound == 4,
          "a committed mesh must carry its topology") ||
      !Check(first_draws.draws.size() == 1 &&
                 first_draws.triangle_count == 2,
          "a complete mesh must be drawn")) {
    return 1;
  }

  if (!Check(world.Commit().revision == first.revision,
          "a commit without changes must keep the revision")) {
    return 1;
  }

  world.SetMeshPoints(mesh, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}});
  const Toon::FrameSnapshot moved = world.Commit();
  if (!Check(moved.revision > first.revision, "a points edit must commit") ||
      !Check(moved.meshes[0].points_revision != first.meshes[0].points_revision,
          "a points edit must advance the points revision") ||
      !Check(moved.meshes[0].topology_revision ==
                 first.meshes[0].topology_revision,
          "a points edit must not advance the topology revision") ||
      !Check(moved.meshes[0].indices == first.meshes[0].indices,
          "a points edit must share the unchanged topology")) {
    return 1;
  }

  Toon::ToonView view;
  view.view.m[14] = -5.0F;
  world.SetView(view);
  const Toon::FrameSnapshot looked = world.Commit();
  if (!Check(looked.view_revision != moved.view_revision,
          "a camera edit must advance the view revision") ||
      !Check(looked.meshes[0].points_revision == moved.meshes[0].points_revision,
          "a camera edit must not touch geometry")) {
    return 1;
  }

  world.SetMetersPerUnit(0.01F);
  const Toon::FrameSnapshot centimetres = world.Commit();
  world.SetMetersPerUnit(0.0F);
  world.SetMetersPerUnit(-1.0F);
  const Toon::FrameSnapshot refused = world.Commit();
  if (!Check(looked.meters_per_unit == 1.0F,
          "a scene's unit must be a metre until set") ||
      !Check(centimetres.revision > looked.revision &&
                 centimetres.meters_per_unit == 0.01F &&
                 Toon::ExtractDrawList(centimetres).meters_per_unit == 0.01F,
          "a unit edit must commit and reach the draw list") ||
      !Check(centimetres.view_revision == looked.view_revision &&
                 centimetres.meshes[0].points_revision ==
                     looked.meshes[0].points_revision,
          "a unit edit must touch neither the camera nor geometry") ||
      !Check(refused.revision == centimetres.revision &&
                 refused.meters_per_unit == 0.01F,
          "a unit that is not positive must change nothing")) {
    return 1;
  }

  world.SetMeshTopology(mesh, {0, 1, 4});
  if (!Check(Toon::ExtractDrawList(world.Commit()).draws.empty(),
          "an index past the last point must not be drawn")) {
    return 1;
  }
  world.SetMeshTopology(mesh, {0, 1, 2});
  world.SetMeshVisible(mesh, false);
  if (!Check(Toon::ExtractDrawList(world.Commit()).draws.empty(),
          "an invisible mesh must not be drawn")) {
    return 1;
  }

  world.RemoveMesh(mesh);
  if (!Check(world.Commit().meshes.empty(), "a removed mesh must be gone")) {
    return 1;
  }

  // A material value edit rewrites its slot; only a structural edit
  // (material policy §8) advances the structure revision.
  const Toon::MaterialId material = world.CreateMaterial();
  const Toon::FrameSnapshot created = world.Commit();
  if (!Check(created.materials.size() == 1 &&
                 created.materials[0].material == Toon::ToonMaterial{},
          "a new material must be the fallback material")) {
    return 1;
  }
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  world.SetMaterial(material, toon);
  const Toon::MaterialSnapshot selected = world.Commit().materials[0];
  if (!Check(selected.structure_revision !=
                 created.materials[0].structure_revision,
          "a model change must be structural")) {
    return 1;
  }
  toon.mtoon.shading_shift = 0.2F;
  world.SetMaterial(material, toon);
  const Toon::FrameSnapshot shifted = world.Commit();
  if (!Check(shifted.materials[0].parameters_revision !=
                 selected.parameters_revision,
          "a value edit must advance the parameters revision") ||
      !Check(shifted.materials[0].structure_revision ==
                 selected.structure_revision,
          "a value edit must not be structural")) {
    return 1;
  }
  world.SetMaterial(material, toon);
  if (!Check(world.Commit().revision == shifted.revision,
          "setting the same values must not commit")) {
    return 1;
  }
  toon.alpha_mode = Toon::ToonAlphaMode::Mask;
  world.SetMaterial(material, toon);
  if (!Check(world.Commit().materials[0].structure_revision !=
                 shifted.materials[0].structure_revision,
          "an alpha mode change must be structural")) {
    return 1;
  }
  // MToon's render queue: Mask ignores the offset; Blend clamps it to its
  // side of the queue, and transparentWithZWrite draws before the rest.
  toon.mtoon.render_queue_offset = 5;
  const std::int32_t mask_queue = Toon::RenderQueue(toon);
  toon.alpha_mode = Toon::ToonAlphaMode::Blend;
  const std::int32_t blend_queue = Toon::RenderQueue(toon);
  const bool blend_depth = Toon::WritesDepth(toon);
  toon.mtoon.transparent_with_z_write = true;
  const std::int32_t z_write_queue = Toon::RenderQueue(toon);
  toon.mtoon.render_queue_offset = 12;
  if (!Check(mask_queue == 2450 && blend_queue == 3000 &&
                 z_write_queue == 2506 && Toon::RenderQueue(toon) == 2510,
          "the render queue must follow MToon's alpha mode and offset") ||
      !Check(!blend_depth && Toon::WritesDepth(toon) &&
                 Toon::IsTransparent(toon),
          "a Blend material must write depth only with transparentWithZWrite")) {
    return 1;
  }
  // The queue and depth writes are values: a draw's order and depth state
  // are recorded per frame from its slot's material, not built into it.
  world.SetMaterial(material, toon);
  const Toon::FrameSnapshot blended = world.Commit();
  toon.mtoon.render_queue_offset = -3;
  toon.mtoon.transparent_with_z_write = false;
  world.SetMaterial(material, toon);
  if (!Check(world.Commit().materials[0].structure_revision ==
                 blended.materials[0].structure_revision,
          "a render queue or depth write edit must not be structural")) {
    return 1;
  }
  world.RemoveMaterial(material);
  if (!Check(world.Commit().materials.empty(),
          "a removed material must be gone")) {
    return 1;
  }

  // Smooth normals follow the points and topology, counter-clockwise
  // triangles facing their normal, and nothing else advances them.
  const Toon::MeshId quad = world.CreateMesh();
  world.SetMeshTopology(quad, {0, 1, 2, 0, 2, 3});
  world.SetMeshPoints(quad, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}});
  const Toon::MeshSnapshot flat = world.Commit().meshes[0];
  bool facing = flat.normals != nullptr && flat.normals->size() == 4;
  for (std::size_t point = 0; facing && point < 4; ++point) {
    const Toon::Float3& normal = (*flat.normals)[point];
    facing = normal.x == 0.0F && normal.y == 0.0F && normal.z == 1.0F;
  }
  if (!Check(facing, "a flat quad's normals must face +z")) {
    return 1;
  }
  world.SetMeshColor(quad, {1, 0, 0});
  const Toon::MeshSnapshot recoloured = world.Commit().meshes[0];
  if (!Check(recoloured.normals_revision == flat.normals_revision &&
                 recoloured.normals == flat.normals,
          "a colour edit must not recompute normals")) {
    return 1;
  }
  world.SetMeshTopology(quad, {0, 2, 1, 0, 3, 2});
  const Toon::MeshSnapshot flipped = world.Commit().meshes[0];
  if (!Check(flipped.normals_revision != flat.normals_revision &&
                 (*flipped.normals)[0].z == -1.0F,
          "reversed winding must reverse the normals")) {
    return 1;
  }

  // Authored normals replace derived ones when there is one per point, do
  // not follow a points edit, and derived ones return without them.
  world.SetMeshNormals(quad, {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}});
  const Toon::MeshSnapshot authored = world.Commit().meshes[0];
  if (!Check(authored.authored_normals && !flipped.authored_normals &&
                 authored.normals_revision != flipped.normals_revision &&
                 (*authored.normals)[0].x == 1.0F,
          "authored normals must replace derived ones")) {
    return 1;
  }
  world.SetMeshPoints(quad, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}});
  world.SetMeshNormals(quad, {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}});
  const Toon::MeshSnapshot raised = world.Commit().meshes[0];
  if (!Check(raised.normals_revision == authored.normals_revision &&
                 raised.normals == authored.normals,
          "a points edit, or the same normals, must not re-send them")) {
    return 1;
  }
  world.SetMeshNormals(quad, {{1, 0, 0}});
  const Toon::MeshSnapshot short_normals = world.Commit().meshes[0];
  world.SetMeshNormals(quad, {});
  const Toon::MeshSnapshot derived = world.Commit().meshes[0];
  if (!Check(!short_normals.authored_normals &&
                 (*short_normals.normals)[0].z == -1.0F &&
                 !derived.authored_normals && derived.normals->size() == 4,
          "too few normals, or none, must derive them")) {
    return 1;
  }

  // A mesh binds a material by id; unbinding or an unknown id draws unlit.
  const Toon::MaterialId bound = world.CreateMaterial();
  world.SetMeshMaterial(quad, bound);
  const Toon::FrameSnapshot binding = world.Commit();
  if (!Check(binding.meshes[0].material == bound,
          "a bound material must reach the snapshot") ||
      !Check(Toon::ExtractDrawList(binding).materials.size() == 1,
          "extraction must carry the materials")) {
    return 1;
  }
  world.SetMeshMaterial(quad, bound);
  if (!Check(world.Commit().revision == binding.revision,
          "rebinding the same material must not commit")) {
    return 1;
  }
  world.SetMeshMaterial(quad, 0);
  if (!Check(world.Commit().meshes[0].material == 0,
          "unbinding must reach the snapshot")) {
    return 1;
  }

  // Texture coordinates have their own revision.
  const Toon::MeshSnapshot untextured = world.Commit().meshes[0];
  world.SetMeshUVs(quad, {{0, 0}, {1, 0}, {1, 1}, {0, 1}});
  const Toon::MeshSnapshot mapped = world.Commit().meshes[0];
  if (!Check(untextured.uvs != nullptr && untextured.uvs->empty(),
          "a mesh without texture coordinates must carry none") ||
      !Check(mapped.uvs_revision != untextured.uvs_revision &&
                 mapped.uvs->size() == 4,
          "a texture coordinate edit must advance its revision") ||
      !Check(mapped.points_revision == untextured.points_revision &&
                 mapped.normals_revision == untextured.normals_revision,
          "a texture coordinate edit must not touch the points")) {
    return 1;
  }

  // A texture's pixels have their own revision; a material that starts or
  // stops sampling one changes structurally, one that only moves its UV
  // transform does not (material policy §8).
  const Toon::TextureId texture = world.CreateTexture();
  const Toon::TextureSnapshot blank = world.Commit().textures[0];
  if (!Check(blank.texture.width == 0 && blank.texture.pixels->empty(),
          "a new texture must have no pixels")) {
    return 1;
  }
  Toon::ToonTexture texels;
  texels.width = 1;
  texels.height = 2;
  texels.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255});
  world.SetTexture(texture, texels);
  const Toon::FrameSnapshot painted = world.Commit();
  if (!Check(painted.textures[0].revision != blank.revision &&
                 painted.textures[0].texture.height == 2,
          "a pixel edit must advance the texture revision") ||
      !Check(Toon::ExtractDrawList(painted).textures.size() == 1,
          "extraction must carry the textures")) {
    return 1;
  }
  texels.height = 3;
  world.SetTexture(texture, texels);
  if (!Check(world.Commit().textures[0].texture.pixels->empty(),
          "pixels that do not fill the size must be dropped")) {
    return 1;
  }
  Toon::ToonMaterial textured;
  textured.model = Toon::ToonShadingModel::MToon;
  world.SetMaterial(bound, textured);
  const Toon::MaterialSnapshot plain = world.Commit().materials[0];
  textured.base_texture.texture = texture;
  world.SetMaterial(bound, textured);
  const Toon::MaterialSnapshot sampling = world.Commit().materials[0];
  if (!Check(sampling.structure_revision != plain.structure_revision,
          "sampling a texture must be structural")) {
    return 1;
  }
  textured.base_texture.offset = {0.5F, 0.0F};
  textured.base_texture.wrap_s = Toon::ToonWrap::ClampToEdge;
  world.SetMaterial(bound, textured);
  const Toon::MaterialSnapshot moved_uv = world.Commit().materials[0];
  if (!Check(moved_uv.parameters_revision != sampling.parameters_revision &&
                 moved_uv.structure_revision == sampling.structure_revision,
          "a UV transform or wrap edit must not be structural")) {
    return 1;
  }
  textured.mtoon.shade_texture.texture = texture;
  world.SetMaterial(bound, textured);
  const Toon::MaterialSnapshot shaded = world.Commit().materials[0];
  if (!Check(shaded.structure_revision != moved_uv.structure_revision,
          "sampling a shade texture must be structural")) {
    return 1;
  }
  // An outline's mode, width and colour are values; its width texture's
  // identity is structural, as every texture's is (material policy §8).
  textured.outline = true;
  textured.outline_width = 0.01F;
  textured.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  if (!Check(Toon::HasOutline(textured), "a positive finite width must draw a hull")) {
    return 1;
  }
  for (float invalid_width : {0.0F, -1.0F,
           std::numeric_limits<float>::infinity(),
           std::numeric_limits<float>::quiet_NaN()}) {
    auto invalid_outline = textured;
    invalid_outline.outline_width = invalid_width;
    if (!Check(!Toon::HasOutline(invalid_outline),
            "a zero, negative or non-finite width must draw no hull")) {
      return 1;
    }
  }
  world.SetMaterial(bound, textured);
  const Toon::MaterialSnapshot outlined = world.Commit().materials[0];
  if (!Check(outlined.parameters_revision != shaded.parameters_revision &&
                 outlined.structure_revision == shaded.structure_revision,
          "turning an outline on must not be structural")) {
    return 1;
  }
  textured.mtoon.outline_width_texture.texture = texture;
  world.SetMaterial(bound, textured);
  const Toon::MaterialSnapshot width_textured = world.Commit().materials[0];
  if (!Check(width_textured.structure_revision != outlined.structure_revision,
          "sampling an outline width texture must be structural")) {
    return 1;
  }
  // The rim's colour, shape and mix are values; its MatCap and multiply
  // textures' identities are structural.
  textured.mtoon.rim_color = {0.0F, 1.0F, 0.0F};
  textured.mtoon.rim_lift = 0.2F;
  textured.mtoon.rim_lighting_mix = 0.0F;
  textured.mtoon.matcap = {0.5F, 0.5F, 0.5F};
  world.SetMaterial(bound, textured);
  const Toon::MaterialSnapshot rimmed = world.Commit().materials[0];
  if (!Check(rimmed.parameters_revision != width_textured.parameters_revision &&
                 rimmed.structure_revision == width_textured.structure_revision,
          "a rim edit must not be structural")) {
    return 1;
  }
  textured.mtoon.matcap_texture.texture = texture;
  world.SetMaterial(bound, textured);
  const Toon::MaterialSnapshot matcapped = world.Commit().materials[0];
  if (!Check(matcapped.structure_revision != rimmed.structure_revision,
          "sampling a MatCap texture must be structural")) {
    return 1;
  }
  textured.mtoon.rim_multiply_texture.texture = texture;
  world.SetMaterial(bound, textured);
  if (!Check(world.Commit().materials[0].structure_revision !=
                 matcapped.structure_revision,
          "sampling a rim multiply texture must be structural")) {
    return 1;
  }
  world.RemoveTexture(texture);
  if (!Check(world.Commit().textures.empty(),
          "a removed texture must be gone")) {
    return 1;
  }

  // A skin is structural and a pose is not: each has its own revision, and
  // neither touches the points (design policy §11, §14).
  const Toon::MeshSnapshot rigid = world.Commit().meshes[0];
  if (!Check(!Toon::IsSkinned(rigid), "a new mesh must not be skinned")) {
    return 1;
  }
  Toon::ToonSkin skin;
  skin.influences_per_point = 2;
  skin.influences = {{0, 0.5F}, {2, 0.5F}, {0, 1.0F}, {1, 0.0F},
      {1, 1.0F}, {0, 0.0F}, {2, 1.0F}, {0, 0.0F}};
  world.SetMeshSkin(quad, skin);
  Toon::ToonSkinPose pose;
  pose.joints.resize(2);
  world.SetMeshSkinPose(quad, pose);
  const Toon::MeshSnapshot bound_skin = world.Commit().meshes[0];
  if (!Check(bound_skin.skin_revision != rigid.skin_revision &&
                 bound_skin.joint_bound == 3 &&
                 bound_skin.points_revision == rigid.points_revision,
          "a skin must advance its own revision and bound its joints") ||
      !Check(!Toon::IsSkinned(bound_skin),
          "a pose without every joint the skin names must not skin")) {
    return 1;
  }
  pose.joints.resize(3);
  world.SetMeshSkinPose(quad, pose);
  const Toon::MeshSnapshot posed = world.Commit().meshes[0];
  if (!Check(Toon::IsSkinned(posed), "a complete skin and pose must skin") ||
      !Check(posed.pose_revision != bound_skin.pose_revision &&
                 posed.skin_revision == bound_skin.skin_revision &&
                 posed.points_revision == bound_skin.points_revision &&
                 posed.normals_revision == bound_skin.normals_revision,
          "a pose edit must advance the pose revision alone")) {
    return 1;
  }
  const std::uint64_t steady = world.Commit().revision;
  world.SetMeshSkinPose(quad, pose);
  world.SetMeshSkin(quad, skin);
  if (!Check(world.Commit().revision == steady,
          "setting the skin and pose a mesh has must change nothing")) {
    return 1;
  }
  skin.influences.resize(6);
  world.SetMeshSkin(quad, skin);
  if (!Check(!Toon::IsSkinned(world.Commit().meshes[0]),
          "influences that miss a point the topology reaches must not "
          "skin")) {
    return 1;
  }
  skin.constant = true;
  skin.influences = {{2, 1.0F}, {0, 0.0F}};
  world.SetMeshSkin(quad, skin);
  if (!Check(Toon::IsSkinned(world.Commit().meshes[0]),
          "one constant set of influences must skin every point")) {
    return 1;
  }
  world.SetMeshSkin(quad, {});
  const Toon::MeshSnapshot unskinned = world.Commit().meshes[0];
  if (!Check(!Toon::IsSkinned(unskinned) && unskinned.influences->empty(),
          "an empty skin must unskin the mesh")) {
    return 1;
  }
  return 0;
}
