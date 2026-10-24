// SPDX-License-Identifier: Apache-2.0
// Vulkan/WebGPU parity on bounded canonical scenes (WebGPU policy §9): the
// same extracted DrawList rendered by both backends on one machine, then
// compared per channel. Parity is semantic and visual, not pixel identity:
// each scene class has measured limits on the largest difference and on
// the share of pixels that differ by more than one step (WGPU-Q1, report
// 91), and the depth products must agree on coverage.
#include <toon/vulkan_backend.hpp>
#include <toon/webgpu_backend.hpp>

#include "capture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kExtent = 128;
constexpr float kPi = 3.14159265358979F;

struct Limits {
  // The largest difference in any channel, in 8-bit steps.
  int max_difference = 0;
  // The share of pixels with any channel more than one step apart.
  double above_one = 0.0;
};

// WGPU-Q1's limits per scene class, from the first measured scenes
// (report 91). Unlit output is exact. Lit and textured output may round
// one step apart where the two compilers' arithmetic or the samplers'
// filtering differ in the last bit, and no further: a difference above
// one is explained or fixed, not absorbed (WebGPU policy §9).
constexpr Limits kUnlitLimits{0, 0.0};
constexpr Limits kLitLimits{1, 0.0};
constexpr Limits kTexturedLimits{1, 0.0};
// Depth is the same rasterization of the same clip positions.
constexpr double kDepthLimit = 1e-6;

struct Scene {
  std::string name;
  Limits limits;
  std::function<void(Toon::RenderWorld&, Toon::DrawList*)> build;
};

Toon::Matrix4 Perspective(float fov_y, float aspect, float near, float far) {
  Toon::Matrix4 m;
  const float f = 1.0F / std::tan(fov_y * 0.5F);
  m.m = {};
  m.m[0] = f / aspect;
  m.m[5] = f;
  m.m[10] = (far + near) / (near - far);
  m.m[11] = -1.0F;
  m.m[14] = 2.0F * far * near / (near - far);
  return m;
}

