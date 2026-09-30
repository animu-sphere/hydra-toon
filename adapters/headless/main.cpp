// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>
#include <toon/render_world.hpp>
#include <toon/vulkan_backend.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

// Process id, for a session identifier that is unique among concurrent runs.
long long CurrentProcessId() {
#if defined(_WIN32)
  return static_cast<long long>(_getpid());
#else
  return static_cast<long long>(getpid());
#endif
}

struct Check {
  std::string id;
  std::string status;
  std::string detail;
};

std::string Escape(std::string_view value) {
  std::string escaped;
  for (const char character : value) {
    switch (character) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped += character;
      break;
    }
  }
  return escaped;
}

// The producing invocation behind this report. OpenStrata refuses a PASS that
// no completed producer stands behind, so the report records who wrote it,
// against what, and whether the run actually finished.
struct Session {
  std::string id;
  std::string target;
  long long started = 0;
  long long completed = 0;
  // Whether this harness reached its verdicts, not whether the verdicts were
  // PASS. A run that decided every check succeeded, however many of them
  // failed; only a run that could not produce checks at all is a failure.
  bool succeeded = false;
};

bool WriteReport(const std::string& path,
    const std::vector<Check>& checks,
    const Toon::GpuFrameEvidence& frame,
    const Session& session) {
  // Write to a sibling temp file and rename into place, so a run killed
  // mid-write leaves no partial overlay for `ost renderer merge` to pick up.
  const std::filesystem::path final_path(path);
  std::filesystem::path temp_path = final_path;
  temp_path += ".tmp";
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    output << "{\n"
           << "  \"schema\": \"openstrata.renderer-report/v1alpha1\",\n"
           << "  \"renderer\": {\"name\": \"toon\"},\n"
           << "  \"producer\": {\"id\":\"" << Escape(session.id)
           << "\",\"kind\":\"renderer-harness\",\"target\":\""
           << Escape(session.target) << "\",\"started_unix\":" << session.started
           << ",\"completed_unix\":" << session.completed << ",\"outcome\":\""
           << (session.succeeded ? "success" : "failure") << "\"},\n";
    const Toon::OffscreenStatistics& device = frame.statistics;
    if (!device.device_name.empty()) {
      output << "  \"device\": {\"backend\":\"vulkan\",\"name\":\""
             << Escape(device.device_name) << "\",\"api_version\":\""
             << Escape(device.api_version) << "\",\"driver_version\":\""
             << Escape(device.driver_version) << "\",\"vendor_id\":"
             << device.vendor_id << ",\"device_id\":" << device.device_id
             << "},\n";
    }
    output << "  \"checks\": [\n";
    for (std::size_t index = 0; index < checks.size(); ++index) {
      const Check& check = checks[index];
      output << "    {\"id\":\"" << Escape(check.id) << "\",\"status\":\""
             << Escape(check.status) << "\"";
      if (!check.detail.empty()) {
        output << ",\"detail\":\"" << Escape(check.detail) << "\"";
      }
      output << '}' << (index + 1 == checks.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    if (!output.good()) {
      return false;
    }
  }
  std::error_code error;
  std::filesystem::rename(temp_path, final_path, error);
  if (error) {
    std::filesystem::remove(temp_path, error);
    return false;
  }
  return true;
}

// One pixel of a colour product, RGBA; row 0 is the top.
std::array<std::uint8_t, 4> PixelAt(const Toon::ColorProduct& color,
    std::uint32_t column, std::uint32_t row) {
  const std::size_t offset =
      static_cast<std::size_t>(row) * color.row_pitch + column * 4U;
  if (offset + 3U >= color.payload.size()) {
    return {};
  }
  return {color.payload[offset], color.payload[offset + 1U],
      color.payload[offset + 2U], color.payload[offset + 3U]};
}

// The centre pixel of a colour product, RGBA.
std::array<std::uint8_t, 4> CenterPixel(const Toon::ColorProduct& color) {
  return PixelAt(color, color.width / 2U, color.height / 2U);
}

// mtoon_opaque (material policy §7): the bootstrap triangle bound to an
// MToon material whose lit colour is red and shade colour blue. Facing the
// key light it draws lit; a shading shift of -1, a value-only edit, turns it
// to shade by rewriting one parameter slot, with no pipeline and no upload.
Check MToonOpaqueCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.material.mtoon_opaque";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (renderer == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }

  Toon::RenderWorld world;
  world.SetBootstrapTriangle();
  const Toon::MeshId mesh = world.Commit().meshes.front().id;
  const Toon::MaterialId material = world.CreateMaterial();
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  toon.base_color = {1.0F, 0.0F, 0.0F};
  toon.mtoon.shade_color = {0.0F, 0.0F, 1.0F};
  world.SetMaterial(material, toon);
  world.SetMeshMaterial(mesh, material);

  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  if (!renderer->Render(Toon::ExtractDrawList(world.Commit()), 64, 64, color,
          depth, detail)) {
    return {id, "fail", detail};
  }
  const std::array<std::uint8_t, 4> lit = CenterPixel(color);
  const Toon::OffscreenStatistics first = renderer->statistics();

  toon.mtoon.shading_shift = -1.0F;
  world.SetMaterial(material, toon);
  if (!renderer->Render(Toon::ExtractDrawList(world.Commit()), 64, 64, color,
          depth, detail)) {
    return {id, "fail", detail};
  }
  const std::array<std::uint8_t, 4> shaded = CenterPixel(color);
  const Toon::OffscreenStatistics& second = renderer->statistics();

  // Its own renderer, so its own validation capture.
  if (second.validation_message_count != 0) {
    return {id, "fail", second.validation_detail};
  }
  if (lit[0] < 200U || lit[2] > 50U) {
    return {id, "fail", "the lit side did not draw the base colour"};
  }
  if (shaded[2] < 200U || shaded[0] > 100U) {
    return {id, "fail", "a shading shift of -1 did not draw the shade colour"};
  }
  if (first.material_writes != 1 || second.material_writes != 2 ||
      second.pipelines_created != first.pipelines_created ||
      second.point_uploads != first.point_uploads ||
      second.topology_uploads != first.topology_uploads) {
    return {id, "fail",
        "a value-only material edit must rewrite one slot and nothing else"};
  }
  return {id, "pass", ""};
}

// mtoon_opaque's textures: the bootstrap triangle, every corner at st
// (0.25, 0.75), samples a 2x2 sRGB texture whose top left is red, top right
// green, bottom left blue and bottom right white. Red shows that st's
// bottom-left origin reaches the image's top-left rows; an offset of
// (0.5, 0), a value-only edit, moves the lit side to green with no upload;
// the same texture as the shade texture, lit fully from behind by a shading
// shift of -1 and offset (0, 0.5), draws blue without a second upload.
Check MToonTexturedCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.material.mtoon_textured";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (renderer == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }

  Toon::RenderWorld world;
  world.SetBootstrapTriangle();
  const Toon::MeshId mesh = world.Commit().meshes.front().id;
  world.SetMeshUVs(mesh, {{0.25F, 0.75F}, {0.25F, 0.75F}, {0.25F, 0.75F}});
  const Toon::TextureId texture = world.CreateTexture();
  Toon::ToonTexture texels;
  texels.width = 2;
  texels.height = 2;
  texels.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255,
          255, 255, 255, 255, 255});
  world.SetTexture(texture, texels);
  const Toon::MaterialId material = world.CreateMaterial();
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  toon.base_texture.texture = texture;
  world.SetMaterial(material, toon);
  world.SetMeshMaterial(mesh, material);

  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  const auto render = [&](std::array<std::uint8_t, 4>& pixel,
                          Toon::OffscreenStatistics& statistics) {
    if (!renderer->Render(Toon::ExtractDrawList(world.Commit()), 64, 64,
            color, depth, detail)) {
      return false;
    }
    pixel = CenterPixel(color);
    statistics = renderer->statistics();
    return true;
  };
  std::array<std::uint8_t, 4> red{};
  std::array<std::uint8_t, 4> green{};
  std::array<std::uint8_t, 4> blue{};
  Toon::OffscreenStatistics first;
  Toon::OffscreenStatistics second;
  Toon::OffscreenStatistics third;
  if (!render(red, first)) {
    return {id, "fail", detail};
  }
  toon.base_texture.offset = {0.5F, 0.0F};
  world.SetMaterial(material, toon);
  if (!render(green, second)) {
    return {id, "fail", detail};
  }
  toon.mtoon.shading_shift = -1.0F;
  toon.mtoon.shade_texture.texture = texture;
  toon.mtoon.shade_texture.offset = {0.0F, 0.5F};
  world.SetMaterial(material, toon);
  if (!render(blue, third)) {
    return {id, "fail", detail};
  }

  if (third.validation_message_count != 0) {
    return {id, "fail", third.validation_detail};
  }
  if (red[0] < 200U || red[1] > 50U || red[2] > 50U) {
    return {id, "fail", "st (0.25, 0.75) did not sample the top-left texel"};
  }
  if (green[1] < 200U || green[0] > 50U || green[2] > 50U) {
    return {id, "fail", "a UV offset of (0.5, 0) did not sample the top right"};
  }
  if (blue[2] < 200U || blue[0] > 50U) {
    return {id, "fail", "the shade texture did not draw the shade side"};
  }
  if (first.texture_uploads != 1 || third.texture_uploads != 1 ||
      second.material_writes != first.material_writes + 1U ||
      third.pipelines_created != first.pipelines_created ||
      third.point_uploads != first.point_uploads ||
      third.topology_uploads != first.topology_uploads) {
    return {id, "fail",
        "a texture must upload once however it is sampled, and a UV edit "
        "must rewrite one slot and nothing else"};
  }
  return {id, "pass", ""};
}

