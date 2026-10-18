// SPDX-License-Identifier: Apache-2.0
// The core's dirty routing (design policy §14): each kind of change advances
// only its own revision, and extraction drops what cannot be drawn.
#include <toon/extraction.hpp>
#include <toon/render_world.hpp>

#include <algorithm>
#include <array>
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

using Parts = std::vector<Toon::MeshPart>;

const Parts& PartsOf(const Toon::FrameSnapshot& snapshot) {
  static const Parts none;
  return snapshot.meshes.empty() || snapshot.meshes[0].parts == nullptr
             ? none
             : *snapshot.meshes[0].parts;
}

// A mesh's face subsets divide its triangle list into parts: each subset
// with its own material, every gap with the mesh's, adjacent parts of one
// material merged; a range that is not whole triangles within the list, or
// overlaps an earlier one, is ignored.
int CheckSubsets() {
  Toon::RenderWorld world;
  const Toon::MeshId mesh = world.CreateMesh();
  // Four triangles.
  world.SetMeshTopology(mesh, {0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3});
  world.SetMeshPoints(mesh, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}});
  world.SetMeshMaterial(mesh, 7);
  const Toon::FrameSnapshot whole = world.Commit();
  if (!Check(PartsOf(whole) == Parts{{0, 12, 7}},
          "a mesh without subsets must be one part with its material")) {
    return 1;
  }

  // Subsets set in any order; the second triangle and the last are not in
  // one, and the mesh's own material draws them.
  world.SetMeshSubsets(mesh, {{6, 3, 9}, {0, 3, 8}});
  const Toon::FrameSnapshot divided = world.Commit();
  if (!Check(divided.revision > whole.revision &&
                 PartsOf(divided) ==
                     Parts{{0, 3, 8}, {3, 3, 7}, {6, 3, 9}, {9, 3, 7}},
          "subsets must divide the triangle list, gaps taking the mesh's "
          "material") ||
      !Check(divided.meshes[0].topology_revision ==
                     whole.meshes[0].topology_revision &&
                 divided.meshes[0].indices == whole.meshes[0].indices,
          "subsets must not touch the topology")) {
    return 1;
  }

  world.SetMeshSubsets(mesh, {{0, 3, 8}, {6, 3, 9}});
  const Toon::FrameSnapshot same = world.Commit();
  if (!Check(same.revision == divided.revision &&
                 same.meshes[0].parts == divided.meshes[0].parts,
          "equal subsets must change nothing")) {
    return 1;
  }

  // A subset bound to the mesh's own material merges with its neighbours.
  world.SetMeshMaterial(mesh, 9);
  if (!Check(PartsOf(world.Commit()) == Parts{{0, 3, 8}, {3, 9, 9}},
          "a mesh material edit must reach its gaps and merge them")) {
    return 1;
  }

  world.SetMeshSubsets(mesh, {{0, 0, 1}, {1, 3, 2}, {0, 4, 3}, {9, 6, 4},
                                 {3, 6, 5}, {6, 3, 6}});
  if (!Check(PartsOf(world.Commit()) == Parts{{0, 3, 9}, {3, 6, 5}, {9, 3, 9}},
          "an empty, misaligned, overlapping or overlong subset must be "
          "ignored")) {
    return 1;
  }

  // A shorter topology drops a subset past its end at the next commit.
  world.SetMeshSubsets(mesh, {{0, 3, 8}, {6, 3, 6}});
  world.SetMeshTopology(mesh, {0, 1, 2, 0, 2, 3});
  const Toon::FrameSnapshot shorter = world.Commit();
  if (!Check(PartsOf(shorter) == Parts{{0, 3, 8}, {3, 3, 9}},
          "a topology edit must divide its new triangle list again")) {
    return 1;
  }

  world.SetMeshSubsets(mesh, {});
  const Toon::FrameSnapshot cleared = world.Commit();
  const Toon::DrawList cleared_draws = Toon::ExtractDrawList(cleared);
  if (!Check(PartsOf(cleared) == Parts{{0, 6, 9}} &&
                 cleared_draws.draws.size() == 1 &&
                 cleared_draws.draws[0].parts == cleared.meshes[0].parts,
          "clearing subsets must draw the mesh whole again")) {
    return 1;
  }
  return 0;
}