// A camera at `eye` looking at the origin, y up.
Toon::Matrix4 LookAtOrigin(Toon::Float3 eye) {
  const auto normalize = [](Toon::Float3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return Toon::Float3{v.x / l, v.y / l, v.z / l};
  };
  const auto cross = [](Toon::Float3 a, Toon::Float3 b) {
    return Toon::Float3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  };
  const auto dot = [](Toon::Float3 a, Toon::Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  const Toon::Float3 forward = normalize({-eye.x, -eye.y, -eye.z});
  const Toon::Float3 side = normalize(cross(forward, {0, 1, 0}));
  const Toon::Float3 up = cross(side, forward);
  Toon::Matrix4 m;
  m.m = {side.x, up.x, -forward.x, 0, side.y, up.y, -forward.y, 0, side.z, up.z, -forward.z, 0,
      -dot(side, eye), -dot(up, eye), dot(forward, eye), 1};
  return m;
}

void SetCamera(Toon::RenderWorld& world, Toon::Float3 eye) {
  world.SetView({LookAtOrigin(eye), Perspective(0.8F, 1.0F, 0.1F, 20.0F)});
}

// A UV sphere of radius `radius` at `center`, with analytic normals and USD
// st (origin bottom left).
Toon::MeshId AddSphere(Toon::RenderWorld& world, float radius, Toon::Float3 center,
    std::uint32_t rings = 24, std::uint32_t segments = 48) {
  std::vector<Toon::Float3> points;
  std::vector<Toon::Float3> normals;
  std::vector<Toon::Float2> uvs;
  for (std::uint32_t ring = 0; ring <= rings; ++ring) {
    const float v = static_cast<float>(ring) / static_cast<float>(rings);
    const float theta = v * kPi;
    for (std::uint32_t segment = 0; segment <= segments; ++segment) {
      const float u = static_cast<float>(segment) / static_cast<float>(segments);
      const float phi = u * 2.0F * kPi;
      const Toon::Float3 n{std::sin(theta) * std::cos(phi), std::cos(theta),
          std::sin(theta) * std::sin(phi)};
      normals.push_back(n);
      points.push_back({center.x + n.x * radius, center.y + n.y * radius,
          center.z + n.z * radius});
      uvs.push_back({u, 1.0F - v});
    }
  }
  std::vector<std::uint32_t> indices;
  const std::uint32_t row = segments + 1;
  for (std::uint32_t ring = 0; ring < rings; ++ring) {
    for (std::uint32_t segment = 0; segment < segments; ++segment) {
      const std::uint32_t a = ring * row + segment;
      const std::uint32_t b = a + row;
      // Counter-clockwise seen from outside.
      indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
    }
  }
  const Toon::MeshId mesh = world.CreateMesh();
  world.SetMeshPoints(mesh, std::move(points));
  world.SetMeshTopology(mesh, std::move(indices));
  world.SetMeshNormals(mesh, std::move(normals));
  world.SetMeshUVs(mesh, std::move(uvs));
  return mesh;
}

// A square of side `size` in the xz plane, facing +y, st over [0, 1].
Toon::MeshId AddGround(Toon::RenderWorld& world, float size) {
  const float h = size * 0.5F;
  const Toon::MeshId mesh = world.CreateMesh();
  world.SetMeshPoints(mesh, {{-h, 0, h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h}});
  world.SetMeshTopology(mesh, {0, 1, 2, 0, 2, 3});
  world.SetMeshNormals(mesh, std::vector<Toon::Float3>(4, {0, 1, 0}));
  world.SetMeshUVs(mesh, {{0, 0}, {1, 0}, {1, 1}, {0, 1}});
  return mesh;
}

Toon::TextureId AddChecker(Toon::RenderWorld& world, Toon::ToonTextureEncoding encoding,
    std::uint32_t size, std::uint32_t cell, bool alpha_holes) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4U);
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      const bool on = ((x / cell) + (y / cell)) % 2U == 0U;
      std::uint8_t* p = pixels.data() + (static_cast<std::size_t>(y) * size + x) * 4U;
      p[0] = on ? 230 : 40;
      p[1] = on ? 180 : 90;
      p[2] = static_cast<std::uint8_t>(x * 255U / (size - 1U));
      p[3] = alpha_holes && !on ? 40 : 255;
    }
  }
  const Toon::TextureId texture = world.CreateTexture();
  Toon::ToonTexture values;
  values.width = values.height = size;
  values.encoding = encoding;
  values.pixels = std::make_shared<const std::vector<std::uint8_t>>(std::move(pixels));
  world.SetTexture(texture, values);
  return texture;
}

// A tangent-space normal map of bumps, linear, +Y toward +V.
Toon::TextureId AddBumps(Toon::RenderWorld& world, std::uint32_t size) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4U);
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      const float nx = 0.5F * std::sin(static_cast<float>(x) * 2.0F * kPi * 4.0F / size);
      const float ny = 0.5F * std::sin(static_cast<float>(y) * 2.0F * kPi * 3.0F / size);
      const float nz = std::sqrt(std::max(0.0F, 1.0F - nx * nx - ny * ny));
      std::uint8_t* p = pixels.data() + (static_cast<std::size_t>(y) * size + x) * 4U;
      p[0] = static_cast<std::uint8_t>(std::lround((nx * 0.5F + 0.5F) * 255.0F));
      p[1] = static_cast<std::uint8_t>(std::lround((ny * 0.5F + 0.5F) * 255.0F));
      p[2] = static_cast<std::uint8_t>(std::lround((nz * 0.5F + 0.5F) * 255.0F));
      p[3] = 255;
    }
  }
  const Toon::TextureId texture = world.CreateTexture();
  Toon::ToonTexture values;
  values.width = values.height = size;
  values.encoding = Toon::ToonTextureEncoding::Linear;
  values.pixels = std::make_shared<const std::vector<std::uint8_t>>(std::move(pixels));
  world.SetTexture(texture, values);
  return texture;
}