// An octahedron of radius 0.5 with UVs of zero, seen along -z through an
// orthographic camera: a diamond |x| + |y| <= 0.5 on screen, whose smooth
// normals point along x and y at the four rim corners and along +z at the
// centre. On a 64x64 target, column c's centre is x = (c + 0.5) / 32 - 1
// and row r's is y = 1 - (r + 0.5) / 32.
// OpenGL's orthographic projection of the unit cube: z is negated, so +z
// faces the camera and is nearer. A point at z lands at depth
// 0.5 - 0.5 * z.
void SetOrthographicView(Toon::RenderWorld& world) {
  Toon::ToonView view;
  view.projection.m[10] = -1.0F;
  world.SetView(view);
}

Toon::MeshId AddOctahedron(Toon::RenderWorld& world) {
  const Toon::MeshId mesh = world.CreateMesh();
  world.SetMeshPoints(mesh, {{0.5F, 0.0F, 0.0F}, {-0.5F, 0.0F, 0.0F},
                                {0.0F, 0.5F, 0.0F}, {0.0F, -0.5F, 0.0F}, {0.0F, 0.0F, 0.5F},
                                {0.0F, 0.0F, -0.5F}});
  // Counter-clockwise seen from outside: one triangle per octant.
  world.SetMeshTopology(mesh, {0, 2, 4, 1, 4, 2, 0, 4, 3, 1, 3, 4, 0, 5, 2,
                                  1, 2, 5, 0, 3, 5, 1, 5, 3});
  world.SetMeshUVs(mesh, std::vector<Toon::Float2>(6));
  SetOrthographicView(world);
  return mesh;
}

// mtoon_outline (material policy §5), on AddOctahedron's diamond, in a
// scene whose unit is a centimetre. A world-coordinates outline of 2 mm in
// green, unlit, is 0.2 units and grows the hull to |x| + |y| <= 0.7, so the
// pixel at x = 0.58 on the centre row draws green while the centre stays
// the surface's. Read in a scene of metres, the same 2 mm is too thin to
// reach that pixel. With the mode None, a value-only edit, that pixel is
// background; as screen coordinates, 0.1 of the screen height is the same
// 0.2 under this camera whatever the unit; a width texture whose G is 0,
// sampled in the vertex stage, takes the outline away again. Last, a hull
// at its surface's depth must lose to the surface.
Check MToonOutlineCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.material.mtoon_outline";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (renderer == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }

  Toon::RenderWorld world;
  const Toon::MeshId mesh = AddOctahedron(world);
  world.SetMetersPerUnit(0.01F);
  const Toon::MaterialId material = world.CreateMaterial();
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  toon.base_color = {1.0F, 0.0F, 0.0F};
  toon.mtoon.shade_color = {0.0F, 0.0F, 1.0F};
  toon.outline = true;
  toon.outline_width = 0.002F;
  toon.outline_color = {0.0F, 1.0F, 0.0F};
  toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  toon.mtoon.outline_lighting_mix = 0.0F;
  world.SetMaterial(material, toon);
  world.SetMeshMaterial(mesh, material);

  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  struct Shot {
    std::array<std::uint8_t, 4> rim{};
    std::array<std::uint8_t, 4> center{};
    Toon::OffscreenStatistics statistics;
  };
  // Column 50's centre is x = 0.578; row 32's is y = -0.016.
  const auto render = [&](Shot& shot) {
    if (!renderer->Render(Toon::ExtractDrawList(world.Commit()), 64, 64,
            color, depth, detail)) {
      return false;
    }
    shot.rim = PixelAt(color, 50, 32);
    shot.center = CenterPixel(color);
    shot.statistics = renderer->statistics();
    return true;
  };
  Shot world_width;
  Shot metres;
  Shot none;
  Shot screen_width;
  Shot textured;
  if (!render(world_width)) {
    return {id, "fail", detail};
  }
  world.SetMetersPerUnit(1.0F);
  if (!render(metres)) {
    return {id, "fail", detail};
  }
  world.SetMetersPerUnit(0.01F);
  toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::None;
  world.SetMaterial(material, toon);
  if (!render(none)) {
    return {id, "fail", detail};
  }
  toon.outline_width = 0.1F;
  toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::Screen;
  world.SetMaterial(material, toon);
  if (!render(screen_width)) {
    return {id, "fail", detail};
  }
  const Toon::TextureId texture = world.CreateTexture();
  Toon::ToonTexture texels;
  texels.width = 1;
  texels.height = 1;
  texels.encoding = Toon::ToonTextureEncoding::Linear;
  texels.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{255, 0, 255, 255});
  world.SetTexture(texture, texels);
  toon.mtoon.outline_width_texture.texture = texture;
  world.SetMaterial(material, toon);
  if (!render(textured)) {
    return {id, "fail", detail};
  }

  // A hull thinner than the depth buffer resolves. A double-sided quad at
  // z = 0.25, wound to face away from the camera, shows its back face, and
  // its hull's visible faces lie behind it, 1 pm away: a move its
  // coordinates round away, so hull and surface land at one depth. The hull
  // draws first, and the surface must still win over it.
  world.SetMeshVisible(mesh, false);
  const Toon::MeshId quad = world.CreateMesh();
  world.SetMeshPoints(quad, {{-0.3F, -0.3F, 0.25F}, {-0.3F, 0.3F, 0.25F},
                                {0.3F, 0.3F, 0.25F}, {0.3F, -0.3F, 0.25F}});
  world.SetMeshTopology(quad, {0, 1, 2, 0, 2, 3});
  world.SetMeshUVs(quad, std::vector<Toon::Float2>(4));
  const Toon::MaterialId thin_material = world.CreateMaterial();
  Toon::ToonMaterial thin = toon;
  thin.double_sided = true;
  thin.outline_width = 1e-12F;
  thin.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  thin.mtoon.outline_width_texture = {};
  world.SetMaterial(thin_material, thin);
  world.SetMeshMaterial(quad, thin_material);
  Shot coincident;
  if (!render(coincident)) {
    return {id, "fail", detail};
  }
  // Tilt the almost coincident back face through GPU skinning. At every
  // slope its image must equal the surface alone, including covered edges.
  Toon::ToonSkin skin;
  skin.influences_per_point = 1;
  skin.constant = true;
  skin.influences = {{0, 1}};
  world.SetMeshSkin(quad, skin);
  for (int frame = 0; frame < 12; ++frame) {
    const float angle = static_cast<float>(frame) * 0.1F;
    Toon::Matrix4 joint;
    joint.m[0] = joint.m[10] = std::cos(angle);
    joint.m[2] = -std::sin(angle);
    joint.m[8] = std::sin(angle);
    Toon::ToonSkinPose pose;
    pose.joints = {joint};
    world.SetMeshSkinPose(quad, pose);
    auto draws = Toon::ExtractDrawList(world.Commit());
    if (!renderer->Render(draws, 64, 64, color, depth, detail)) {
      return {id, "fail", detail};
    }
    const auto outlined = color.payload;
    draws.outlines = false;
    if (!renderer->Render(draws, 64, 64, color, depth, detail)) {
      return {id, "fail", detail};
    }
    if (outlined != color.payload) {
      return {id, "fail", "a nearly coincident animated hull changed the surface"};
    }
  }

  const Toon::OffscreenStatistics& first = world_width.statistics;
  const Toon::OffscreenStatistics& last = textured.statistics;
  if (coincident.statistics.validation_message_count != 0) {
    return {id, "fail", coincident.statistics.validation_detail};
  }
  if (renderer->statistics().validation_message_count != 0) {
    return {id, "fail", renderer->statistics().validation_detail};
  }
  const auto is_outline = [](const std::array<std::uint8_t, 4>& pixel) {
    return pixel[1] > 200U && pixel[0] < 50U && pixel[2] < 50U;
  };
  if (!is_outline(world_width.rim)) {
    return {id, "fail", "a world-coordinates outline did not draw its hull"};
  }
  if (world_width.center[1] > 50U || world_width.center[0] < 150U) {
    return {id, "fail", "the hull must stay behind the surface it outlines"};
  }
  if (metres.rim[1] > 80U) {
    return {id, "fail",
        "a world-coordinates width must be metres in the scene's unit"};
  }
  if (none.rim[1] > 80U) {
    return {id, "fail", "an outline width mode of None must draw no hull"};
  }
  if (!is_outline(screen_width.rim)) {
    return {id, "fail",
        "a screen-coordinates outline did not draw its ratio of the height"};
  }
  if (textured.rim[1] > 80U) {
    return {id, "fail",
        "a width texture whose G is 0 must take the outline away"};
  }
  if (world_width.statistics.outline_draws != 1 ||
      none.statistics.outline_draws != 0 ||
      textured.statistics.outline_draws != 0) {
    return {id, "fail", "disabled and all-zero width hulls must not be recorded"};
  }
  if (coincident.center[1] > 50U || coincident.center[0] < 150U) {
    return {id, "fail",
        "a hull at its surface's depth must lose the depth test to it"};
  }
  if (first.material_writes != 1 ||
      metres.statistics.material_writes != 1 ||
      none.statistics.material_writes != 2 ||
      screen_width.statistics.material_writes != 3 ||
      screen_width.statistics.texture_uploads != 0 ||
      last.texture_uploads != 1 ||
      last.pipelines_created != first.pipelines_created ||
      last.point_uploads != first.point_uploads ||
      last.topology_uploads != first.topology_uploads) {
    return {id, "fail",
        "an outline edit must rewrite one slot and nothing else, and a unit "
        "edit nothing at all"};
  }
  return {id, "pass", ""};
}

