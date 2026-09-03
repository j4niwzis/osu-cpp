module;

export module platform.clock;

import std;

export namespace platform::clock {

// A process-local monotonic clock. Only differences are meaningful, which is
// exactly what input timestamps, animation and playback synchronization use.
[[nodiscard]] inline double milliseconds() noexcept {
  using Clock = std::chrono::steady_clock;
  static const auto origin = Clock::now();
  return std::chrono::duration<double, std::milli>(Clock::now() - origin)
      .count();
}

} // namespace platform::clock
