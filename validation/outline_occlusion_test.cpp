// SPDX-License-Identifier: Apache-2.0
#include <toon/extraction.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <random>

namespace {
bool Check(bool condition, const char* message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}
} // namespace

int main() {
  Toon::RenderWorld world;
  const auto target = world.CreateMesh();
  world.SetMeshPoints(target, {{-0.05F, -0.05F, 0.5F}, {0.05F, -0.05F, 0.5F}, {0, 0.05F, 0.5F}});
  world.SetMeshTopology(target, {0, 1, 2});
  const auto blocker = world.CreateMesh();
  world.SetMeshPoints(blocker, {{-2, -2, 0}, {2, -2, 0}, {0, 2, 0}});
  world.SetMeshTopology(blocker, {0, 1, 2});
  const auto material = world.CreateMaterial();
  world.SetMeshMaterial(blocker, material);
  Toon::ToonMaterial opaque;
  opaque.model = Toon::ToonShadingModel::MToon;
  opaque.double_sided = true;
  world.SetMaterial(material, opaque);
  Toon::OutlineBounds bounds;
  Toon::OutlineOcclusion occlusion;
  auto draws = Toon::ExtractDrawList(world.Commit());
  auto mesh = draws.draws[0];
  bounds.Update(mesh);
  const auto hidden = [&](float width = 0.01F,
                          Toon::ToonOutlineWidthMode mode = Toon::ToonOutlineWidthMode::World,
                          float units = 1) {
    return occlusion.Occludes(mesh, bounds, draws.view, width, mode, units);
  };
  occlusion.Update(draws, 256, 256);
  if (!Check(hidden(), "fully covered farther hull should be omitted"))
    return 1;
  if (!Check(!hidden(1) && !hidden(0.01F, Toon::ToonOutlineWidthMode::World, 0.01F) &&
                 !hidden(0.5F, Toon::ToonOutlineWidthMode::Screen),
          "extrusion reaching past a blocker must stay visible"))
    return 1;
  mesh.transform.m[14] = -0.5F;
  if (!Check(!hidden(), "depth ties cannot establish occlusion"))
    return 1;
  mesh.transform.m[14] = -0.7F;
  if (!Check(!hidden(), "a hull in front of a blocker must survive"))
    return 1;
  mesh.transform = {};
  mesh.transform.m[12] = 1.1F;
  if (!Check(!hidden(), "partial triangle coverage must retain the hull"))
    return 1;
  mesh.transform = {};
  // Material changes are current-frame values, including Opaque restoration.
  for (const auto alpha : {Toon::ToonAlphaMode::Mask, Toon::ToonAlphaMode::Blend, Toon::ToonAlphaMode::Opaque}) {
    opaque.alpha_mode = alpha;
    opaque.mtoon.transparent_with_z_write = true;
    world.SetMaterial(material, opaque);
    draws = Toon::ExtractDrawList(world.Commit());
    occlusion.Update(draws, 256, 256);
    if (!Check(hidden() == (alpha == Toon::ToonAlphaMode::Opaque),
            "Mask/Blend, even with depth writes, cannot certify coverage"))
      return 1;
  }
  opaque.double_sided = false;
  world.SetMaterial(material, opaque);
  draws = Toon::ExtractDrawList(world.Commit());
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "single-sided MToon blockers are conservatively ignored"))
    return 1;
  opaque.model = Toon::ToonShadingModel::PreviewSurface;
  world.SetMaterial(material, opaque);
  draws = Toon::ExtractDrawList(world.Commit());
  occlusion.Update(draws, 256, 256);
  if (!Check(hidden(), "the unlit fallback is opaque and unculled"))
    return 1;
  auto stale_view = draws.view;
  stale_view.view.m[12] = 1;
  if (!Check(!occlusion.Occludes(mesh, bounds, stale_view, 0.01F, Toon::ToonOutlineWidthMode::World, 1),
          "a stale camera certificate must not be used"))
    return 1;
  occlusion.Update(draws, 0, 256);
  if (!Check(!hidden(), "unknown raster dimensions must retain hulls"))
    return 1;
  occlusion.Update(draws, 1, 1);
  if (!Check(!hidden(), "raster safety margins must retain tiny-target hulls"))
    return 1;
  const auto saved_blocker = draws.draws[1];
  draws.draws[1].transform.m[12] = 4;
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "a moved blocker must restore the hull this frame"))
    return 1;
  draws.draws[1] = saved_blocker;
  occlusion.Update(draws, 256, 256);
  if (!Check(hidden(), "a returning blocker must establish coverage this frame"))
    return 1;
  draws.draws[1].transform.m[14] = -2;
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "a depth-clipped blocker cannot certify coverage"))
    return 1;
  draws.draws[1] = saved_blocker;
  draws.draws[1].transform.m[0] = std::numeric_limits<float>::quiet_NaN();
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "nonfinite blocker transforms cannot certify coverage"))
    return 1;
  draws.draws[1] = saved_blocker;
  draws.view.view.m[12] = 1e7F;
  draws.draws[1].transform.m[12] = -1e7F;
  mesh.transform.m[12] = -1e7F;
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "large cancelling transforms must retain uncertain hulls"))
    return 1;
  draws.view = {};
  mesh.transform = {};
  draws.draws[1] = saved_blocker;
  world.SetMeshPoints(blocker, {{-0.01F, -0.01F, 0}, {0.01F, -0.01F, 0}, {0, 0.01F, 0}});
  draws = Toon::ExtractDrawList(world.Commit());
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "edited blocker points must invalidate previous coverage"))
    return 1;
  world.SetMeshPoints(blocker, {{-2, -2, 0}, {2, -2, 0}, {0, 2, 0}});
  world.SetMeshTopology(blocker, {0, 0, 0});
  draws = Toon::ExtractDrawList(world.Commit());
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "edited degenerate topology must invalidate coverage"))
    return 1;
  std::vector<std::uint32_t> large_topology;
  for (unsigned triangle = 0; triangle < 65; ++triangle)
    large_topology.insert(large_topology.end(), {0, 1, 2});
  world.SetMeshTopology(blocker, large_topology);
  draws = Toon::ExtractDrawList(world.Commit());
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "large blocker meshes must honor the geometry work limit"))
    return 1;
  world.SetMeshTopology(blocker, {0, 2, 1});
  draws = Toon::ExtractDrawList(world.Commit());
  occlusion.Update(draws, 256, 256);
  if (!Check(hidden(), "reversed opaque winding must still establish coverage"))
    return 1;
  Toon::ToonSkin skin;
  skin.influences_per_point = 1;
  skin.constant = true;
  skin.influences = {{0, 1}};
  world.SetMeshSkin(blocker, skin);
  draws = Toon::ExtractDrawList(world.Commit());
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "skinned blockers cannot use their undeformed triangles"))
    return 1;
  world.SetMeshSkin(blocker, {});
  world.SetMeshVisible(blocker, false);
  draws = Toon::ExtractDrawList(world.Commit());
  occlusion.Update(draws, 256, 256);
  if (!Check(!hidden(), "a removed or hidden blocker must immediately restore the hull"))
    return 1;
  world.SetMeshVisible(blocker, true);
  draws = Toon::ExtractDrawList(world.Commit());

  // Independent sampled-hull oracle: translations and world/screen widths
  // across perspective/orthographic views, reversed winding and mirrored
  // blockers. Every omitted envelope's samples must be inside the actual
  // triangle and farther away, without the implementation's safety margins.
  std::mt19937 random(9465);
  std::uniform_real_distribution<float> translation(-1.4F, 1.4F);
  std::uniform_real_distribution<float> widths(0.001F, 0.15F);
  unsigned omitted = 0, retained = 0;
  for (const bool perspective : {false, true}) {
    draws.view = {};
    draws.view.projection.m[10] = -0.2F;
    if (perspective) {
      draws.view.projection.m[10] = -4.5F / 3.5F;
      draws.view.projection.m[14] = -4.0F / 3.5F;
      draws.view.projection.m[11] = -1;
      draws.view.projection.m[15] = 0;
    }
    draws.draws[1].transform.m[14] = -1;
    for (const bool mirrored : {false, true}) {
      draws.draws[1].transform.m[0] = mirrored ? -1.0F : 1.0F;
      occlusion.Update(draws, 256, 256);
      for (unsigned trial = 0; trial < 500; ++trial) {
        mesh.transform = {};
        mesh.transform.m[12] = translation(random);
        mesh.transform.m[13] = translation(random);
        mesh.transform.m[14] = -2.5F;
        const float width = widths(random);
        const auto mode = trial % 2 ? Toon::ToonOutlineWidthMode::World : Toon::ToonOutlineWidthMode::Screen;
        if (!hidden(width, mode)) {
          ++retained;
          continue;
        }
        ++omitted;
        const double radius = mode == Toon::ToonOutlineWidthMode::Screen ? (perspective ? 4.0 : 2.0) * width : width;
        for (const auto& point : *mesh.points) {
          for (unsigned sample = 0; sample < 40; ++sample) {
            const double azimuth = 6.283185307 * sample / 40;
            for (const double z : {-1.0, 0.0, 1.0}) {
              const double r = radius * std::sqrt(1 - z * z);
              const double sample_z = -2.0 + radius * z;
              double x = point.x + mesh.transform.m[12] + r * std::cos(azimuth);
              double y = point.y + mesh.transform.m[13] + r * std::sin(azimuth);
              if (perspective) {
                x /= -sample_z;
                y /= -sample_z;
              }
              // Triangle (-2,-2), (2,-2), (0,2), both projected views.
              if (!Check(sample_z < -1 && y > -2 && 2 * x + y < 2 && -2 * x + y < 2,
                      "omission discarded a sampled visible hull point")) {
                std::cerr << "perspective=" << perspective << " trial=" << trial << " width=" << width
                          << " sample=" << x << ',' << y << ',' << sample_z << '\n';
                return 1;
              }
            }
          }
        }
      }
    }
  }
  if (!Check(omitted > 100 && retained > 100, "oracle needs omitted and visible cases"))
    return 1;
  mesh.transform.m[0] = 0;
  if (!Check(!hidden(), "singular target transforms must survive"))
    return 1;
  mesh.transform = {};
  mesh.transform.m[12] = std::numeric_limits<float>::infinity();
  if (!Check(!hidden(), "nonfinite target data must survive"))
    return 1;
  std::cout << "2000 sampled hulls: " << omitted << " omitted, " << retained << " retained\n";
  return 0;
}