// Width sampling uses linear G at mip 0 and the role's UV transform. Pixel
// changes affect hull omission without rewriting a material slot; no UVs
// and missing images use white, just as the shader does.
Check OutlineSamplingCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.outline.sampling";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (!renderer) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }
  Toon::RenderWorld world;
  const auto mesh = AddOctahedron(world);
  const auto material = world.CreateMaterial();
  const auto texture = world.CreateTexture();
  Toon::ToonTexture image;
  image.width = 2;
  image.height = 1;
  image.encoding = Toon::ToonTextureEncoding::Linear;
  const auto pixels = [&](std::uint8_t left, std::uint8_t right) {
    image.pixels = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{255, left, 255, 255, 255, right, 255, 255});
    world.SetTexture(texture, image);
  };
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  toon.base_color = {1, 0, 0};
  toon.mtoon.shade_color = {1, 0, 0};
  toon.outline = true;
  toon.outline_width = 0.1F;
  toon.outline_color = {0, 1, 0};
  toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::Screen;
  toon.mtoon.outline_width_texture.texture = texture;
  toon.mtoon.outline_width_texture.offset = {0.75F, 0.5F};
  toon.mtoon.outline_lighting_mix = 0.0F;
  world.SetMeshMaterial(mesh, material);
  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  const auto render = [&](bool visible, std::uint32_t hulls, bool enabled = true) {
    auto draws = Toon::ExtractDrawList(world.Commit());
    draws.outlines = enabled;
    if (!renderer->Render(draws, 64, 64, color, depth, detail)) {
      return false;
    }
    const auto rim = PixelAt(color, 52, 32);
    if ((rim[1] > 200U) != visible ||
        renderer->statistics().outline_draws != hulls) {
      detail = "width sampling: G=" + std::to_string(rim[1]) +
               ", hulls=" + std::to_string(renderer->statistics().outline_draws) +
               ", expected G visible=" + std::to_string(visible) +
               ", hulls=" + std::to_string(hulls);
      return false;
    }
    return true;
  };
  for (auto alpha : {Toon::ToonAlphaMode::Opaque, Toon::ToonAlphaMode::Blend}) {
    toon.alpha_mode = alpha;
    toon.mtoon.outline_width_texture.offset.x = 0.75F;
    world.SetMeshUVs(mesh, std::vector<Toon::Float2>(6));
    world.SetMaterial(material, toon);
    pixels(0, 255);
    if (!render(true, 1)) {
      return {id, "fail", detail};
    }
    const auto writes = renderer->statistics().material_writes;
    pixels(0, 0);
    if (!render(false, 0) || renderer->statistics().material_writes != writes) {
      return {id, "fail", detail + "; a pixel edit must not rewrite a slot"};
    }
    // The vertex stage bypasses textures without UVs, so this hull must stay.
    world.SetMeshUVs(mesh, {});
    if (!render(true, 1)) {
      return {id, "fail", detail + "; no UVs must sample white"};
    }
    world.SetMeshUVs(mesh, std::vector<Toon::Float2>(6));
    pixels(0, 255);
    if (!render(true, 1) || renderer->statistics().material_writes != writes) {
      return {id, "fail", detail + "; a nonzero pixel must restore the hull"};
    }
    toon.mtoon.outline_width_texture.offset.x = 0.25F;
    world.SetMaterial(material, toon);
    if (!render(false, 1)) {
      return {id, "fail", detail + "; the UV offset must select the zero texel"};
    }
    toon.mtoon.outline_width_texture.offset.x = 0.5F;
    world.SetMaterial(material, toon);
    if (!render(false, 1)) {
      return {id, "fail", detail + "; linear filtering must interpolate half width"};
    }
    toon.mtoon.outline_width_texture.offset.x = 0.75F;
    world.SetMaterial(material, toon);
    if (!render(false, 0, false) || !render(true, 1)) {
      return {id, "fail", detail + "; the evaluation switch must omit all hulls"};
    }
    image.pixels = {};
    world.SetTexture(texture, image);
    if (!render(true, 1)) {
      return {id, "fail", detail + "; a missing image must sample white"};
    }
  }
  if (renderer->statistics().validation_message_count != 0) {
    return {id, "fail", renderer->statistics().validation_detail};
  }
  return {id, "pass", "opaque and Blend: linear G, UV offset, zero/nonzero pixel "
                      "edits, no UVs, missing image and hull switch"};
}

// Perspective distance changes a world's projected width, but not a screen
// ratio. Compensated object scale and orthographic zoom leave screen width
// alone as well, including a mirrored transform.
Check OutlineWidthCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.outline.width";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (!renderer) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }
  Toon::RenderWorld world;
  const auto mesh = AddOctahedron(world);
  const auto material = world.CreateMaterial();
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  toon.base_color = {1, 0, 0};
  toon.mtoon.shade_color = {1, 0, 0};
  toon.outline = true;
  toon.outline_width = 0.05F;
  toon.outline_color = {0, 1, 0};
  toon.mtoon.outline_lighting_mix = 0;
  world.SetMeshMaterial(mesh, material);
  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  const auto extent = [&](bool outlined) {
    auto draws = Toon::ExtractDrawList(world.Commit());
    draws.outlines = outlined;
    if (!renderer->Render(draws, 128, 128, color, depth, detail)) {
      return -1;
    }
    int right = -1;
    for (std::uint32_t row = 0; row < color.height; ++row) {
      for (std::uint32_t column = 0; column < color.width; ++column) {
        if (PixelAt(color, column, row)[outlined ? 1 : 0] > 128U) {
          right = std::max(right, static_cast<int>(column));
        }
      }
    }
    return right;
  };
  const auto width = [&]() {
    const int hull = extent(true);
    const int surface = extent(false);
    return hull >= 0 && surface >= 0 ? hull - surface : -1;
  };
  const auto perspective = [&](float distance) {
    Toon::ToonView view;
    view.view.m[14] = -distance;
    view.projection.m[0] = view.projection.m[5] = 2;
    view.projection.m[10] = -10.1F / 9.9F;
    view.projection.m[11] = -1;
    view.projection.m[14] = -2.0F / 9.9F;
    view.projection.m[15] = 0;
    world.SetView(view);
  };
  toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::Screen;
  world.SetMaterial(material, toon);
  perspective(2);
  const int screen_near = width();
  perspective(4);
  const int screen_far = width();
  toon.outline_width = 0.1F;
  toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  world.SetMaterial(material, toon);
  perspective(2);
  const int world_near = width();
  perspective(4);
  const int world_far = width();
  toon.outline_width = 0.05F;
  toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::Screen;
  world.SetMaterial(material, toon);
  Toon::ToonView view;
  view.projection.m[10] = -1;
  world.SetView(view);
  const int ortho = width();
  Toon::Matrix4 transform;
  transform.m[0] = -2;
  transform.m[5] = 2;
  world.SetMeshTransform(mesh, transform);
  view.projection.m[0] = view.projection.m[5] = 0.5F;
  world.SetView(view);
  const int scaled = width();
  if (screen_near < 4 || std::abs(screen_near - screen_far) > 1 ||
      world_near < 4 || world_far < 1 || world_far >= world_near ||
      ortho < 4 || std::abs(ortho - scaled) > 1) {
    return {id, "fail", "projected width changed incorrectly: screen=" + std::to_string(screen_near) + "/" + std::to_string(screen_far) + ", world=" + std::to_string(world_near) + "/" + std::to_string(world_far) + ", ortho=" + std::to_string(ortho) + "/" + std::to_string(scaled)};
  }
  if (renderer->statistics().validation_message_count != 0) {
    return {id, "fail", renderer->statistics().validation_detail};
  }
  return {id, "pass", "128px target, perspective distances 2/4: screen " + std::to_string(screen_near) + "/" + std::to_string(screen_far) + "px, world " + std::to_string(world_near) + "/" + std::to_string(world_far) + "px; orthographic/mirrored nonuniform scale " + std::to_string(ortho) + "/" + std::to_string(scaled) + "px"};
}

