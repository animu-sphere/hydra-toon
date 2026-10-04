// SPDX-License-Identifier: Apache-2.0
#include "playback.hpp"

#include <algorithm>
#include <cmath>

namespace Toon::viewport {

void Playback::Reset(double start, double end, double codes_per_second,
    double time) {
  *this = {};
  time_ = std::isfinite(time) ? time : 0.0;
  available_ = std::isfinite(start) && std::isfinite(end) && end > start &&
               std::isfinite(end - start) &&
               std::isfinite(codes_per_second) && codes_per_second > 0.0;
  if (available_) {
    start_ = start;
    end_ = end;
    codes_per_second_ = codes_per_second;
  }
}

void Playback::SetPlaying(bool playing) {
  playing_ = available_ && playing;
  if (playing_ && (time_ < start_ || time_ >= end_)) {
    time_ = start_;
  }
}

void Playback::SetSpeed(double speed) {
  if (std::isfinite(speed) && speed >= 0.05 && speed <= 4.0) {
    speed_ = speed;
  }
}

void Playback::Seek(double time) {
  if (available_ && std::isfinite(time)) {
    playing_ = false;
    time_ = std::clamp(time, start_, end_);
  }
}

void Playback::Step(double codes) {
  if (std::isfinite(codes)) {
    Seek(time_ + codes);
  }
}

void Playback::Advance(double seconds) {
  if (!playing_ || !std::isfinite(seconds) || seconds < 0.0) {
    return;
  }
  const double next = time_ + seconds * codes_per_second_ * speed_;
  if (!std::isfinite(next) || !std::isfinite(next - start_)) {
    playing_ = false;
    return;
  }
  if (next > end_ && loop_) {
    time_ = start_ + std::fmod(next - start_, end_ - start_);
  } else {
    time_ = std::min(next, end_);
    if (time_ == end_ && !loop_) {
      playing_ = false;
    }
  }
}

} // namespace Toon::viewport