Toon::ToonMaterial Preview(Toon::Float3 base, float metallic, float roughness) {
  Toon::ToonMaterial material;
  material.preview.enabled = true;
  material.base_color = base;
  material.preview.metallic = metallic;
  material.preview.roughness = roughness;
  return material;
}

void Bind(Toon::RenderWorld& world, Toon::MeshId mesh, const Toon::ToonMaterial& values) {
  const Toon::MaterialId material = world.CreateMaterial();
  world.SetMaterial(material, values);
  world.SetMeshMaterial(mesh, material);
}

std::vector<Scene> Scenes() {
  std::vector<Scene> scenes;
  scenes.push_back({"unlit", kUnlitLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    SetCamera(world, {0, 1.5F, 4});
    const auto sphere = AddSphere(world, 1, {0, 0, 0});
    Toon::ToonMaterial diagnostic;
    diagnostic.base_color = {0.9F, 0.3F, 0.15F};
    Bind(world, sphere, diagnostic);
    const auto ground = AddGround(world, 5);
    Toon::ToonMaterial floor;
    floor.base_color = {0.2F, 0.5F, 0.3F};
    Toon::Matrix4 down;
    down.m[13] = -1.0F;
    world.SetMeshTransform(ground, down);
    Bind(world, ground, floor);
  }});
  scenes.push_back({"display-colour", kLitLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    SetCamera(world, {0, 1, 4});
    const auto sphere = AddSphere(world, 1.2F, {0, 0, 0});
    world.SetMeshColor(sphere, {0.3F, 0.6F, 0.9F});
  }});
  scenes.push_back({"preview-dielectric", kLitLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    SetCamera(world, {0, 1, 4});
    Bind(world, AddSphere(world, 1.2F, {0, 0, 0}), Preview({0.8F, 0.35F, 0.1F}, 0, 0.5F));
  }});
  scenes.push_back({"preview-metal", kLitLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    SetCamera(world, {0, 1, 4});
    Bind(world, AddSphere(world, 1.2F, {0, 0, 0}), Preview({0.95F, 0.64F, 0.54F}, 1, 0.3F));
  }});
  scenes.push_back({"preview-scene-lights", kLitLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    SetCamera(world, {0, 2, 5});
    Bind(world, AddSphere(world, 1, {0, 0, 0}), Preview({0.7F, 0.7F, 0.7F}, 0, 0.4F));
    const auto ground = AddGround(world, 6);
    Toon::Matrix4 down;
    down.m[13] = -1.0F;
    world.SetMeshTransform(ground, down);
    Bind(world, ground, Preview({0.5F, 0.5F, 0.5F}, 0, 0.8F));
    Toon::ToonLight point;
    point.type = Toon::ToonLightType::Point;
    point.color = {6, 3, 1};
    point.position = {1.5F, 1.2F, 1.5F};
    point.radius = 0.05F;
    point.shadow = false;
    world.SetLight(world.CreateLight(), point);
    Toon::ToonLight spot;
    spot.type = Toon::ToonLightType::Spot;
    spot.color = {1, 3, 8};
    spot.position = {-2, 2.5F, 0.5F};
    spot.direction = {0.6F, -0.8F, -0.1F};
    spot.cone_angle = 35;
    spot.cone_softness = 0.3F;
    spot.shadow = false;
    world.SetLight(world.CreateLight(), spot);
    Toon::ToonLight ambient;
    ambient.type = Toon::ToonLightType::Ambient;
    ambient.color = {0.08F, 0.08F, 0.1F};
    world.SetLight(world.CreateLight(), ambient);
  }});
  scenes.push_back({"preview-textured", kTexturedLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    // A receding plane: its far texels are minified through every level.
    world.SetView({LookAtOrigin({0, 0.8F, 3.5F}), Perspective(0.9F, 1.0F, 0.1F, 50.0F)});
    const auto ground = AddGround(world, 20);
    Toon::Matrix4 down;
    down.m[13] = -0.5F;
    world.SetMeshTransform(ground, down);
    auto material = Preview({1, 1, 1}, 0, 0.9F);
    material.base_texture.texture =
        AddChecker(world, Toon::ToonTextureEncoding::Srgb, 64, 8, false);
    material.base_texture.scale = {6, 6};
    material.preview.inputs[0].connected = true;
    material.double_sided = true;
    Bind(world, ground, material);
  }});
  scenes.push_back({"preview-normal-map", kTexturedLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    SetCamera(world, {0, 1, 4});
    auto material = Preview({0.6F, 0.6F, 0.65F}, 0, 0.35F);
    material.normal_texture.texture = AddBumps(world, 128);
    material.preview.inputs[5].connected = true;
    material.preview.inputs[5].scale = {2, 2, 2, 1};
    material.preview.inputs[5].bias = {-1, -1, -1, 0};
    Bind(world, AddSphere(world, 1.2F, {0, 0, 0}), material);
  }});
  scenes.push_back({"preview-mask", kTexturedLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    SetCamera(world, {0, 2.5F, 3});
    auto material = Preview({1, 1, 1}, 0, 0.6F);
    const auto checker = AddChecker(world, Toon::ToonTextureEncoding::Srgb, 32, 4, true);
    material.base_texture.texture = checker;
    material.preview.opacity_texture.texture = checker;
    material.preview.inputs[0].connected = true;
    material.preview.inputs[4].connected = true;
    material.preview.inputs[4].channel = 3;
    material.alpha_mode = Toon::ToonAlphaMode::Mask;
    material.alpha_cutoff = 0.5F;
    material.double_sided = true;
    Bind(world, AddGround(world, 3), material);
  }});
  scenes.push_back({"preview-blend", kLitLimits, [](Toon::RenderWorld& world, Toon::DrawList*) {
    SetCamera(world, {0, 0.5F, 4.5F});
    Bind(world, AddSphere(world, 0.9F, {-0.5F, 0, -0.6F}), Preview({0.2F, 0.5F, 0.9F}, 0, 0.5F));
    auto glass = Preview({0.9F, 0.4F, 0.2F}, 0, 0.2F);
    glass.alpha_mode = Toon::ToonAlphaMode::Blend;
    glass.alpha = 0.45F;
    Bind(world, AddSphere(world, 0.9F, {0.5F, 0, 0.4F}), glass);
    auto veil = Preview({0.9F, 0.9F, 0.9F}, 0, 0.9F);
    veil.alpha_mode = Toon::ToonAlphaMode::Blend;
    veil.alpha = 0.25F;
    veil.emissive = {0.1F, 0.1F, 0.1F};
    Bind(world, AddSphere(world, 0.6F, {0.2F, 0.3F, 1.4F}), veil);
  }});
  return scenes;
}