// Measure a subpixel hull under GPU skinning, separating its coverage from
// the moving surface with the evaluation switch. Repeating a pose must be
// pixel-identical; pose edits must leave geometry and materials resident.
Check OutlineMotionCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.outline.motion";
  std::string detail;
  struct Result {
    double minimum = 1e30;
    double maximum = 0;
    Toon::OffscreenStatistics statistics;
  };
  const auto run = [&](std::uint32_t samples, Result& result) {
    Toon::FrameStatus status = Toon::FrameStatus::Fail;
    Toon::RenderOptions options;
    options.samples = samples;
    auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail, options);
    if (!renderer) {
      return false;
    }
    Toon::RenderWorld world;
    const auto mesh = AddOctahedron(world);
    Toon::ToonSkin skin;
    skin.influences_per_point = 1;
    skin.constant = true;
    skin.influences = {{0, 1}};
    world.SetMeshSkin(mesh, skin);
    const auto material = world.CreateMaterial();
    Toon::ToonMaterial toon;
    toon.model = Toon::ToonShadingModel::MToon;
    toon.base_color = {1, 0, 0};
    toon.mtoon.shade_color = {1, 0, 0};
    toon.outline = true;
    toon.outline_width = 0.75F / 64.0F;
    toon.outline_color = {0, 1, 0};
    toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::Screen;
    toon.mtoon.outline_lighting_mix = 0;
    world.SetMaterial(material, toon);
    world.SetMeshMaterial(mesh, material);
    Toon::ColorProduct outlined;
    Toon::ColorProduct surface;
    Toon::ColorProduct repeated;
    Toon::DepthProduct depth;
    for (int frame = 0; frame <= 64; ++frame) {
      Toon::ToonSkinPose pose;
      Toon::Matrix4 joint;
      joint.m[12] = static_cast<float>(frame) / (64.0F * 32.0F);
      pose.joints = {joint};
      world.SetMeshSkinPose(mesh, pose);
      auto draws = Toon::ExtractDrawList(world.Commit());
      if (!renderer->Render(draws, 64, 64, outlined, depth, detail) ||
          !renderer->Render(draws, 64, 64, repeated, depth, detail)) {
        return false;
      }
      if (outlined.payload != repeated.payload) {
        detail = "a repeated pose changed the outline image";
        return false;
      }
      draws.outlines = false;
      if (!renderer->Render(draws, 64, 64, surface, depth, detail)) {
        return false;
      }
      double coverage = 0;
      for (std::size_t pixel = 1; pixel < outlined.payload.size(); pixel += 4) {
        coverage += static_cast<double>(outlined.payload[pixel]) - surface.payload[pixel];
      }
      coverage /= 255.0;
      result.minimum = std::min(result.minimum, coverage);
      result.maximum = std::max(result.maximum, coverage);
      const auto center = CenterPixel(outlined);
      if (center[0] < 150U || center[1] > 50U) {
        detail = "the animated hull covered the surface's centre";
        return false;
      }
    }
    result.statistics = renderer->statistics();
    const auto& stats = result.statistics;
    if (stats.pose_writes != 65 || stats.skin_uploads != 1 ||
        stats.point_uploads != 1 || stats.topology_uploads != 1 ||
        stats.material_writes != 1 || stats.validation_message_count != 0) {
      detail = "motion must rewrite only the joint buffer, with clean validation";
      return false;
    }
    return true;
  };
  Result single;
  Result multi;
  Result eight;
  if (!run(1, single) || !run(4, multi) || !run(8, eight)) {
    return {id, "fail", detail};
  }
  if (multi.statistics.samples == 1) {
    return {id, "skip", "device offers no MSAA"};
  }
  if (multi.minimum <= 0 ||
      multi.maximum - multi.minimum >= single.maximum - single.minimum) {
    return {id, "fail", "MSAA must reduce thin-hull coverage fluctuation"};
  }
  return {id, "pass", "0.75 pixel hull, 65 poses across one pixel: coverage " + std::to_string(single.minimum) + ".." + std::to_string(single.maximum) + " at 1x, " + std::to_string(multi.minimum) + ".." + std::to_string(multi.maximum) + " at " + std::to_string(multi.statistics.samples) + "x, " + std::to_string(eight.minimum) + ".." + std::to_string(eight.maximum) + " at " + std::to_string(eight.statistics.samples) + "x; repeats identical, "
                                                                                                                                                                                                                                                                                                                                                                                                                                    "65 pose writes and one geometry/skin/material upload"};
}

