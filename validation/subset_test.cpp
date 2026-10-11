// SPDX-License-Identifier: Apache-2.0
// A mesh divided into parts by its face subsets draws as separate meshes,
// one per part, with the same geometry and materials would.
#include <toon/vulkan_backend.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

// Four triangles apart from each other, one in each quadrant, so no draw
// covers another and their order cannot change a pixel. Each has its own
// points and authored normals, so dividing the mesh changes no normal.
std::vector<Toon::Float3> Points() {
  std::vector<Toon::Float3> points;
  for (const auto [x, y] : {std::pair{-0.6F, -0.6F}, std::pair{0.2F, -0.6F},
           std::pair{-0.6F, 0.2F}, std::pair{0.2F, 0.2F}}) {
    points.push_back({x, y, 0});
    points.push_back({x + 0.4F, y, 0});
    points.push_back({x + 0.2F, y + 0.4F, 0});
  }
  return points;
}

std::vector<std::uint32_t> Triangles(std::initializer_list<std::uint32_t> which) {
  std::vector<std::uint32_t> indices;
  for (const std::uint32_t triangle : which) {
    for (std::uint32_t corner = 0; corner < 3; ++corner) {
      indices.push_back(triangle * 3U + corner);
    }
  }
  return indices;
}

// An opaque MToon material with a hull, a blended double-sided one, and a
// Mask one.
std::vector<Toon::ToonMaterial> Materials() {
  Toon::ToonMaterial opaque;
  opaque.model = Toon::ToonShadingModel::MToon;
  opaque.base_color = {0.8F, 0.2F, 0.1F};
  opaque.mtoon.shade_color = {0.4F, 0.1F, 0.05F};
  opaque.outline = true;
  opaque.outline_width = 0.01F;
  opaque.outline_color = {0.1F, 0.1F, 0.4F};
  opaque.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::World;
  Toon::ToonMaterial blend = opaque;
  blend.base_color = {0.1F, 0.3F, 0.9F};
  blend.alpha_mode = Toon::ToonAlphaMode::Blend;
  blend.alpha = 0.5F;
  blend.double_sided = true;
  Toon::ToonMaterial mask = opaque;
  mask.base_color = {0.2F, 0.8F, 0.3F};
  mask.outline = false;
  mask.mtoon.outline_width_mode = Toon::ToonOutlineWidthMode::None;
  mask.alpha_mode = Toon::ToonAlphaMode::Mask;
  return {opaque, blend, mask};
}

struct Scene {
  Toon::RenderWorld world;
  std::vector<Toon::MaterialId> materials;

  Scene() {
    for (const Toon::ToonMaterial& values : Materials()) {
      materials.push_back(world.CreateMaterial());
      world.SetMaterial(materials.back(), values);
    }
  }

  Toon::MeshId Mesh(std::vector<std::uint32_t> indices, Toon::MaterialId material) {
    const Toon::MeshId mesh = world.CreateMesh();
    const std::vector<Toon::Float3> points = Points();
    world.SetMeshPoints(mesh, points);
    world.SetMeshNormals(mesh, std::vector<Toon::Float3>(points.size(), {0.3F, 0.2F, 1.0F}));
    world.SetMeshTopology(mesh, std::move(indices));
    world.SetMeshColor(mesh, {0.9F, 0.9F, 0.2F});
    world.SetMeshMaterial(mesh, material);
    return mesh;
  }
};

bool Gpu(const Toon::SceneShaders& shaders) {
  Toon::FrameStatus status;
  std::string error;
  auto renderer = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
  if (status == Toon::FrameStatus::Skip) {
    std::cout << error << '\n';
    return false;
  }
  Require(renderer != nullptr, error.c_str());

  // Triangles 1 and 3 in the opaque subset, 0 in the blended one, and 2 in
  // none, drawn with the mesh's own Mask material; then with none, unlit.
  Scene divided;
  const Toon::MeshId mesh = divided.Mesh(Triangles({1, 3, 0, 2}), divided.materials[2]);
  divided.world.SetMeshSubsets(mesh, {{0, 6, divided.materials[0]}, {6, 3, divided.materials[1]}});

  Toon::ColorProduct color, expected;
  Toon::DepthProduct depth, expected_depth;
  for (const bool bound : {true, false}) {
    if (!bound) divided.world.SetMeshMaterial(mesh, 0);
    Scene oracle;
    oracle.Mesh(Triangles({1, 3}), oracle.materials[0]);
    oracle.Mesh(Triangles({0}), oracle.materials[1]);
    oracle.Mesh(Triangles({2}), bound ? oracle.materials[2] : 0U);
    auto reference = Toon::CreateOffscreenRenderer(shaders, status, error, {1});
    Require(reference != nullptr, error.c_str());

    const Toon::OffscreenStatistics before = renderer->statistics();
    const Toon::DrawList draws = Toon::ExtractDrawList(divided.world.Commit());
    Require(draws.draws.size() == 1 && draws.draws[0].parts->size() == 3,
        "the mesh must commit three parts");
    Require(renderer->Render(draws, 96, 96, color, depth, error), error.c_str());
    Require(reference->Render(Toon::ExtractDrawList(oracle.world.Commit()), 96, 96,
                expected, expected_depth, error),
        error.c_str());
    Require(color.payload == expected.payload, "a divided mesh's colour differs from separate meshes");
    Require(depth.payload == expected_depth.payload, "a divided mesh's depth differs from separate meshes");
    const Toon::OffscreenStatistics& after = renderer->statistics();
    Require(after.validation_message_count == 0, "Vulkan validation reported an error");
    Require(after.outline_draws == reference->statistics().outline_draws,
        "a divided mesh's hulls differ from separate meshes");
    if (!bound) {
      Require(after.topology_uploads == before.topology_uploads &&
              after.point_uploads == before.point_uploads &&
              after.material_writes == before.material_writes,
          "rebinding a mesh's gaps uploaded geometry or materials");
    }
  }
  std::cout << "subsets parts=3 hulls=" << renderer->statistics().outline_draws
            << " topology_uploads=" << renderer->statistics().topology_uploads << '\n';
  return true;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) {
      std::cerr << "usage: toon-subset-test <shader directory>\n";
      return 2;
    }
    const auto capability = Toon::ProbeVulkanBackend();
    if (!capability.available) {
      std::cout << capability.detail << '\n';
      return 77;
    }
    return Gpu(Toon::SceneShadersIn(argv[1])) ? 0 : 77;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