struct Comparison {
  int max_difference = 0;
  double mean_difference = 0.0;
  double above_one = 0.0;
  double max_depth = 0.0;
  std::uint32_t coverage_mismatch = 0;
  std::uint32_t covered = 0;
};

Comparison Compare(const Toon::ColorProduct& a, const Toon::ColorProduct& b,
    const Toon::DepthProduct& da, const Toon::DepthProduct& db) {
  Comparison result;
  const std::size_t pixels = static_cast<std::size_t>(a.width) * a.height;
  std::uint64_t sum = 0;
  std::size_t above = 0;
  for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
    int largest = 0;
    for (std::size_t channel = 0; channel < 4; ++channel) {
      const int d = std::abs(int(a.payload[pixel * 4 + channel]) - int(b.payload[pixel * 4 + channel]));
      sum += static_cast<std::uint64_t>(d);
      largest = std::max(largest, d);
    }
    result.max_difference = std::max(result.max_difference, largest);
    above += largest > 1 ? 1U : 0U;
    const bool covered_a = da.payload[pixel] < 1.0F;
    const bool covered_b = db.payload[pixel] < 1.0F;
    result.covered += covered_a ? 1U : 0U;
    result.coverage_mismatch += covered_a != covered_b ? 1U : 0U;
    if (covered_a && covered_b) {
      result.max_depth = std::max(result.max_depth,
          static_cast<double>(std::abs(da.payload[pixel] - db.payload[pixel])));
    }
  }
  result.mean_difference = static_cast<double>(sum) / static_cast<double>(pixels * 4U);
  result.above_one = static_cast<double>(above) / static_cast<double>(pixels);
  return result;
}