// MToon's rim (material policy §4's MToon block), on AddOctahedron's diamond
// with black lit and shade colours, so a pixel is the rim alone. A green
// parametric rim of fresnel power 1, unlit, is 1 - N.V: near 0 at the
// centre, 0.93 at x = 0.45 on the centre row, and the default matcapFactor
// adds nothing without a MatCap texture. A 2x2 MatCap texture whose top
// left is red, top right green and bottom left blue then draws each where
// the normal points: green up and to the right, blue down and to the left,
// red up and to the left. A matcapFactor of 0.5 mixed fully with the
// stand-in light, 1 + 0.25 ambient, is a value-only edit to 0.625; a black
// rim multiply texture takes the rim away.
Check MToonRimCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.material.mtoon_rim";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (renderer == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }

  Toon::RenderWorld world;
  const Toon::MeshId mesh = AddOctahedron(world);
  const Toon::MaterialId material = world.CreateMaterial();
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  toon.base_color = {0.0F, 0.0F, 0.0F};
  toon.mtoon.shade_color = {0.0F, 0.0F, 0.0F};
  toon.mtoon.rim_color = {0.0F, 1.0F, 0.0F};
  toon.mtoon.rim_fresnel_power = 1.0F;
  toon.mtoon.rim_lighting_mix = 0.0F;
  world.SetMaterial(material, toon);
  world.SetMeshMaterial(mesh, material);

  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  struct Shot {
    std::array<std::uint8_t, 4> center{};
    // x = 0.45 on the centre row.
    std::array<std::uint8_t, 4> edge{};
    // x and y 0.2 from the centre: up right, down left, up left.
    std::array<std::uint8_t, 4> up_right{};
    std::array<std::uint8_t, 4> down_left{};
    std::array<std::uint8_t, 4> up_left{};
    Toon::OffscreenStatistics statistics;
  };
  const auto render = [&](Shot& shot) {
    if (!renderer->Render(Toon::ExtractDrawList(world.Commit()), 64, 64,
            color, depth, detail)) {
      return false;
    }
    shot.center = CenterPixel(color);
    shot.edge = PixelAt(color, 46, 32);
    shot.up_right = PixelAt(color, 38, 25);
    shot.down_left = PixelAt(color, 25, 38);
    shot.up_left = PixelAt(color, 25, 25);
    shot.statistics = renderer->statistics();
    return true;
  };
  Shot parametric;
  Shot matcap;
  Shot mixed;
  Shot masked;
  if (!render(parametric)) {
    return {id, "fail", detail};
  }
  const Toon::TextureId matcap_texture = world.CreateTexture();
  Toon::ToonTexture texels;
  texels.width = 2;
  texels.height = 2;
  texels.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255,
          255, 255, 255, 255, 255});
  world.SetTexture(matcap_texture, texels);
  toon.mtoon.rim_color = {0.0F, 0.0F, 0.0F};
  toon.mtoon.matcap_texture.texture = matcap_texture;
  // Clamped, so a filtered sample near an edge does not reach across it.
  toon.mtoon.matcap_texture.wrap_s = Toon::ToonWrap::ClampToEdge;
  toon.mtoon.matcap_texture.wrap_t = Toon::ToonWrap::ClampToEdge;
  world.SetMaterial(material, toon);
  if (!render(matcap)) {
    return {id, "fail", detail};
  }
  toon.mtoon.matcap = {0.5F, 0.5F, 0.5F};
  toon.mtoon.rim_lighting_mix = 1.0F;
  world.SetMaterial(material, toon);
  if (!render(mixed)) {
    return {id, "fail", detail};
  }
  const Toon::TextureId mask = world.CreateTexture();
  texels.width = 1;
  texels.height = 1;
  texels.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{0, 0, 0, 255});
  world.SetTexture(mask, texels);
  toon.mtoon.rim_multiply_texture.texture = mask;
  world.SetMaterial(material, toon);
  if (!render(masked)) {
    return {id, "fail", detail};
  }

  const Toon::OffscreenStatistics& first = parametric.statistics;
  const Toon::OffscreenStatistics& last = masked.statistics;
  if (last.validation_message_count != 0) {
    return {id, "fail", last.validation_detail};
  }
  const auto is = [](const std::array<std::uint8_t, 4>& pixel,
                      std::uint8_t red, std::uint8_t green,
                      std::uint8_t blue) {
    const auto near = [](std::uint8_t value, std::uint8_t expected) {
      return value + 16 >= expected && value <= expected + 16;
    };
    return near(pixel[0], red) && near(pixel[1], green) &&
           near(pixel[2], blue);
  };
  if (!is(parametric.center, 0, 0, 0)) {
    return {id, "fail",
        "facing the camera, the parametric rim and a MatCap without a "
        "texture must add nothing"};
  }
  if (!is(parametric.edge, 0, 237, 0)) {
    return {id, "fail", "the parametric rim did not draw 1 - N.V at the edge"};
  }
  if (!is(matcap.up_right, 0, 255, 0) || !is(matcap.down_left, 0, 0, 255) ||
      !is(matcap.up_left, 255, 0, 0)) {
    return {id, "fail",
        "the MatCap texture did not draw where the view-space normal points"};
  }
  if (!is(mixed.up_right, 0, 159, 0)) {
    return {id, "fail",
        "a rim lighting mix of 1 did not multiply the rim by the light"};
  }
  if (!is(masked.up_right, 0, 0, 0) || !is(masked.edge, 0, 0, 0)) {
    return {id, "fail", "a black rim multiply texture must take the rim away"};
  }
  if (first.material_writes != 1 || matcap.statistics.texture_uploads != 1 ||
      mixed.statistics.material_writes != 3 ||
      mixed.statistics.texture_uploads != 1 || last.texture_uploads != 2 ||
      last.material_writes != 4 ||
      last.pipelines_created != first.pipelines_created ||
      last.point_uploads != first.point_uploads ||
      last.topology_uploads != first.topology_uploads) {
    return {id, "fail",
        "a rim edit must rewrite one slot, and a texture upload once"};
  }
  return {id, "pass", ""};
}

// The bootstrap triangle's outline at z, facing the camera, over the centre
// of the target.
Toon::MeshId AddTriangle(Toon::RenderWorld& world, float z) {
  const Toon::MeshId mesh = world.CreateMesh();
  world.SetMeshPoints(mesh,
      {{-0.70F, -0.65F, z}, {0.70F, -0.65F, z}, {0.00F, 0.70F, z}});
  world.SetMeshTopology(mesh, {0, 1, 2});
  return mesh;
}

// mtoon_transparent and the draw order (material policy §6), seen through
// SetOrthographicView. Lit colours saturate, and the target is UNORM, so a
// Blend layer of alpha 0.5 halves what is behind it and adds half its
// colour. At the centre: red Blend at z 0.5, created first, over opaque
// blue at -0.5 draws after it, as purple. Green Blend at 0.2, in the same
// queue, draws after red, as `draws` lists it; a renderQueueOffsetNumber of
// -1, a value-only edit, draws it first; transparentWithZWrite on red draws
// red first, in its own queue, and its depth hides green. Blue as Mask
// draws whole above its cutoff and is cut away below it. A double-sided
// Blend mesh whose front triangle samples red and whose back one, nearer
// the blue, samples green draws its back faces first. An outlined Blend
// octahedron with transparentWithZWrite blends its green hull at the rim
// and hides the hull's far side behind its surface.
Check MToonTransparentCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.material.mtoon_transparent";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (renderer == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }

  const auto blend = [](Toon::Float3 color) {
    Toon::ToonMaterial toon;
    toon.model = Toon::ToonShadingModel::MToon;
    toon.base_color = color;
    toon.mtoon.shade_color = color;
    toon.alpha = 0.5F;
    toon.alpha_mode = Toon::ToonAlphaMode::Blend;
    return toon;
  };
  Toon::RenderWorld world;
  SetOrthographicView(world);
  const Toon::MeshId red = AddTriangle(world, 0.5F);
  const Toon::MeshId blue = AddTriangle(world, -0.5F);
  Toon::ToonMaterial red_toon = blend({1.0F, 0.0F, 0.0F});
  Toon::ToonMaterial blue_toon;
  blue_toon.model = Toon::ToonShadingModel::MToon;
  blue_toon.base_color = {0.0F, 0.0F, 1.0F};
  blue_toon.mtoon.shade_color = {0.0F, 0.0F, 1.0F};
  const Toon::MaterialId red_material = world.CreateMaterial();
  const Toon::MaterialId blue_material = world.CreateMaterial();
  world.SetMaterial(red_material, red_toon);
  world.SetMaterial(blue_material, blue_toon);
  world.SetMeshMaterial(red, red_material);
  world.SetMeshMaterial(blue, blue_material);

  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  struct Shot {
    std::array<std::uint8_t, 4> center{};
    // x = 0.58 on the centre row, AddOctahedron's outline pixel.
    std::array<std::uint8_t, 4> rim{};
    float center_depth = 0.0F;
    Toon::OffscreenStatistics statistics;
  };
  const auto render = [&](Shot& shot) {
    if (!renderer->Render(Toon::ExtractDrawList(world.Commit()), 64, 64,
            color, depth, detail)) {
      return false;
    }
    shot.center = CenterPixel(color);
    shot.rim = PixelAt(color, 50, 32);
    shot.center_depth = depth.payload[32U * depth.width + 32U];
    shot.statistics = renderer->statistics();
    return true;
  };
  Shot over_opaque;
  Shot in_order;
  Shot offset;
  Shot z_write;
  Shot mask_kept;
  Shot mask_cut;
  Shot double_sided;
  Shot outlined;
  if (!render(over_opaque)) {
    return {id, "fail", detail};
  }
  const Toon::MeshId green = AddTriangle(world, 0.2F);
  Toon::ToonMaterial green_toon = blend({0.0F, 1.0F, 0.0F});
  const Toon::MaterialId green_material = world.CreateMaterial();
  world.SetMaterial(green_material, green_toon);
  world.SetMeshMaterial(green, green_material);
  if (!render(in_order)) {
    return {id, "fail", detail};
  }
  green_toon.mtoon.render_queue_offset = -1;
  world.SetMaterial(green_material, green_toon);
  if (!render(offset)) {
    return {id, "fail", detail};
  }
  red_toon.mtoon.transparent_with_z_write = true;
  world.SetMaterial(red_material, red_toon);
  if (!render(z_write)) {
    return {id, "fail", detail};
  }
  blue_toon.alpha_mode = Toon::ToonAlphaMode::Mask;
  blue_toon.alpha = 0.7F;
  world.SetMaterial(blue_material, blue_toon);
  if (!render(mask_kept)) {
    return {id, "fail", detail};
  }
  blue_toon.alpha = 0.3F;
  world.SetMaterial(blue_material, blue_toon);
  if (!render(mask_cut)) {
    return {id, "fail", detail};
  }

  // The front triangle at z 0.5 samples the red texel; the back one at 0.3,
  // wound to face away, the green one, and is listed second.
  world.SetMeshVisible(red, false);
  world.SetMeshVisible(green, false);
  blue_toon.alpha_mode = Toon::ToonAlphaMode::Opaque;
  blue_toon.alpha = 1.0F;
  world.SetMaterial(blue_material, blue_toon);
  const Toon::MeshId layers = world.CreateMesh();
  world.SetMeshPoints(layers,
      {{-0.70F, -0.65F, 0.5F}, {0.70F, -0.65F, 0.5F}, {0.00F, 0.70F, 0.5F},
          {-0.70F, -0.65F, 0.3F}, {0.70F, -0.65F, 0.3F},
          {0.00F, 0.70F, 0.3F}});
  world.SetMeshTopology(layers, {0, 1, 2, 3, 5, 4});
  world.SetMeshUVs(layers, {{0.25F, 0.75F}, {0.25F, 0.75F}, {0.25F, 0.75F},
                               {0.75F, 0.75F}, {0.75F, 0.75F}, {0.75F, 0.75F}});
  const Toon::TextureId texture = world.CreateTexture();
  Toon::ToonTexture texels;
  texels.width = 2;
  texels.height = 2;
  texels.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255,
          255, 255, 255, 255, 255});
  world.SetTexture(texture, texels);
  Toon::ToonMaterial layers_toon = blend({1.0F, 1.0F, 1.0F});
  layers_toon.base_texture.texture = texture;
  layers_toon.base_texture.wrap_s = Toon::ToonWrap::ClampToEdge;
  layers_toon.base_texture.wrap_t = Toon::ToonWrap::ClampToEdge;
  layers_toon.double_sided = true;
  const Toon::MaterialId layers_material = world.CreateMaterial();
  world.SetMaterial(layers_material, layers_toon);
  world.SetMeshMaterial(layers, layers_material);
  if (!render(double_sided)) {
    return {id, "fail", detail};
  }

  world.SetMeshVisible(layers, false);
  world.SetMeshVisible(blue, false);
  const Toon::MeshId octahedron = AddOctahedron(world);
  Toon::ToonMaterial outlined_toon = blend({1.0F, 0.0F, 0.0F});
  outlined_toon.mtoon.transparent_with_z_write = true;
  outlined_toon.outline = true;
  outlined_toon.outline_width = 0.2F;
  outlined_toon.outline_color = {0.0F, 1.0F, 0.0F};
  outlined_toon.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  outlined_toon.mtoon.outline_lighting_mix = 0.0F;
  const Toon::MaterialId outlined_material = world.CreateMaterial();
  world.SetMaterial(outlined_material, outlined_toon);
  world.SetMeshMaterial(octahedron, outlined_material);
  if (!render(outlined)) {
    return {id, "fail", detail};
  }

  const Toon::OffscreenStatistics& first = over_opaque.statistics;
  const Toon::OffscreenStatistics& last = outlined.statistics;
  if (last.validation_message_count != 0) {
    return {id, "fail", last.validation_detail};
  }
  const auto is = [](const std::array<std::uint8_t, 4>& pixel,
                      std::uint8_t red, std::uint8_t green,
                      std::uint8_t blue) {
    const auto near = [](std::uint8_t value, std::uint8_t expected) {
      return value + 16 >= expected && value <= expected + 16;
    };
    return near(pixel[0], red) && near(pixel[1], green) &&
           near(pixel[2], blue);
  };
  if (!is(over_opaque.center, 128, 0, 128)) {
    return {id, "fail",
        "a Blend surface must blend by its alpha over the opaque draw behind "
        "it, whatever order the meshes were created in"};
  }
  if (!is(in_order.center, 64, 128, 64)) {
    return {id, "fail",
        "within one render queue, transparent draws must keep their order"};
  }
  if (!is(offset.center, 128, 64, 64) ||
      std::fabs(offset.center_depth - 0.75F) > 0.01F) {
    return {id, "fail",
        "a lower renderQueueOffsetNumber must draw first, without writing "
        "depth"};
  }
  if (!is(z_write.center, 128, 0, 128) ||
      std::fabs(z_write.center_depth - 0.25F) > 0.01F) {
    return {id, "fail",
        "transparentWithZWrite must draw before the other transparent queue "
        "and write depth"};
  }
  if (!is(mask_kept.center, 128, 0, 128)) {
    return {id, "fail",
        "a Mask surface above its cutoff must draw whole, before Blend"};
  }
  if (!is(mask_cut.center, 134, 13, 19)) {
    return {id, "fail", "a Mask surface below its cutoff must be cut away"};
  }
  if (!is(double_sided.center, 128, 64, 64)) {
    return {id, "fail",
        "a double-sided Blend surface must draw its back faces first"};
  }
  if (!is(outlined.rim, 6, 140, 19)) {
    return {id, "fail",
        "a transparent material's hull must blend by its surface's alpha"};
  }
  if (!is(outlined.center, 134, 13, 19)) {
    return {id, "fail",
        "a surface that writes depth must hide its hull's far side"};
  }
  const Toon::OffscreenStatistics& before = in_order.statistics;
  const Toon::OffscreenStatistics& after = z_write.statistics;
  if (after.material_writes != before.material_writes + 2U ||
      after.point_uploads != before.point_uploads ||
      after.topology_uploads != before.topology_uploads ||
      after.texture_uploads != before.texture_uploads ||
      last.pipelines_created != first.pipelines_created) {
    return {id, "fail",
        "a render queue or depth write edit must rewrite one slot and "
        "nothing else"};
  }
  return {id, "pass", ""};
}

