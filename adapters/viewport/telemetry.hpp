// SPDX-License-Identifier: Apache-2.0
// The viewport's frame telemetry (design policy §24): each frame's CPU and
// GPU times kept over a rolling window, and summaries of that window for the
// overlay and the run's last lines.
#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <toon/vulkan_present.hpp>

namespace Toon::viewport {

struct Summary {
  std::size_t count = 0;
  double mean = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  double max = 0.0;
  double variance = 0.0;
  double stddev = 0.0;
};

// The last `capacity` values of one quantity, oldest first once full.
class Series {
public:
  explicit Series(std::size_t capacity);

  void Push(double value);
  [[nodiscard]] Summary Summarize() const;
  [[nodiscard]] std::size_t size() const {
    return values_.size();
  }
  // The values from the oldest to the newest.
  void CopyInOrder(std::vector<float>& values) const;

private:
  std::vector<double> values_;
  std::size_t capacity_ = 0;
  // Where the next value goes once the window is full.
  std::size_t next_ = 0;
  mutable std::vector<double> sorted_;
};

// What the frame loop measured around one RenderFrame, in milliseconds.
struct CpuSample {
  // From the last frame's RenderFrame returning to this one's.
  double interval = 0.0;
  double hydra = 0.0;
  double extract = 0.0;
  double overlay = 0.0;
  // RenderFrame's own split (PresentStatistics).
  double wait = 0.0;
  double submit = 0.0;
};

class FrameTelemetry {
public:
  static constexpr std::size_t kWindow = 1024;

  FrameTelemetry();

  void PushCpu(const CpuSample& sample);
  // A frame's GPU times, pushed once each, as they arrive.
  void PushGpu(const PresentGpuTimes& times);
  void PushLatency(const FrameLatency& sample);

  // One named series per quantity; the GPU's are empty until a frame's
  // times arrive.
  struct Named {
    const char* name;
    const Series* series;
  };
  [[nodiscard]] std::vector<Named> Cpu() const;
  [[nodiscard]] std::vector<Named> Gpu() const;
  [[nodiscard]] std::vector<Named> Latency() const;
  [[nodiscard]] const Series& interval() const {
    return interval_;
  }

  // The run's last lines: every series, summarized over the window.
  [[nodiscard]] std::string Report() const;
  [[nodiscard]] std::string Json() const;

private:
  Series interval_;
  Series hydra_;
  Series extract_;
  Series overlay_;
  Series wait_;
  Series submit_;
  Series gpu_uploads_;
  Series gpu_image_wait_;
  Series gpu_unlit_;
  Series gpu_outline_;
  Series gpu_opaque_;
  Series gpu_transparent_;
  Series gpu_resolve_;
  Series gpu_capture_;
  Series gpu_overlay_;
  // The frame without its image wait: the GPU's own work.
  Series gpu_work_;
  Series gpu_frame_;
  Series pose_buffer_{kWindow}, pose_submit_{kWindow}, pose_present_{kWindow};
  Series expression_submit_{kWindow}, look_at_submit_{kWindow}, camera_present_{kWindow};
  Series pose_write_{kWindow}, morph_write_{kWindow}, material_write_{kWindow};
  std::vector<FrameLatency> latency_frames_;
  std::size_t latency_next_ = 0;
  // Response latency uses the first completed frame for each input update.
  // Repeated frames of a stationary camera are not new response samples.
  std::array<std::int64_t, 6> measured_inputs_{};
};

} // namespace Toon::viewport
