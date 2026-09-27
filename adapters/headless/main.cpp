// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>
#include <toon/render_world.hpp>
#include <toon/vulkan_backend.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
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

// mtoon_outline (material policy §5): an octahedron of radius 0.5, seen
// along -z through an orthographic camera, is a diamond |x| + |y| <= 0.5 on
// screen, and its smooth normals at the four rim corners point along x and
// y. A world-coordinates outline of 0.2 in green, unlit, grows the hull to
// |x| + |y| <= 0.7, so the pixel at x = 0.58 on the centre row draws green
// while the centre stays the surface's. With the mode None, a value-only
// edit, that pixel is background; as screen coordinates, 0.1 of the screen
// height is the same 0.2 under this camera; a width texture whose G is 0,
// sampled in the vertex stage, takes the outline away again.
Check MToonOutlineCheck(const Toon::SceneShaders& shaders) {
  const std::string id = "renderer.material.mtoon_outline";
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, detail);
  if (renderer == nullptr) {
    return {id, status == Toon::FrameStatus::Skip ? "skip" : "fail", detail};
  }

  Toon::RenderWorld world;
  const Toon::MeshId mesh = world.CreateMesh();
  world.SetMeshPoints(mesh, {{0.5F, 0.0F, 0.0F}, {-0.5F, 0.0F, 0.0F},
                                {0.0F, 0.5F, 0.0F}, {0.0F, -0.5F, 0.0F}, {0.0F, 0.0F, 0.5F},
                                {0.0F, 0.0F, -0.5F}});
  // Counter-clockwise seen from outside: one triangle per octant.
  world.SetMeshTopology(mesh, {0, 2, 4, 1, 4, 2, 0, 4, 3, 1, 3, 4, 0, 5, 2,
                                  1, 2, 5, 0, 3, 5, 1, 5, 3});
  world.SetMeshUVs(mesh, std::vector<Toon::Float2>(6));
  // OpenGL's orthographic projection of the unit cube: z is negated, so +z
  // faces the camera and is nearer.
  Toon::ToonView view;
  view.projection.m[10] = -1.0F;
  world.SetView(view);
  const Toon::MaterialId material = world.CreateMaterial();
  Toon::ToonMaterial toon;
  toon.model = Toon::ToonShadingModel::MToon;
  toon.base_color = {1.0F, 0.0F, 0.0F};
  toon.mtoon.shade_color = {0.0F, 0.0F, 1.0F};
  toon.outline = true;
  toon.outline_width = 0.2F;
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
  Shot none;
  Shot screen_width;
  Shot textured;
  if (!render(world_width)) {
    return {id, "fail", detail};
  }
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

  const Toon::OffscreenStatistics& first = world_width.statistics;
  const Toon::OffscreenStatistics& last = textured.statistics;
  if (last.validation_message_count != 0) {
    return {id, "fail", last.validation_detail};
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
  if (first.material_writes != 1 || none.statistics.material_writes != 2 ||
      screen_width.statistics.material_writes != 3 ||
      screen_width.statistics.texture_uploads != 0 ||
      last.texture_uploads != 1 ||
      last.pipelines_created != first.pipelines_created ||
      last.point_uploads != first.point_uploads ||
      last.topology_uploads != first.topology_uploads) {
    return {id, "fail",
        "an outline edit must rewrite one slot and nothing else"};
  }
  return {id, "pass", ""};
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
    // 1,000 frames on one device: the three scene pipelines, one target
    // allocation and one upload of the unchanged mesh, every later frame
    // reusing them.
    const Toon::OffscreenStatistics& statistics = frame.statistics;
    persistence_ok = statistics.frames_rendered == 1000 &&
                     statistics.completion == 1000 &&
                     statistics.pipelines_created == 3 &&
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
    checks.push_back(SkinningCheck(shaders));
  } else {
    const std::string dependent = "renderer.gpu.frame did not pass: " + frame.detail;
    checks.push_back({"renderer.render_product.color", "skip", dependent});
    checks.push_back({"renderer.render_product.depth", "skip", dependent});
    checks.push_back({"renderer.frame.persistence", "skip", dependent});
    checks.push_back({"renderer.material.mtoon_opaque", "skip", dependent});
    checks.push_back({"renderer.material.mtoon_textured", "skip", dependent});
    checks.push_back({"renderer.material.mtoon_outline", "skip", dependent});
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