// Multisample anti-aliasing, by a renderer asked for 1 sample and one given
// the default, 4, through SetOrthographicView. The first scene is
// AddOctahedron's diamond in unlit red under an unlit green world outline
// of 0.2: at 1 sample every pixel is background, red or green, and
// multisampled the hull's silhouette and the surface's edge over it
// resolve to mixtures, while a pixel inside either stays the same. The
// second is a Mask quad, its edges on pixel boundaries, whose base texture
// alpha rises from 0 to 1 across the diagonal x + y = 0, where it crosses
// the cutoff: the cut is a hard step at 1 sample and, by alpha to coverage,
// a mixture along the diagonal multisampled.
Check AntiAliasingCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.antialiasing.msaa";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto single = Toon::CreateOffscreenRenderer(shaders, status, detail,
      Toon::RenderOptions{1});
  if (single == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }
  auto multi = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (multi == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }
  const std::uint32_t samples = multi->statistics().samples;
  if (single->statistics().samples != 1) {
    return {id, "fail", "a renderer asked for 1 sample must not multisample"};
  }
  if (samples < 2) {
    return {id, "skip",
        multi->statistics().device_name +
            " offers no multisampled colour and depth target"};
  }

  Toon::RenderWorld outlined;
  const Toon::MeshId octahedron = AddOctahedron(outlined);
  Toon::ToonMaterial red;
  red.model = Toon::ToonShadingModel::MToon;
  red.base_color = {1.0F, 0.0F, 0.0F};
  red.mtoon.shade_color = {1.0F, 0.0F, 0.0F};
  Toon::ToonMaterial outline = red;
  outline.outline = true;
  outline.outline_width = 0.2F;
  outline.outline_color = {0.0F, 1.0F, 0.0F};
  outline.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  outline.mtoon.outline_lighting_mix = 0.0F;
  const Toon::MaterialId outline_material = outlined.CreateMaterial();
  outlined.SetMaterial(outline_material, outline);
  outlined.SetMeshMaterial(octahedron, outline_material);
  const Toon::DrawList outline_draws = Toon::ExtractDrawList(outlined.Commit());

  // x from -0.75 to 0.75 is columns 8 to 55, y rows 8 to 55. u runs from 0
  // at the bottom left to 1 at the top right, 0.5 along x + y = 0; the
  // texture's alpha is 0 at u 0.25 and 1 at 0.75, clamped.
  Toon::RenderWorld masked;
  SetOrthographicView(masked);
  const Toon::MeshId quad = masked.CreateMesh();
  masked.SetMeshPoints(quad, {{-0.75F, -0.75F, 0.0F}, {0.75F, -0.75F, 0.0F},
                                 {0.75F, 0.75F, 0.0F}, {-0.75F, 0.75F, 0.0F}});
  masked.SetMeshTopology(quad, {0, 1, 2, 0, 2, 3});
  masked.SetMeshUVs(quad,
      {{0.0F, 0.5F}, {0.5F, 0.5F}, {1.0F, 0.5F}, {0.5F, 0.5F}});
  const Toon::TextureId ramp = masked.CreateTexture();
  Toon::ToonTexture texels;
  texels.width = 2;
  texels.height = 1;
  texels.pixels = std::make_shared<const std::vector<std::uint8_t>>(
      std::vector<std::uint8_t>{255, 0, 0, 0, 255, 0, 0, 255});
  masked.SetTexture(ramp, texels);
  Toon::ToonMaterial mask = red;
  mask.alpha_mode = Toon::ToonAlphaMode::Mask;
  mask.base_texture.texture = ramp;
  mask.base_texture.wrap_s = Toon::ToonWrap::ClampToEdge;
  mask.base_texture.wrap_t = Toon::ToonWrap::ClampToEdge;
  const Toon::MaterialId mask_material = masked.CreateMaterial();
  masked.SetMaterial(mask_material, mask);
  masked.SetMeshMaterial(quad, mask_material);
  const Toon::DrawList mask_draws = Toon::ExtractDrawList(masked.Commit());

  struct Shot {
    Toon::ColorProduct color;
    Toon::DepthProduct depth;
  };
  Shot single_outline;
  Shot multi_outline;
  Shot single_mask;
  Shot multi_mask;
  if (!single->Render(outline_draws, 64, 64, single_outline.color,
          single_outline.depth, detail) ||
      !multi->Render(outline_draws, 64, 64, multi_outline.color,
          multi_outline.depth, detail) ||
      !single->Render(mask_draws, 64, 64, single_mask.color, single_mask.depth,
          detail) ||
      !multi->Render(mask_draws, 64, 64, multi_mask.color, multi_mask.depth,
          detail)) {
    return {id, "fail", detail};
  }
  for (const Toon::OffscreenRenderer* renderer : {single.get(), multi.get()}) {
    if (renderer->statistics().validation_message_count != 0) {
      return {id, "fail", renderer->statistics().validation_detail};
    }
  }

  const auto near = [](const std::array<std::uint8_t, 4>& pixel,
                        std::uint8_t red_value, std::uint8_t green,
                        std::uint8_t blue) {
    const auto close = [](std::uint8_t value, std::uint8_t expected) {
      return value + 3 >= expected && value <= expected + 3;
    };
    return close(pixel[0], red_value) && close(pixel[1], green) &&
           close(pixel[2], blue);
  };
  // The background, 0.05, 0.10, 0.15 in UNORM.
  const auto background = [&](const std::array<std::uint8_t, 4>& pixel) {
    return near(pixel, 13, 26, 38);
  };
  struct Counts {
    // Neither background, red nor green.
    std::uint32_t mixed = 0;
    // Red and green both at least a sixth: the surface's edge over its hull.
    std::uint32_t surface_edge = 0;
  };
  const auto count = [&](const Toon::ColorProduct& color) {
    Counts counts;
    for (std::uint32_t row = 0; row < color.height; ++row) {
      for (std::uint32_t column = 0; column < color.width; ++column) {
        const std::array<std::uint8_t, 4> pixel = PixelAt(color, column, row);
        if (!background(pixel) && !near(pixel, 255, 0, 0) &&
            !near(pixel, 0, 255, 0)) {
          ++counts.mixed;
        }
        if (pixel[0] >= 43U && pixel[1] >= 43U) {
          ++counts.surface_edge;
        }
      }
    }
    return counts;
  };
  const Counts single_counts = count(single_outline.color);
  const Counts multi_counts = count(multi_outline.color);
  const Counts single_cut = count(single_mask.color);
  const Counts multi_cut = count(multi_mask.color);

  if (single_counts.mixed != 0 || single_cut.mixed != 0) {
    return {id, "fail",
        "at 1 sample every pixel must be the background, the surface or the "
        "outline"};
  }
  if (multi_counts.mixed < 32U || multi_counts.surface_edge < 8U) {
    return {id, "fail",
        "multisampled, the hull's silhouette and the surface's edge over it "
        "must resolve to mixtures"};
  }
  // The centre is the surface's, x = 0.58 on the centre row the hull's.
  for (const auto& [column, row] :
      {std::pair<std::uint32_t, std::uint32_t>{32, 32}, {50, 32}}) {
    const std::array<std::uint8_t, 4> one =
        PixelAt(single_outline.color, column, row);
    const std::array<std::uint8_t, 4> many =
        PixelAt(multi_outline.color, column, row);
    if (!near(many, one[0], one[1], one[2])) {
      return {id, "fail",
          "a pixel inside a surface must not change with the sample count"};
    }
  }
  const float centre_depth =
      single_outline.depth.payload[32U * single_outline.depth.width + 32U];
  const float resolved_depth =
      multi_outline.depth.payload[32U * multi_outline.depth.width + 32U];
  // Sample 0 lies within the pixel, where the facet's depth differs from
  // the centre's by less than 0.016.
  if (std::fabs(centre_depth - resolved_depth) > 0.02F ||
      multi_outline.depth.payload.front() < 0.99F) {
    return {id, "fail",
        "the resolved depth must be the surface's inside it and 1 outside"};
  }
  // Kept above the diagonal, where x + y > 0, and cut below it.
  if (!near(PixelAt(multi_mask.color, 40, 20), 255, 0, 0) ||
      !background(PixelAt(multi_mask.color, 20, 40))) {
    return {id, "fail",
        "a Mask surface must be kept above its cutoff and cut below it"};
  }
  if (multi_cut.mixed < 16U) {
    return {id, "fail",
        "multisampled, alpha to coverage must spread a Mask cut over its "
        "samples"};
  }
  return {id, "pass",
      std::to_string(samples) + " samples: " +
          std::to_string(multi_counts.mixed) + " mixed pixels around the " +
          "outlined diamond, " + std::to_string(multi_cut.mixed) +
          " along the Mask cut; none at 1 sample"};
}

