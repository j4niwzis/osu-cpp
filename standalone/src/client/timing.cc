export module client.timing;

import std;

export namespace client {

// Anchored game clock.
//
// Maps a monotonic wall clock (monotonic-clock-based, milliseconds) onto the
// audio/game timeline. The audio device is queried only sparsely (see
// App::maybeSyncClock); between syncs the game time is extrapolated from the
// wall clock, which removes per-frame blocking OpenAL queries from the frame
// loop entirely.
//
// Corrections are slewed: a fraction of the observed error is applied per
// sync instead of snapping, so the timeline never jumps visibly. Output is
// clamped to be monotonically non-decreasing, which the engine requires for
// submitted input events.
class AnchoredClock {
public:
  // Establish the anchor: wallMs corresponds to gameMs exactly.
  void reset(double wallMs, double gameMs) noexcept {
    fAnchorWall = wallMs;
    fAnchorGame = gameMs;
    fLast = gameMs;
  }

  // Current game time for the given wall time (monotonic).
  [[nodiscard]] double sample(double wallMs) noexcept {
    double t = this->extrapolate(wallMs);
    if (t < fLast) {
      t = fLast;
    } else {
      fLast = t;
    }
    return t;
  }

  // Sparse re-synchronisation against the real audio position.
  void sync(double wallMs, double audioMs) noexcept {
    const double predicted = this->extrapolate(wallMs);
    const double error = audioMs - predicted;
    if (std::abs(error) > kHardResyncThresholdMs) {
      // Seek or gross desync: snap the anchor. sample() keeps the output
      // monotonic, so a backwards snap stalls briefly instead of rewinding.
      fAnchorWall = wallMs;
      fAnchorGame = audioMs;
      return;
    }
    // Slew: absorb a fraction of the error at each sync.
    fAnchorWall = wallMs;
    fAnchorGame = predicted + error * kSlewGain;
  }

private:
  [[nodiscard]] double extrapolate(double wallMs) const noexcept {
    return fAnchorGame + (wallMs - fAnchorWall);
  }

  static constexpr double kHardResyncThresholdMs = 150.0;
  static constexpr double kSlewGain = 0.1;

  double fAnchorWall = 0.0;
  double fAnchorGame = 0.0;
  double fLast = std::numeric_limits<double>::lowest();
};

} // namespace client
