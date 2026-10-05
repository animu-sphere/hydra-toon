// SPDX-License-Identifier: Apache-2.0
#include "telemetry.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
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
  if (capacity_ == 0 || !std::isfinite(value) || value < 0) return;
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
  for (double value : sorted_) {
    const double delta = value - summary.mean;
    summary.variance += delta * delta;
  }
  summary.variance /= static_cast<double>(sorted_.size());
  summary.stddev = std::sqrt(summary.variance);
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
  latency_frames_.reserve(kWindow);
}

void FrameTelemetry::PushLatency(const FrameLatency& sample) {
  const auto age = [&](std::size_t index, Series& series, std::int64_t input, std::int64_t endpoint) {
    if (input > 0 && endpoint >= input && input != measured_inputs_[index]) {
      series.Push(static_cast<double>(endpoint - input) / 1e6);
      measured_inputs_[index] = input;
    }
  };
  age(0, pose_buffer_, sample.inputs.pose, sample.buffers_written);
  age(1, pose_submit_, sample.inputs.pose, sample.submitted);
  age(2, pose_present_, sample.inputs.pose, sample.present_returned);
  age(3, expression_submit_, sample.inputs.expression, sample.submitted);
  age(4, look_at_submit_, sample.inputs.look_at, sample.submitted);
  age(5, camera_present_, sample.inputs.camera, sample.present_returned);
  pose_write_.Push(sample.pose_write_ms);
  morph_write_.Push(sample.morph_write_ms);
  material_write_.Push(sample.material_write_ms);
  if (latency_frames_.size() < kWindow) latency_frames_.push_back(sample);
  else {
    latency_frames_[latency_next_] = sample;
    latency_next_ = (latency_next_ + 1U) % kWindow;
  }
}

std::vector<FrameTelemetry::Named> FrameTelemetry::Latency() const {
  return {{"pose_buffer", &pose_buffer_}, {"pose_submit", &pose_submit_},
      {"pose_present_api", &pose_present_}, {"expression_submit", &expression_submit_},
      {"look_at_submit", &look_at_submit_}, {"camera_present_api", &camera_present_},
      {"pose_write", &pose_write_}, {"morph_write", &morph_write_}, {"material_write", &material_write_}};
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
           << " p99=" << summary.p99 << " max=" << summary.max
           << " stddev=" << summary.stddev << " ms\n";
  };
  for (const Named& named : Cpu()) {
    line("cpu", named);
  }
  for (const Named& named : Gpu()) {
    line("gpu", named);
  }
  for (const Named& named : Latency()) line("latency", named);
  return report.str();
}

std::string FrameTelemetry::Json() const {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(12) << "{\"schema\":1,\"clock\":\"steady_nanoseconds\","
      << "\"present_endpoint\":\"vkQueuePresentKHR_return\",\"display_time_measured\":false,\"summaries_ms\":{";
  bool first = true;
  const auto summary = [&](const char* group, const Named& row) {
    if (!first) out << ',';
    first = false;
    const auto s = row.series->Summarize();
    out << '"' << group << '_' << row.name << "\":{\"count\":" << s.count
        << ",\"mean\":" << s.mean << ",\"p95\":" << s.p95 << ",\"p99\":" << s.p99
        << ",\"max\":" << s.max << ",\"variance_ms2\":" << s.variance << ",\"stddev\":" << s.stddev << '}';
  };
  for (const auto& row : Cpu()) summary("cpu", row);
  for (const auto& row : Gpu()) summary("gpu", row);
  for (const auto& row : Latency()) summary("latency", row);
  out << "},\"frames\":[";
  for (std::size_t i = 0; i < latency_frames_.size(); ++i) {
    if (i != 0) out << ',';
    const auto& f = latency_frames_[(latency_next_ + i) % latency_frames_.size()];
    out << "{\"frame\":" << f.frame << ",\"pose_input_ns\":" << f.inputs.pose
        << ",\"expression_input_ns\":" << f.inputs.expression << ",\"look_at_input_ns\":" << f.inputs.look_at
        << ",\"camera_input_ns\":" << f.inputs.camera << ",\"latched_ns\":" << f.latched
        << ",\"buffers_written_ns\":" << f.buffers_written << ",\"submitted_ns\":" << f.submitted
        << ",\"present_returned_ns\":" << f.present_returned
        << ",\"pose_write_ms\":" << f.pose_write_ms << ",\"morph_write_ms\":" << f.morph_write_ms
        << ",\"material_write_ms\":" << f.material_write_ms
        << ",\"late_sample_applied\":" << (f.late_sample_applied ? "true" : "false") << '}';
  }
  out << "]}\n";
  return out.str();
}

} // namespace Toon::viewport
