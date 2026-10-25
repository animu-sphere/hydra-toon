// SPDX-License-Identifier: Apache-2.0
// The measurements of GPU handoff policy §8. A USD stage loads through
// UsdImaging and hdToon's delegate; its committed draw list, framed by the
// viewport's camera, renders on a device this host lends, without the
// validation layer, at each extent and sample count. Per frame it records
// the render call's CPU time, the GPU frame time, the bytes read back and the
// latency from the render call's start to the product being ready, and
// writes their p50/p95/p99 as JSON, for the readback path and, beside it on
// the same renderer, the handoff.
#include "adapter.hpp"
#include "camera.hpp"
#include "lending_host.hpp"

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
#include <pxr/usdImaging/usdImaging/stageSceneIndex.h>

#include <toon/vulkan_handoff.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

using toon_lending::Require;

constexpr int kSkip = 77;

struct Arguments {
  std::string shaders;
  std::string stage;
  std::string output;
  std::uint32_t frames = 120;
  std::uint32_t warmup = 10;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> extents;
  std::vector<std::uint32_t> samples;
};

std::pair<std::uint32_t, std::uint32_t> ParseExtent(const std::string& text) {
  const auto x = text.find('x');
  Require(x != std::string::npos, "an extent is WIDTHxHEIGHT: " + text);
  const auto width = static_cast<std::uint32_t>(std::stoul(text.substr(0, x)));
  const auto height = static_cast<std::uint32_t>(std::stoul(text.substr(x + 1)));
  Require(width > 0 && height > 0, "an extent must be non-zero: " + text);
  return {width, height};
}

Arguments Parse(int argc, char** argv) {
  Arguments arguments;
  for (int at = 1; at < argc; ++at) {
    const std::string argument = argv[at];
    const auto value = [&]() -> std::string {
      Require(at + 1 < argc, argument + " needs a value");
      return argv[++at];
    };
    if (argument == "--shaders") arguments.shaders = value();
    else if (argument == "--stage") arguments.stage = value();
    else if (argument == "--output") arguments.output = value();
    else if (argument == "--frames") arguments.frames = static_cast<std::uint32_t>(std::stoul(value()));
    else if (argument == "--warmup") arguments.warmup = static_cast<std::uint32_t>(std::stoul(value()));
    else if (argument == "--extent") arguments.extents.push_back(ParseExtent(value()));
    else if (argument == "--samples") arguments.samples.push_back(static_cast<std::uint32_t>(std::stoul(value())));
    else Require(false, "unknown argument " + argument);
  }
  Require(!arguments.shaders.empty() && !arguments.stage.empty() &&
              !arguments.output.empty(),
      "usage: toon-handoff-measure --shaders DIR --stage FILE --output FILE "
      "[--frames N] [--warmup N] [--extent WxH]... [--samples N]...");
  Require(arguments.frames > 0, "--frames must be at least 1");
  // Policy §8's extents and sample counts unless others are given.
  if (arguments.extents.empty()) arguments.extents = {{1920, 1080}, {3840, 2160}};
  if (arguments.samples.empty()) arguments.samples = {1, 4};
  return arguments;
}

// The stage through UsdImaging into hdToon's delegate, synced once.
struct Scene {
  HdToonRenderDelegate delegate;
  UsdImagingSceneIndices indices;
  std::unique_ptr<HdRenderIndex> index;
  Toon::viewport::UpAxis up = Toon::viewport::UpAxis::Y;

  explicit Scene(const UsdStageRefPtr& stage) {
    UsdImagingCreateSceneIndicesInfo info;
    info.stage = stage;
    indices = UsdImagingCreateSceneIndices(info);
    index.reset(HdRenderIndex::New(&delegate, {}));
    Require(index != nullptr, "could not create the Hydra render index");
    index->InsertSceneIndex(indices.finalSceneIndex, SdfPath::AbsoluteRootPath());
    indices.stageSceneIndex->SetTime(stage->HasAuthoredTimeCodeRange()
                                         ? UsdTimeCode(stage->GetStartTimeCode())
                                         : UsdTimeCode::Default());
    up = UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z
             ? Toon::viewport::UpAxis::Z
             : Toon::viewport::UpAxis::Y;
  }
  ~Scene() { index.reset(); }

