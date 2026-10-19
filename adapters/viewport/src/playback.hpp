// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace Toon::viewport {

// USD time codes at the host boundary; elapsed time is in seconds. No USD
// types or motion/expression evaluation belong in this transport.
class Playback {
public:
  void Reset(double start, double end, double codes_per_second, double time);
  void SetPlaying(bool playing);
  void SetSpeed(double speed);
  void SetLoop(bool loop) { loop_ = loop; }
  // A manual seek/step pauses playback and clamps to the authored range.
  void Seek(double time);
  void Step(double codes);
  void Advance(double seconds);

  [[nodiscard]] bool available() const { return available_; }
  [[nodiscard]] bool playing() const { return playing_; }
  [[nodiscard]] bool loop() const { return loop_; }
  [[nodiscard]] double start() const { return start_; }
  [[nodiscard]] double end() const { return end_; }
  [[nodiscard]] double time() const { return time_; }
  [[nodiscard]] double speed() const { return speed_; }
  [[nodiscard]] double codes_per_second() const { return codes_per_second_; }

private:
  bool available_ = false;
  bool playing_ = false;
  bool loop_ = true;
  double start_ = 0.0;
  double end_ = 0.0;
  double time_ = 0.0;
  double speed_ = 1.0;
  double codes_per_second_ = 24.0;
};

} // namespace Toon::viewport
