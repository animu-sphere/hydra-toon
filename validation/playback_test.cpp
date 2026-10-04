// SPDX-License-Identifier: Apache-2.0
#include "../adapters/viewport/playback.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void Time(const Toon::viewport::Playback& playback, double expected) {
  Require(std::abs(playback.time() - expected) < 1e-9, "unexpected time code");
}
}

int main() {
  try {
    Toon::viewport::Playback playback;
    playback.Reset(10.0, 58.0, 24.0, 10.0);
    Require(playback.available() && !playback.playing(), "animated stage starts paused");
    playback.Advance(100.0);
    Time(playback, 10.0);
    playback.SetPlaying(true);
    playback.Advance(0.5);
    Time(playback, 22.0);
    playback.SetSpeed(2.0);
    playback.Advance(0.25);
    Time(playback, 34.0);
    playback.SetPlaying(false);
    playback.Advance(10.0);
    Time(playback, 34.0);

    playback.SetPlaying(true);
    playback.Seek(46.0);
    Require(!playback.playing(), "manual seek pauses");
    playback.Step(-1.0);
    Time(playback, 45.0);
    playback.Step(100.0);
    Time(playback, 58.0);
    playback.Step(-100.0);
    Time(playback, 10.0);

    playback.SetSpeed(1.0);
    playback.SetLoop(false);
    playback.SetPlaying(true);
    playback.Advance(2.0);
    Time(playback, 58.0);
    Require(!playback.playing(), "non-looping playback stops at the endpoint");
    playback.SetPlaying(true);
    Time(playback, 10.0);
    playback.Advance(100.0);
    Time(playback, 58.0);
    Require(!playback.playing(), "large elapsed interval stops at the endpoint");

    playback.SetLoop(true);
    playback.SetPlaying(true);
    playback.Advance(2.0);
    Time(playback, 58.0);
    Require(playback.playing(), "looping playback keeps the exact endpoint");
    playback.Advance(0.25);
    Time(playback, 16.0);
    playback.Advance(4.5);
    Time(playback, 28.0);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    playback.Advance(-1.0);
    playback.Advance(nan);
    playback.Advance(infinity);
    playback.Seek(nan);
    playback.Step(infinity);
    Time(playback, 28.0);
    Require(playback.playing(), "invalid input must not pause or change time");
    playback.SetSpeed(nan);
    playback.SetSpeed(0.0);
    playback.SetSpeed(5.0);
    Require(playback.speed() == 1.0, "invalid speed ignored");
    playback.Advance(std::numeric_limits<double>::max());
    Require(!playback.playing() && std::isfinite(playback.time()), "overflow pauses safely");

    // Replacing a stage resets transport state, including its time-code rate.
    playback.Reset(-30.0, 30.0, 60.0, -30.0);
    Require(!playback.playing() && playback.speed() == 1.0 && playback.loop(), "reset state");
    playback.SetPlaying(true);
    playback.Advance(0.5);
    Time(playback, 0.0);
    playback.Reset(1.0, 3.0, 24.0, 100.0);
    Time(playback, 100.0); // Explicit capture times retain their existing semantics.
    playback.SetPlaying(true);
    Time(playback, 1.0);
    playback.Reset(0.0, 0.0, 24.0, 7.0);
    playback.SetPlaying(true);
    playback.Seek(0.0);
    Require(!playback.available() && !playback.playing(), "static stage cannot play");
    Time(playback, 7.0);
    playback.Reset(3.0, 1.0, 24.0, 0.0);
    Require(!playback.available(), "reversed range rejected");
    playback.Reset(1.0, infinity, 24.0, 0.0);
    Require(!playback.available(), "infinite range rejected");
    playback.Reset(1.0, 3.0, 0.0, 0.0);
    Require(!playback.available(), "zero time-code rate rejected");
    playback.Reset(1.0, 3.0, nan, 0.0);
    Require(!playback.available(), "non-finite time-code rate rejected");
    std::cout << "playback state: pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "playback state: " << error.what() << '\n';
    return 1;
  }
}