// |a - b| per channel, scaled by 16 so a step of one shows, opaque.
Toon::ColorProduct Difference(const Toon::ColorProduct& a, const Toon::ColorProduct& b) {
  Toon::ColorProduct difference = a;
  for (std::size_t i = 0; i < a.payload.size(); i += 4) {
    int largest = 0;
    for (std::size_t c = 0; c < 4; ++c) {
      largest = std::max(largest, std::abs(int(a.payload[i + c]) - int(b.payload[i + c])));
    }
    const auto v = static_cast<std::uint8_t>(std::min(255, largest * 16));
    difference.payload[i] = v;
    difference.payload[i + 1] = v;
    difference.payload[i + 2] = v;
    difference.payload[i + 3] = 255;
  }
  return difference;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 4) {
      std::cerr << "usage: toon-webgpu-parity-test <spirv dir> <wgsl dir> <summary.json> "
                   "[--images <dir>] [--webgpu-adapter d3d12|vulkan]\n";
      return 2;
    }
    const Toon::SceneShaders vulkan_shaders = Toon::SceneShadersIn(argv[1]);
    const Toon::WebGpuShaders webgpu_shaders = Toon::WebGpuShadersIn(argv[2]);
    const std::filesystem::path summary_path(argv[3]);
    std::filesystem::path images;
    Toon::WebGpuAdapter adapter = Toon::WebGpuAdapter::Direct3D12;
    for (int index = 4; index + 1 < argc; index += 2) {
      const std::string_view option(argv[index]);
      if (option == "--images") {
        images = argv[index + 1];
      } else if (option == "--webgpu-adapter") {
        adapter = std::string_view(argv[index + 1]) == "vulkan" ? Toon::WebGpuAdapter::Vulkan
                                                                : Toon::WebGpuAdapter::Direct3D12;
      } else {
        return 2;
      }
    }
    if (!images.empty()) std::filesystem::create_directories(images);

    Toon::FrameStatus status = Toon::FrameStatus::Fail;
    std::string error;
    // A renderer per scene: each scene's world numbers its meshes,
    // materials and revisions from 1, which a renderer that had drawn
    // another scene would take for resident ones. One sample on both: the
    // WebGPU backend's stage 1 renders at one.
    std::unique_ptr<Toon::OffscreenRenderer> vulkan;
    std::unique_ptr<Toon::OffscreenRenderer> webgpu;
    const auto create = [&]() {
      vulkan = Toon::CreateOffscreenRenderer(vulkan_shaders, status, error, {1});
      if (status == Toon::FrameStatus::Skip) {
        std::cout << "Vulkan: " << error << '\n';
        return false;
      }
      if (!vulkan) throw std::runtime_error(error);
      webgpu = Toon::CreateWebGpuOffscreenRenderer(webgpu_shaders, status, error, {1}, adapter);
      if (status == Toon::FrameStatus::Skip) {
        std::cout << "WebGPU: " << error << '\n';
        return false;
      }
      if (!webgpu) throw std::runtime_error(error);
      return true;
    };
    if (!create()) return 77;

    std::ofstream summary(summary_path, std::ios::binary | std::ios::trunc);
    summary << "{\n  \"vulkan_device\": \"" << vulkan->statistics().device_name
            << "\",\n  \"webgpu\": \"" << webgpu->statistics().api_version << " on "
            << webgpu->statistics().device_name << "\",\n  \"extent\": " << kExtent
            << ",\n  \"samples\": 1,\n  \"scenes\": [\n";
    bool passed = true;
    bool first = true;
    std::uint32_t validation_messages = 0;
    std::string validation_detail;
    for (const Scene& scene : Scenes()) {
      if (!first && !create()) return 77;
      for (const Toon::BackgroundMode mode :
          {Toon::BackgroundMode::Opaque, Toon::BackgroundMode::Transparent}) {
        Toon::RenderWorld world;
        scene.build(world, nullptr);
        Toon::DrawList draws = Toon::ExtractDrawList(world.Commit());
        // Shared shadows arrive on WebGPU with stage 3; both draw none.
        draws.shadows = false;
        draws.mmd_self_shadow = false;
        Toon::BackgroundSettings background;
        background.mode = mode;
        if (!vulkan->SetBackground(background, error) || !webgpu->SetBackground(background, error)) {
          throw std::runtime_error(error);
        }
        Toon::ColorProduct vc, wc;
        Toon::DepthProduct vd, wd;
        if (!vulkan->Render(draws, kExtent, kExtent, vc, vd, error)) {
          throw std::runtime_error("Vulkan " + scene.name + ": " + error);
        }
        if (!webgpu->Render(draws, kExtent, kExtent, wc, wd, error)) {
          throw std::runtime_error("WebGPU " + scene.name + ": " + error);
        }
        const Comparison c = Compare(vc, wc, vd, wd);
        const bool ok = c.max_difference <= scene.limits.max_difference &&
                        c.above_one <= scene.limits.above_one && c.coverage_mismatch == 0 &&
                        c.max_depth <= kDepthLimit && c.covered > 0;
        passed = passed && ok;
        const std::string label = scene.name +
            (mode == Toon::BackgroundMode::Opaque ? "-opaque" : "-transparent");
        summary << (first ? "" : ",\n") << "    {\"scene\": \"" << label
                << "\", \"max_difference\": " << c.max_difference
                << ", \"mean_difference\": " << c.mean_difference
                << ", \"above_one\": " << c.above_one
                << ", \"covered\": " << c.covered
                << ", \"coverage_mismatch\": " << c.coverage_mismatch
                << ", \"max_depth_difference\": " << c.max_depth
                << ", \"limit_max\": " << scene.limits.max_difference
                << ", \"limit_above_one\": " << scene.limits.above_one
                << ", \"pass\": " << (ok ? "true" : "false") << "}";
        first = false;
        std::cout << label << ": max " << c.max_difference << " mean " << c.mean_difference
                  << " above-one " << c.above_one << " coverage-mismatch "
                  << c.coverage_mismatch << " depth " << c.max_depth << (ok ? "" : "  FAIL")
                  << '\n';
        if (!images.empty()) {
          Toon::viewport::WriteForegroundPng((images / (label + "-vulkan.png")).string(), vc);
          Toon::viewport::WriteForegroundPng((images / (label + "-webgpu.png")).string(), wc);
          Toon::viewport::WriteForegroundPng((images / (label + "-difference.png")).string(),
              Difference(vc, wc));
        }
      }
      for (const auto* renderer : {vulkan.get(), webgpu.get()}) {
        validation_messages += renderer->statistics().validation_message_count;
        if (validation_detail.empty()) validation_detail = renderer->statistics().validation_detail;
      }
    }
    summary << "\n  ],\n  \"validation_messages\": " << validation_messages
            << ",\n  \"pass\": " << (passed ? "true" : "false") << "\n}\n";
    if (validation_messages != 0) {
      std::cerr << "validation messages: " << validation_detail << '\n';
      return 1;
    }
    return passed ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