// MMD's shared toon ramps (material policy MAT-Q2): the renderer's own,
// 32 x 32 and linear, toon01 white above its middle row and 205 grey
// below it, toon05 graded, and toon07-toon10 or an index outside 0-9 white.
int CheckSharedToonRamps() {
  const auto pixel = [](const Toon::ToonTexture& ramp, std::uint32_t column,
                         std::uint32_t row) {
    const std::size_t at = (row * ramp.width + column) * 4U;
    return std::array<std::uint8_t, 4>{(*ramp.pixels)[at],
        (*ramp.pixels)[at + 1U], (*ramp.pixels)[at + 2U],
        (*ramp.pixels)[at + 3U]};
  };
  using Pixel = std::array<std::uint8_t, 4>;
  const Toon::ToonTexture toon01 = Toon::SharedToonRamp(0);
  if (!Check(toon01.width == 32 && toon01.height == 32 &&
                 toon01.encoding == Toon::ToonTextureEncoding::Linear &&
                 toon01.pixels != nullptr &&
                 toon01.pixels->size() == 32U * 32U * 4U,
          "a shared ramp must be 32 x 32 linear RGBA8")) {
    return 1;
  }
  if (!Check(pixel(toon01, 0, 15) == Pixel{255, 255, 255, 255} &&
                 pixel(toon01, 31, 16) == Pixel{205, 205, 205, 255} &&
                 pixel(toon01, 7, 31) == Pixel{205, 205, 205, 255},
          "toon01 must be white above its middle row and 205 below it")) {
    return 1;
  }
  const Toon::ToonTexture toon05 = Toon::SharedToonRamp(4);
  if (!Check(pixel(toon05, 0, 17) == Pixel{255, 255, 255, 255} &&
                 pixel(toon05, 0, 24) == Pixel{255, 231, 221, 255} &&
                 pixel(toon05, 0, 20)[1] < 255 && pixel(toon05, 0, 20)[1] > 231,
          "toon05 must grade from white into its shade")) {
    return 1;
  }
  for (const std::int32_t index : {-1, 6, 9, 10}) {
    const Toon::ToonTexture white = Toon::SharedToonRamp(index);
    if (!Check(std::all_of(white.pixels->begin(), white.pixels->end(),
                   [](std::uint8_t value) { return value == 255; }),
            "toon07-toon10 and an index outside 0-9 must be white")) {
      return 1;
    }
  }
  return 0;
}

} // namespace