Toon::Matrix4 Translation(float x) {
  Toon::Matrix4 matrix;
  matrix.m[12] = x;
  return matrix;
}

// GPU skinning (design policy §11): the bootstrap triangle, every point
// bound to joint 1 alone. Its geometry bind moves it right by 1.2 and joint
// 1's skinning transform left by as much, so it draws where it is. A pose
// that drops joint 1's move leaves the centre empty; a skeleton-to-mesh
// transform left by 1.2 brings it back, drawn through mtoon_opaque this
// time. Each pose writes one joint buffer and uploads nothing else.
Check SkinningCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.skinning.gpu";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (renderer == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }

  Toon::RenderWorld world;
  world.SetBootstrapTriangle();
  const Toon::MeshId mesh = world.Commit().meshes.front().id;
  Toon::ToonSkin skin;
  skin.influences_per_point = 1;
  skin.influences = {{1, 1.0F}, {1, 1.0F}, {1, 1.0F}};
  skin.geom_bind = Translation(1.2F);
  world.SetMeshSkin(mesh, skin);
  Toon::ToonSkinPose pose;
  pose.joints = {Toon::Matrix4{}, Translation(-1.2F)};
  world.SetMeshSkinPose(mesh, pose);

  Toon::ColorProduct color;
  Toon::DepthProduct depth;
  const auto render = [&](std::array<std::uint8_t, 4>& pixel,
                          Toon::OffscreenStatistics& statistics) {
    if (!renderer->Render(Toon::ExtractDrawList(world.Commit()), 64, 64,
            color, depth, detail)) {
      return false;
    }
    pixel = CenterPixel(color);
    statistics = renderer->statistics();
    return true;
  };
  std::array<std::uint8_t, 4> bound{};
  std::array<std::uint8_t, 4> moved{};
  std::array<std::uint8_t, 4> returned{};
  Toon::OffscreenStatistics first;
  Toon::OffscreenStatistics second;
  Toon::OffscreenStatistics third;
  if (!render(bound, first)) {
    return {id, "fail", detail};
  }
  pose.joints[1] = Toon::Matrix4{};
  world.SetMeshSkinPose(mesh, pose);
  if (!render(moved, second)) {
    return {id, "fail", detail};
  }
  pose.skeleton_to_mesh = Translation(-1.2F);
  world.SetMeshSkinPose(mesh, pose);
  const Toon::MaterialId material = world.CreateMaterial();
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  toon.base_color = {1.0F, 0.0F, 0.0F};
  toon.mtoon.shade_color = {0.0F, 0.0F, 1.0F};
  world.SetMaterial(material, toon);
  world.SetMeshMaterial(mesh, material);
  if (!render(returned, third)) {
    return {id, "fail", detail};
  }

  if (third.validation_message_count != 0) {
    return {id, "fail", third.validation_detail};
  }
  if (bound[0] < 150U || bound[2] > 80U) {
    return {id, "fail",
        "a joint undoing the geometry bind must draw the triangle in place"};
  }
  if (moved[0] > 80U) {
    return {id, "fail", "a pose change must move the skinned points"};
  }
  if (returned[0] < 200U || returned[2] > 50U) {
    return {id, "fail",
        "mtoon_opaque must skin into the mesh's space and light it"};
  }
  if (first.skin_uploads != 1 || third.skin_uploads != 1 ||
      first.pose_writes != 1 || second.pose_writes != 2 ||
      third.pose_writes != 3 || third.point_uploads != first.point_uploads ||
      third.topology_uploads != first.topology_uploads ||
      third.pipelines_created != first.pipelines_created) {
    return {id, "fail",
        "a pose change must write one joint buffer and upload nothing else"};
  }
  return {id, "pass", ""};
}

