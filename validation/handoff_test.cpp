// SPDX-License-Identifier: Apache-2.0
// The host that lends its device (GPU handoff policy §2, §9). It creates its
// instance and device with Vulkan itself, from the published requirements,
// with validation and synchronization validation on its own instance; checks
// that a device lacking a requirement is refused with the item named; and
// renders the RGBA fixtures on the lent device and on a device the renderer
// owns, at 1x and 4x, comparing both products byte for byte.
#include <toon/vulkan_handoff.hpp>
#include "lending_host.hpp"
#include "rgba_fixture_scenes.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int kSkip = 77;

using toon_lending::Host;
using toon_lending::Require;
using toon_lending::Skip;

// A device lacking a requirement is refused, the missing item named.
void Refusals(const Host& host, const Toon::SceneShaders& shaders) {
  const auto refused = [&](Toon::LentVulkanDevice lent, std::string_view named) {
    Toon::FrameStatus status = Toon::FrameStatus::Pass;
    std::string detail;
    auto renderer = Toon::CreateOffscreenRenderer(lent, shaders, status, detail);
    Require(renderer == nullptr && status == Toon::FrameStatus::Fail &&
                detail.find(named) != std::string::npos,
        "a lent device without " + std::string(named) + " was not refused by "
        "name: " + detail);
  };
  auto lent = host.Lend();
  lent.enabled.synchronization2 = false;
  refused(lent, "synchronization2");
  lent = host.Lend();
  lent.enabled.timeline_semaphore = false;
  lent.enabled.shader_sampled_image_array_dynamic_indexing = false;
  refused(lent, "shaderSampledImageArrayDynamicIndexing");
  lent = host.Lend();
  lent.api_version = VK_API_VERSION_1_2;
  refused(lent, "Vulkan 1.3");
  lent = host.Lend();
  lent.queue_family = host.family_count();
  refused(lent, "family");
  lent = host.Lend();
  lent.queue = 0;
  refused(lent, "queue");
}

struct Frame {
  Toon::ColorProduct color;
  Toon::DepthProduct depth;
};

void Render(Toon::OffscreenRenderer& renderer, const Toon::DrawList& draws,
    const Toon::BackgroundSettings& background, std::uint32_t width,
    std::uint32_t height, Frame& frame) {
  std::string error;
  Require(renderer.SetBackground(background, error), error);
  Require(renderer.Render(draws, width, height, frame.color, frame.depth, error),
      error);
}

// Every fixture, background and extent, lent against owned.
void Compare(const Host& host, const Toon::SceneShaders& shaders,
    std::uint32_t samples) {
  Toon::FrameStatus status = Toon::FrameStatus::Fail;
  std::string detail;
  auto owned = Toon::CreateOffscreenRenderer(shaders, status, detail, {samples});
  if (status == Toon::FrameStatus::Skip) throw Skip(detail);
  Require(owned != nullptr, detail);
  auto lent = Toon::CreateOffscreenRenderer(host.Lend(), shaders, status, detail,
      {samples});
  Require(lent != nullptr, detail);
  const auto& own = owned->statistics();
  const auto& borrowed = lent->statistics();
  Require(own.vendor_id == borrowed.vendor_id && own.device_id == borrowed.device_id &&
              own.driver_version == borrowed.driver_version,
      "the host chose another device than the renderer: " + borrowed.device_name +
          " against " + own.device_name);
  Require(own.samples == samples && borrowed.samples == samples,
      "requested samples unavailable");
  Require(own.pipelines_created == borrowed.pipelines_created,
      "a lent device built other pipelines");
  std::cout << "device=" << borrowed.device_name << " samples=" << samples
            << " gpu_timing=" << borrowed.gpu_timing << '\n';

  const Toon::BackgroundSettings backgrounds[] = {
      {Toon::BackgroundMode::Transparent, {0, 0, 0}},
      {Toon::BackgroundMode::Opaque, {0, 0, 0}},
      {Toon::BackgroundMode::Opaque, {1, 1, 1}},
  };
  // The fixtures' extent, then a resize and back.
  const std::uint32_t extents[][2] = {{256, 256}, {320, 180}, {256, 256}};
  for (const std::string name : {"avatar", "mmd-shadow"}) {
    Toon::RenderWorld world;
    if (name == "avatar") {
      toon_fixtures::Avatar(world);
    } else {
      toon_fixtures::Shadow(world);
    }
    const auto draws = Toon::ExtractDrawList(world.Commit());
    for (const auto& extent : extents) {
      for (const auto& background : backgrounds) {
        Frame a;
        Frame b;
        Render(*owned, draws, background, extent[0], extent[1], a);
        Render(*lent, draws, background, extent[0], extent[1], b);
        const std::string where = name + " " + std::to_string(samples) + "x " +
                                  std::to_string(extent[0]) + "x" +
                                  std::to_string(extent[1]);
        Require(!a.color.payload.empty() && a.color.payload == b.color.payload,
            where + ": the lent device's colour differs from the owned one's");
        Require(a.depth.payload == b.depth.payload,
            where + ": the lent device's depth differs from the owned one's");
        Require(a.color.alpha_convention == b.color.alpha_convention &&
                    a.color.color_space == b.color.color_space &&
                    a.color.origin == b.color.origin,
            where + ": the products' interpretation differs");
        Require(borrowed.readback_bytes ==
                    std::uint64_t{extent[0]} * extent[1] * 8U,
            where + ": unexpected readback bytes");
        Require(!borrowed.gpu_timing || borrowed.gpu_frame_ms > 0,
            where + ": the GPU frame time was not measured");
      }
    }
    std::cout << name << ' ' << samples << "x: lent equals owned\n";
  }
  // A steady frame on the lent device rebuilds nothing, as on an owned one.
  Require(borrowed.pipelines_created == own.pipelines_created &&
              borrowed.texture_uploads == own.texture_uploads &&
              borrowed.topology_uploads == own.topology_uploads &&
              borrowed.target_allocations == own.target_allocations,
      "the lent device's uploads or allocations differ from the owned one's");
  Require(own.validation_message_count == 0,
      "Vulkan validation messages on the owned device: " + own.validation_detail);
  Require(host.messages().count == 0,
      "Vulkan validation messages on the lent device: " + host.messages().first);
}

} // namespace

int main(int argc, char** argv) {
  try {
    Require(argc == 2, "expected the shader directory");
    const auto shaders = Toon::SceneShadersIn(argv[1]);
    const auto requirements = Toon::VulkanHandoffRequirements();
    Require(requirements.api_version != 0,
        "the Vulkan backend publishes no requirements");
    Host host(requirements, true);
    std::cout << "validation=" << host.validation() << '\n';
    Refusals(host, shaders);
    for (const std::uint32_t samples : {1U, 4U}) {
      Compare(host, shaders, samples);
    }
    // Every renderer has gone; the device and what the host made remain the
    // host's, and destroying the device reports nothing left on it.
    host.DestroyDevice();
    Require(host.messages().count == 0,
        "a renderer left objects on the lent device: " + host.messages().first);
    std::cout << "lent and owned devices render identical products\n";
    return 0;
  } catch (const Skip& skip) {
    std::cout << "SKIP: " << skip.what() << '\n';
    return kSkip;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
