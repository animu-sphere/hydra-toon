// SPDX-License-Identifier: Apache-2.0
#include "telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Toon::viewport {
namespace {

// The nearest-rank percentile of sorted values.
double Percentile(const std::vector<double>& sorted, double fraction) {
  const auto rank = static_cast<std::size_t>(
      std::ceil(fraction * static_cast<double>(sorted.size())));
  return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1U];
}

} // namespace

Series::Series(std::size_t capacity) : capacity_(capacity) {
  values_.reserve(capacity);
  sorted_.reserve(capacity);
}

void Series::Push(double value) {
  if (values_.size() < capacity_) {
    values_.push_back(value);
    return;
  }
  values_[next_] = value;
  next_ = (next_ + 1U) % capacity_;
}

Summary Series::Summarize() const {
  Summary summary;
  summary.count = values_.size();
  if (values_.empty()) {
    return summary;
  }
  sorted_.assign(values_.begin(), values_.end());
  std::sort(sorted_.begin(), sorted_.end());
  double total = 0.0;
  for (const double value : sorted_) {
    total += value;
  }
  summary.mean = total / static_cast<double>(sorted_.size());
  summary.p50 = Percentile(sorted_, 0.50);
  summary.p95 = Percentile(sorted_, 0.95);
  summary.p99 = Percentile(sorted_, 0.99);
  summary.max = sorted_.back();
  return summary;
}

void Series::CopyInOrder(std::vector<float>& values) const {
  values.clear();
  for (std::size_t index = 0; index < values_.size(); ++index) {
    values.push_back(
        static_cast<float>(values_[(next_ + index) % values_.size()]));
  }
}

FrameTelemetry::FrameTelemetry()
    : interval_(kWindow), hydra_(kWindow), extract_(kWindow),
      overlay_(kWindow), wait_(kWindow), submit_(kWindow),
      gpu_uploads_(kWindow), gpu_image_wait_(kWindow), gpu_unlit_(kWindow),
      gpu_outline_(kWindow), gpu_opaque_(kWindow),
      gpu_transparent_(kWindow), gpu_resolve_(kWindow),
      gpu_capture_(kWindow), gpu_overlay_(kWindow), gpu_work_(kWindow),
      gpu_frame_(kWindow) {
}

void FrameTelemetry::PushCpu(const CpuSample& sample) {
  interval_.Push(sample.interval);
  hydra_.Push(sample.hydra);
  extract_.Push(sample.extract);
  overlay_.Push(sample.overlay);
  wait_.Push(sample.wait);
  submit_.Push(sample.submit);
}

void FrameTelemetry::PushGpu(const PresentGpuTimes& times) {
  gpu_uploads_.Push(times.uploads);
  gpu_image_wait_.Push(times.image_wait);
  gpu_unlit_.Push(times.unlit);
  gpu_outline_.Push(times.outline);
  gpu_opaque_.Push(times.opaque);
  gpu_transparent_.Push(times.transparent);
  gpu_resolve_.Push(times.resolve);
  gpu_capture_.Push(times.capture);
  gpu_overlay_.Push(times.overlay);
  gpu_work_.Push(times.frame - times.image_wait);
  gpu_frame_.Push(times.frame);
}

std::vector<FrameTelemetry::Named> FrameTelemetry::Cpu() const {
  return {{"interval", &interval_}, {"hydra", &hydra_},
      {"extract", &extract_}, {"overlay", &overlay_}, {"wait", &wait_},
      {"submit", &submit_}};
}

std::vector<FrameTelemetry::Named> FrameTelemetry::Gpu() const {
  return {{"uploads", &gpu_uploads_}, {"image_wait", &gpu_image_wait_},
      {"unlit", &gpu_unlit_}, {"outline", &gpu_outline_},
      {"opaque", &gpu_opaque_}, {"transparent", &gpu_transparent_},
      {"resolve", &gpu_resolve_}, {"capture", &gpu_capture_},
      {"overlay", &gpu_overlay_}, {"work", &gpu_work_},
      {"frame", &gpu_frame_}};
}

std::string FrameTelemetry::Report() const {
  std::ostringstream report;
  report << std::fixed << std::setprecision(3);
  const auto line = [&](const char* group, const Named& named) {
    const Summary summary = named.series->Summarize();
    if (summary.count == 0) {
      return;
    }
    report << "Timing: " << group << ' ' << named.name
           << " frames=" << summary.count << " mean=" << summary.mean
           << " p50=" << summary.p50 << " p95=" << summary.p95
           << " p99=" << summary.p99 << " max=" << summary.max << " ms\n";
  };
  for (const Named& named : Cpu()) {
    line("cpu", named);
  }
  for (const Named& named : Gpu()) {
    line("gpu", named);
  }
  return report.str();
}

} // namespace Toon::viewport