std::string Status(Toon::FrameStatus status) {
  switch (status) {
  case Toon::FrameStatus::Pass:
    return "pass";
  case Toon::FrameStatus::Fail:
    return "fail";
  case Toon::FrameStatus::Skip:
    return "skip";
  }
  return "fail";
}

} // namespace

int main(int argc, char** argv) {
  Session session;
  session.started = static_cast<long long>(std::time(nullptr));
  session.target = "toon-headless";
  // Start time plus pid: unique per invocation, so a later session supersedes
  // an earlier one, and a portable identifier the report model accepts.
  session.id = "headless-" + std::to_string(session.started) + "-" +
               std::to_string(CurrentProcessId());

  std::string report_path = "renderer-report.json";
  bool install_tree = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--report" && index + 1 < argc) {
      report_path = argv[++index];
    } else if (argument == "--install-tree") {
      install_tree = true;
    } else {
      std::cerr << "usage: toon-headless [--report <path>] [--install-tree]\n";
      return 2;
    }
  }

  Toon::RenderWorld world;
  world.SetBootstrapTriangle();
  const Toon::FrameSnapshot first = world.Commit();
  const Toon::FrameSnapshot unchanged = world.Commit();
  const Toon::DrawList draws = Toon::ExtractDrawList(first);
  const bool core_ok = first.revision == 1 && unchanged.revision == first.revision &&
                       draws.draws.size() == 1 && draws.triangle_count == 1;

  const Toon::BackendCapability capability = Toon::ProbeVulkanBackend();
  const Toon::SceneShaders shaders = Toon::SceneShadersIn(
      (std::filesystem::absolute(argv[0]).parent_path() / "shaders").string());
  const Toon::GpuFrameEvidence frame =
      Toon::RenderOffscreen(draws, shaders, 1000);

  bool color_ok = false;
  bool depth_ok = false;
  bool persistence_ok = false;
  if (frame.status == Toon::FrameStatus::Pass) {
    const std::size_t center =
        (frame.color.height / 2U) * frame.color.row_pitch +
        (frame.color.width / 2U) * 4U;
    color_ok = frame.color.width == 64 && frame.color.height == 64 &&
               frame.color.row_pitch == 64U * 4U &&
               frame.color.pixel_format == "rgba8-unorm" &&
               frame.color.origin == "top-left" &&
               frame.color.color_space == "linear" &&
               frame.color.payload.size() == 64U * 64U * 4U &&
               center + 3U < frame.color.payload.size() &&
               frame.color.payload[center] > 150U &&
               frame.color.payload[center + 1U] < 100U &&
               frame.color.payload[center + 2U] < 80U &&
               frame.color.payload[center + 3U] > 240U;

    const std::size_t depth_center =
        (frame.depth.height / 2U) * frame.depth.width + frame.depth.width / 2U;
    depth_ok = frame.depth.width == 64 && frame.depth.height == 64 &&
               frame.depth.row_pitch == 64U * sizeof(float) &&
               frame.depth.pixel_format == "d32-sfloat" &&
               frame.depth.origin == "top-left" &&
               frame.depth.payload.size() == 64U * 64U &&
               depth_center < frame.depth.payload.size() &&
               frame.depth.payload[depth_center] > 0.0F &&
               frame.depth.payload[depth_center] < 0.9F &&
               frame.depth.payload.front() > 0.99F;
    // 1,000 frames on one device: the four scene pipelines, one target
    // allocation and one upload of the unchanged mesh, every later frame
    // reusing them.
    const Toon::OffscreenStatistics& statistics = frame.statistics;
    persistence_ok = statistics.frames_rendered == 1000 &&
                     statistics.completion == 1000 &&
                     statistics.pipelines_created == 4 &&
                     statistics.target_allocations == 1 &&
                     statistics.topology_uploads == 1 &&
                     statistics.point_uploads == 1;
  }

  std::vector<Check> checks;
  checks.push_back({"renderer.core.boundary", core_ok ? "pass" : "fail",
      core_ok ? "" : "commit/extraction contract mismatch"});
  checks.push_back({"renderer.backend.capability",
      capability.available ? "pass" : "skip", capability.detail});
  checks.push_back({"renderer.gpu.frame", Status(frame.status), frame.detail});
  const Toon::OffscreenStatistics& statistics = frame.statistics;
  if (statistics.validation_available) {
    checks.push_back({"renderer.validation.messages",
        statistics.validation_message_count == 0 ? "pass" : "fail",
        statistics.validation_message_count == 0
            ? ""
            : statistics.validation_detail});
  } else {
    checks.push_back({"renderer.validation.messages", "skip",
        statistics.validation_detail.empty()
            ? "Vulkan validation capture was unavailable"
            : statistics.validation_detail});
  }
  if (frame.status == Toon::FrameStatus::Pass) {
    checks.push_back({"renderer.render_product.color", color_ok ? "pass" : "fail",
        color_ok ? "" : "RGBA8 metadata or center pixel mismatch"});
    checks.push_back({"renderer.render_product.depth", depth_ok ? "pass" : "fail",
        depth_ok ? "" : "depth metadata or numeric payload mismatch"});
    checks.push_back({"renderer.frame.persistence",
        persistence_ok ? "pass" : "fail",
        persistence_ok ? ""
                       : "1,000 frames did not complete on the scene "
                         "pipelines, one target allocation and mesh upload"});
    checks.push_back(MToonOpaqueCheck(shaders));
    checks.push_back(MToonTexturedCheck(shaders));
    checks.push_back(MToonOutlineCheck(shaders));
    checks.push_back(OutlineSamplingCheck(shaders));
    checks.push_back(OutlineWidthCheck(shaders));
    checks.push_back(OutlineMotionCheck(shaders));
    checks.push_back(MToonRimCheck(shaders));
    checks.push_back(MToonTransparentCheck(shaders));
    checks.push_back(AntiAliasingCheck(shaders));
    checks.push_back(SkinningCheck(shaders));
  } else {
    const std::string dependent = "renderer.gpu.frame did not pass: " + frame.detail;
    checks.push_back({"renderer.render_product.color", "skip", dependent});
    checks.push_back({"renderer.render_product.depth", "skip", dependent});
    checks.push_back({"renderer.frame.persistence", "skip", dependent});
    checks.push_back({"renderer.material.mtoon_opaque", "skip", dependent});
    checks.push_back({"renderer.material.mtoon_textured", "skip", dependent});
    checks.push_back({"renderer.material.mtoon_outline", "skip", dependent});
    checks.push_back({"renderer.outline.sampling", "skip", dependent});
    checks.push_back({"renderer.outline.width", "skip", dependent});
    checks.push_back({"renderer.outline.motion", "skip", dependent});
    checks.push_back({"renderer.material.mtoon_rim", "skip", dependent});
    checks.push_back({"renderer.material.mtoon_transparent", "skip", dependent});
    checks.push_back({"renderer.antialiasing.msaa", "skip", dependent});
    checks.push_back({"renderer.skinning.gpu", "skip", dependent});
  }
  checks.push_back({"renderer.install_tree", install_tree ? "pass" : "skip",
      install_tree ? "" : "run the renderer install-tree CTest"});
#if defined(TOON_HAS_HYDRA2)
  const std::string hydra_detail =
      "the co-built Hydra adapter is exercised by its OpenUSD CTests";
#else
  const std::string hydra_detail =
      "configure with TOON_ENABLE_HYDRA2=ON and a matching OpenUSD SDK";
#endif
  checks.push_back({"renderer.plugin.discovery", "skip", hydra_detail});
  checks.push_back({"renderer.delegate.creation", "skip", hydra_detail});
  checks.push_back({"renderer.render_buffer.cpu", "skip", hydra_detail});
  checks.push_back({"renderer.host.first_frame", "skip", hydra_detail});
  checks.push_back({"renderer.host.stable_update", "skip", hydra_detail});

  const bool failed = std::any_of(checks.begin(), checks.end(),
      [](const Check& check) {
        return check.status == "fail";
      });

  // The session concludes here, with every check already decided — the report
  // is published only once this run has actually finished, and it says so.
  //
  // The outcome describes *this harness*, not the verdicts it reached: it ran
  // every check to a decision, so it succeeded even when some of those checks
  // failed. Conflating the two would mark the session a failure and make its
  // own PASSes unmergeable, so a run with any FAIL could not report the
  // checks that passed alongside it. Failing checks are carried by `checks`;
  // a failed session is one that could not produce them at all.
  session.completed = static_cast<long long>(std::time(nullptr));
  session.succeeded = true;
  if (!WriteReport(report_path, checks, frame, session)) {
    std::cerr << "cannot write renderer report: " << report_path << '\n';
    return 1;
  }
  return failed ? 1 : 0;
}