int main() {
  if (CheckSubsets() != 0 || CheckSharedToonRamps() != 0) {
    return 1;
  }
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
  world.SetMeshNormals(quad, {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}}, false);
  const auto adapter_normals = world.Commit().meshes[0];
  if (!Check(!adapter_normals.authored_normals &&
                 *adapter_normals.normals == *raised.normals,
          "adapter-derived normals must retain their values and provenance")) return 1;
  world.SetMeshNormals(quad, {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}}, false);
  if (!Check(world.Commit().meshes[0].normals_revision == adapter_normals.normals_revision,
          "unchanged adapter-derived normals must not resend")) return 1;
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

  // A mesh binds a material by id; unbinding or an unknown id uses display colour.
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

  world.SetMeshUVs(quad, {{0, 0}, {1, 0}, {1, 1}, {0, 1}});
  if (!Check(world.Commit().meshes[0].uvs_revision == mapped.uvs_revision,
          "setting the same texture coordinates must change nothing")) {
    return 1;
  }

  // MMD's edge scales have their own revision too, and setting the ones a
  // mesh already has changes nothing.
  world.SetMeshEdgeScales(quad, {1.0F, 0.5F, 0.5F, 1.0F});
  const Toon::MeshSnapshot scaled = world.Commit().meshes[0];
  world.SetMeshEdgeScales(quad, {1.0F, 0.5F, 0.5F, 1.0F});
  const Toon::MeshSnapshot rescaled = world.Commit().meshes[0];
  if (!Check(mapped.edge_scales != nullptr && mapped.edge_scales->empty(),
          "a mesh without edge scales must carry none") ||
      !Check(scaled.edge_scales_revision != mapped.edge_scales_revision &&
                 scaled.edge_scales->size() == 4 &&
                 scaled.uvs_revision == mapped.uvs_revision &&
                 scaled.points_revision == mapped.points_revision,
          "an edge scale edit must advance its own revision alone") ||
      !Check(rescaled.edge_scales_revision == scaled.edge_scales_revision,
          "setting the same edge scales must change nothing")) {
    return 1;
  }

  // So have MMD's sub-texture UVs.
  world.SetMeshSubTextureUVs(quad, {{0, 0}, {1, 0}, {1, 1}, {0, 1}});
  const Toon::MeshSnapshot subbed = world.Commit().meshes[0];
  world.SetMeshSubTextureUVs(quad, {{0, 0}, {1, 0}, {1, 1}, {0, 1}});
  const Toon::MeshSnapshot resubbed = world.Commit().meshes[0];
  if (!Check(mapped.sub_texture_uvs != nullptr &&
                 mapped.sub_texture_uvs->empty(),
          "a mesh without sub-texture UVs must carry none") ||
      !Check(subbed.sub_texture_uvs_revision !=
                     rescaled.sub_texture_uvs_revision &&
                 subbed.sub_texture_uvs->size() == 4 &&
                 subbed.uvs_revision == rescaled.uvs_revision &&
                 subbed.edge_scales_revision == rescaled.edge_scales_revision,
          "a sub-texture UV edit must advance its own revision alone") ||
      !Check(resubbed.sub_texture_uvs_revision ==
                 subbed.sub_texture_uvs_revision,
          "setting the same sub-texture UVs must change nothing")) {
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
  // MMD's edge is its outline request, drawn whatever MToon's width mode
  // says, as long as it has a finite width above zero.
  Toon::ToonMaterial edged;
  edged.model = Toon::ToonShadingModel::MMD;
  edged.outline = true;
  edged.outline_width = 0.4F;
  if (!Check(Toon::HasOutline(edged), "an MMD edge must draw a hull")) {
    return 1;
  }
  for (float invalid_width : {0.0F, -1.0F,
           std::numeric_limits<float>::infinity(),
           std::numeric_limits<float>::quiet_NaN()}) {
    auto invalid_edge = edged;
    invalid_edge.outline_width = invalid_width;
    if (!Check(!Toon::HasOutline(invalid_edge),
            "a zero, negative or non-finite edge size must draw no hull")) {
      return 1;
    }
  }
  edged.outline = false;
  if (!Check(!Toon::HasOutline(edged),
          "an MMD material that draws no edge must draw no hull")) {
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
  const auto before_time = world.Commit();
  world.SetTimeSeconds(1.25);
  const auto timed = world.Commit();
  if (!Check(timed.time_seconds == 1.25 &&
                 timed.revision != before_time.revision &&
                 timed.meshes[0].points_revision == before_time.meshes[0].points_revision &&
                 timed.materials[0].parameters_revision == before_time.materials[0].parameters_revision &&
                 Toon::ExtractDrawList(timed).time_seconds == 1.25,
          "time must reach extraction without geometry or material changes")) {
    return 1;
  }
  world.SetTimeSeconds(1.25);
  world.SetTimeSeconds(std::numeric_limits<double>::infinity());
  if (!Check(world.Commit().revision == timed.revision,
          "unchanged or invalid time must change nothing")) {
    return 1;
  }
  const Toon::ToonMaterial defaults;
  auto preview = defaults;
  preview.preview.enabled = true;
  if (!Check(Toon::IsStructuralChange(defaults, preview), "enabling PreviewSurface changes the draw pipeline")) return 1;
  auto preview_values = preview;
  preview_values.preview.metallic = 0.8F;
  preview_values.preview.roughness = 0.2F;
  preview_values.preview.normal = {0.2F, -0.3F, 0.8F};
  preview_values.preview.inputs[5].connected = true;
  preview_values.preview.inputs[5].scale = {2, 2, 2, 1};
  preview_values.preview.inputs[0].scale.x = 2;
  preview_values.preview.inputs[1].bias.y = 0.1F;
  if (!Check(preview_values != preview && !Toon::IsStructuralChange(preview, preview_values),
          "PreviewSurface metallic/roughness edits must be value-only")) return 1;
  for (auto role : {&Toon::ToonMaterial::PreviewSurface::metallic_texture,
                    &Toon::ToonMaterial::PreviewSurface::roughness_texture,
                    &Toon::ToonMaterial::PreviewSurface::opacity_texture}) {
    auto textured = preview;
    (textured.preview.*role).texture = 1;
    if (!Check(Toon::IsStructuralChange(preview, textured), "PreviewSurface texture identity must be structural")) return 1;
    auto transformed = textured;
    (transformed.preview.*role).offset.x = 0.5F;
    if (!Check(!Toon::IsStructuralChange(textured, transformed), "PreviewSurface UV edits must be value-only")) return 1;
  }
  const auto before_light = world.Commit();
  const auto light_id = world.CreateLight();
  Toon::ToonLight light;
  light.type = Toon::ToonLightType::Spot;
  light.direction = {0, 0, -2};
  world.SetLight(light_id, light);
  const auto lit_scene = world.Commit();
  if (!Check(lit_scene.lights.size() == 1 &&
                 lit_scene.lights[0].light.direction == Toon::Float3{0, 0, -1} &&
                 Toon::ExtractDrawList(lit_scene).lights[0].id == light_id &&
                 lit_scene.meshes[0].points_revision == before_light.meshes[0].points_revision &&
                 lit_scene.materials[0].parameters_revision == before_light.materials[0].parameters_revision,
          "lights must reach extraction without touching geometry or materials"))
    return 1;
  world.SetLight(light_id, light);
  light.color.x = std::numeric_limits<float>::quiet_NaN();
  world.SetLight(light_id, light);
  if (!Check(world.Commit().revision == lit_scene.revision,
          "unchanged or invalid light values must do nothing"))
    return 1;
  light = lit_scene.lights[0].light;
  light.visible = false;
  world.SetLight(light_id, light);
  const auto hidden_light = world.Commit();
  if (!Check(!hidden_light.lights[0].light.visible &&
                 hidden_light.lights[0].revision != lit_scene.lights[0].revision,
          "light visibility must advance its revision"))
    return 1;
  world.RemoveLight(light_id);
  if (!Check(world.Commit().lights.empty(), "removed lights must leave snapshots"))
    return 1;
  for (int role = 0; role < 6; ++role) {
    auto changed = defaults;
    const std::array<Toon::ToonTextureRef*, 6> refs{&changed.emissive_texture,
        &changed.normal_texture, &changed.mtoon.shading_shift_texture,
        &changed.mtoon.uv_animation_mask_texture, &changed.mmd.sphere_texture,
        &changed.mmd.toon_texture};
    refs[role]->rotation = 1.0F;
    if (!Check(!Toon::IsStructuralChange(defaults, changed),
            "a new role's transform is value-only")) {
      return 1;
    }
    refs[role]->texture = 1;
    if (!Check(Toon::IsStructuralChange(defaults, changed),
            "a new role's texture identity is structural")) {
      return 1;
    }
  }
  auto tinted = defaults;
  tinted.mmd.texture_tint.multiply = {0.0F, 0.0F, 0.0F, 0.0F};
  tinted.mmd.sphere_tint.add = {1.0F, 1.0F, 1.0F, 1.0F};
  tinted.mmd.toon_tint.multiply = {3.0F, 3.0F, 3.0F, 3.0F};
  if (!Check(tinted != defaults && !Toon::IsStructuralChange(defaults, tinted),
          "an MMD material morph's tints are value-only")) {
    return 1;
  }
  return 0;
}