  Toon::FrameSnapshot Sync() {
    indices.stageSceneIndex->ApplyPendingUpdates();
    index->EnqueueCollectionToSync(HdRprimCollection(HdTokens->geometry,
        HdReprSelector(HdReprTokens->smoothHull)));
    HdTaskSharedPtrVector tasks;
    HdTaskContext context;
    index->SyncAll(&tasks, &context);
    return delegate.CommitScene();
  }
};

struct Series {
  std::vector<double> values;

  // Nearest rank.
  [[nodiscard]] double Percentile(double rank) const {
    if (values.empty()) return 0;
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const auto at = static_cast<std::size_t>(
        std::ceil(rank / 100.0 * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(at, 1, sorted.size()) - 1];
  }
  void Write(std::ostream& out, const char* name) const {
    double sum = 0;
    double max = 0;
    for (const double value : values) {
      sum += value;
      max = std::max(max, value);
    }
    out << '"' << name << "\": {\"p50\": " << Percentile(50)
        << ", \"p95\": " << Percentile(95) << ", \"p99\": " << Percentile(99)
        << ", \"mean\": " << (values.empty() ? 0 : sum / values.size())
        << ", \"max\": " << max << '}';
  }
};

std::string Escape(const std::string& text) {
  std::string escaped;
  for (const char c : text) {
    if (c == '"' || c == '\\') escaped += '\\';
    escaped += c;
  }
  return escaped;
}

double Milliseconds(std::int64_t from, std::int64_t to) {
  return static_cast<double>(to - from) * 1e-6;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Arguments arguments = Parse(argc, argv);
    const auto stage = UsdStage::Open(arguments.stage);
    Require(stage != nullptr, "cannot open " + arguments.stage);
    Scene scene(stage);
    const Toon::FrameSnapshot snapshot = scene.Sync();
    Toon::viewport::OrbitCamera camera(scene.up);
    camera.Frame(Toon::viewport::SceneBounds(snapshot));
    Toon::DrawList draws = Toon::ExtractDrawList(snapshot);

    // Validation off: the layer's own CPU cost is not the renderer's.
    toon_lending::Host host(Toon::VulkanHandoffRequirements(), false);
    const auto shaders = Toon::SceneShadersIn(arguments.shaders);

    std::ostringstream runs;
    runs << std::setprecision(6);
    std::string device;
    std::string driver;
    bool first = true;
    for (const std::uint32_t samples : arguments.samples) {
      Toon::FrameStatus status = Toon::FrameStatus::Fail;
      std::string error;
      auto renderer = Toon::CreateHandoffRenderer(host.Lend(), shaders, status,
          error, {samples});
      Require(renderer != nullptr, error);
      const auto& statistics = renderer->statistics();
      device = statistics.device_name;
      driver = statistics.driver_version;
      for (const auto& [width, height] : arguments.extents) {
        draws.view = camera.View(static_cast<float>(width) / height);
        for (const bool handoff : {false, true}) {
          Toon::ColorProduct color;
          Toon::DepthProduct depth;
          Series cpu;
          Series gpu;
          Series ready;
          Series submit_to_ready;
          std::uint64_t readback_bytes = 0;
          std::uint64_t previous = 0;
          for (std::uint32_t frame = 0; frame < arguments.warmup + arguments.frames;
               ++frame) {
            const std::int64_t start = Toon::SteadyNanoseconds();
            std::int64_t end = 0;
            std::int64_t done = 0;
            std::int64_t submitted = 0;
            if (handoff) {
              Toon::HandoffImage image;
              Require(renderer->RenderHandoff(draws, width, height, image, error) ==
                          Toon::HandoffStatus::Rendered,
                  error);
              end = Toon::SteadyNanoseconds();
              submitted = image.submitted;
              // A consumer that reads nothing: acquired and released at
              // once. The product is ready when its value is signalled,
              // which this host waits for on the CPU only to time it.
              Toon::HandoffImage acquired;
              Require(renderer->Acquire(acquired) &&
                          renderer->Release(acquired, 0, 0, error),
                  error);
              const auto semaphore = reinterpret_cast<VkSemaphore>(image.semaphore);
              VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
              wait.semaphoreCount = 1;
              wait.pSemaphores = &semaphore;
              wait.pValues = &image.value;
              Require(vkWaitSemaphores(host.device(), &wait, 10'000'000'000ULL) ==
                          VK_SUCCESS,
                  "waiting for a handed-off frame failed");
              done = Toon::SteadyNanoseconds();
            } else {
              Require(renderer->Render(draws, width, height, color, depth, error),
                  error);
              // The readback path's product is ready when the call returns.
              end = done = Toon::SteadyNanoseconds();
              submitted = statistics.latency.submitted;
            }
            // A handed-off frame's GPU time is read by the next call.
            const bool timed = statistics.gpu_timing &&
                               statistics.gpu_frame ==
                                   (handoff ? previous : statistics.latency.frame);
            previous = statistics.latency.frame;
            if (frame < arguments.warmup) continue;
            cpu.values.push_back(Milliseconds(start, end));
            ready.values.push_back(Milliseconds(start, done));
            submit_to_ready.values.push_back(Milliseconds(submitted, done));
            if (timed) gpu.values.push_back(statistics.gpu_frame_ms);
            readback_bytes = statistics.readback_bytes;
          }
          const char* path = handoff ? "handoff" : "readback";
          runs << (first ? "" : ",\n") << "    {\"path\": \"" << path
               << "\", \"width\": " << width << ", \"height\": " << height
               << ", \"samples\": " << statistics.samples << ", \"frames\": "
               << arguments.frames << ", \"gpu_timing\": "
               << (statistics.gpu_timing ? "true" : "false")
               << ", \"readback_bytes_per_frame\": " << readback_bytes << ",\n      ";
          cpu.Write(runs, "cpu_render_ms");
          runs << ",\n      ";
          gpu.Write(runs, "gpu_frame_ms");
          runs << ",\n      ";
          ready.Write(runs, "producer_to_ready_ms");
          runs << ",\n      ";
          submit_to_ready.Write(runs, "submit_to_ready_ms");
          runs << '}';
          first = false;
          std::cout << path << ' ' << width << 'x' << height << ' '
                    << statistics.samples << "x: cpu p50 " << cpu.Percentile(50)
                    << " ms, gpu p50 " << gpu.Percentile(50) << " ms, ready p50 "
                    << ready.Percentile(50) << " ms, " << readback_bytes
                    << " bytes\n";
        }
      }
    }

    std::ofstream out(arguments.output);
    out << std::setprecision(6) << "{\n  \"tool\": \"toon-handoff-measure\",\n"
        << "  \"stage\": \"" << Escape(arguments.stage) << "\",\n"
        << "  \"device\": \"" << Escape(device) << "\",\n"
        << "  \"driver_version\": \"" << Escape(driver) << "\",\n"
        << "  \"validation\": false,\n"
        << "  \"warmup\": " << arguments.warmup << ",\n"
        << "  \"draws\": " << draws.draws.size() << ",\n"
        << "  \"triangles\": " << draws.triangle_count << ",\n"
        << "  \"runs\": [\n" << runs.str() << "\n  ]\n}\n";
    Require(out.good(), "cannot write " + arguments.output);
    return 0;
  } catch (const toon_lending::Skip& skip) {
    std::cout << "SKIP: " << skip.what() << '\n';
    return kSkip;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
