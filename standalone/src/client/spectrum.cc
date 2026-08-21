export module client.spectrum;

import std;

export namespace client {

// Port of osu!lazer's LogoVisualisation amplitude handling.
//
// lazer receives 256 FFT bins from BASS (ChannelAmplitudes.FrequencyAmplitudes)
// and, every 50 ms, folds them onto 200 bars with a rotating index offset,
// keeping the maximum (peak hold) and decaying continuously in between.
// OpenAL has no spectrum API, so the bins are computed here from the decoded
// PCM; everything downstream matches the original constant for constant.
class Spectrum {
public:
  static constexpr std::size_t kBars = 200;      // bars_per_visualiser
  static constexpr std::size_t kBins = 256;      // BASS ChannelAmplitudes size
  static constexpr std::size_t kFftSize = 512;   // 512-point FFT -> 256 bins
  static constexpr std::size_t kIndexChange = 5; // index_change
  static constexpr double kUpdateIntervalMs = 50.0;   // time_between_updates
  static constexpr float kDecayPerMs = 0.0024f;       // decay_per_millisecond
  static constexpr float kNonKiaiMultiplier = 0.5f;   // kiai off

  void update(std::span<const std::int16_t> mono, int rate, double posSec,
              double nowMs) {
    this->decay(nowMs);
    if (nowMs - fLastAnalysisMs < kUpdateIntervalMs) {
      return;
    }
    fLastAnalysisMs = nowMs;
    if (mono.empty() || rate <= 0) {
      return;
    }

    const auto center = static_cast<std::ptrdiff_t>(posSec * rate);
    const auto start = center - static_cast<std::ptrdiff_t>(kFftSize / 2);
    if (start < 0 || start + static_cast<std::ptrdiff_t>(kFftSize) >=
                         static_cast<std::ptrdiff_t>(mono.size())) {
      return; // outside the track: let the bars decay away
    }

    this->ensureTables();
    for (std::size_t i = 0; i < kFftSize; ++i) {
      const float sample =
          static_cast<float>(mono[static_cast<std::size_t>(start) + i]) /
          32768.0f;
      fRe[i] = sample * fWindow[i];
      fIm[i] = 0.0f;
    }
    this->fft();

    // Magnitudes, normalised the way BASS reports them: roughly 0..1 per bin.
    for (std::size_t k = 0; k < kBins; ++k) {
      const float mag = std::sqrt(fRe[k] * fRe[k] + fIm[k] * fIm[k]) * 2.0f /
                        static_cast<float>(kFftSize);
      // BASS amplitudes are already perceptually weighted; sqrt approximates
      // that shaping closely enough to keep the visual behaviour.
      fBins[k] = std::clamp(std::sqrt(mag) * 1.6f, 0.0f, 1.0f);
    }

    // lazer: targetAmplitude = temporalAmplitudes[(i + indexOffset) % 200]
    for (std::size_t i = 0; i < kBars; ++i) {
      const float target =
          fBins[(i + fIndexOffset) % kBars] * kNonKiaiMultiplier;
      if (target > fBars_[i]) {
        fBars_[i] = target;
      }
    }
    fIndexOffset = (fIndexOffset + kIndexChange) % kBars;
  }

  [[nodiscard]] std::span<const float> bars() const noexcept { return fBars_; }

  // Stands in for lazer's beat-synced logo pulse (no timing points in menu).
  [[nodiscard]] float bass() const noexcept {
    float sum = 0.0f;
    for (std::size_t i = 0; i < 8; ++i) {
      sum += fBins[i];
    }
    return sum / 8.0f;
  }

  void reset() noexcept {
    fBars_.fill(0.0f);
    fBins.fill(0.0f);
    fLastAnalysisMs = 0.0;
  }

private:
  void decay(double nowMs) noexcept {
    // Linear, not exponential: the bars fall by a fixed amount per
    // millisecond, so a late frame would drop them a long way at once. Capped
    // at a whole bar's worth of fall, which is as far as one can go and still
    // mean anything -- in the decay's own units rather than in a guess at how
    // late a frame may be.
    const double dt = fLastDecayMs > 0.0 ? nowMs - fLastDecayMs : 0.0;
    fLastDecayMs = nowMs;
    const float factor = std::min(1.0f, static_cast<float>(dt) * kDecayPerMs);
    for (float &bar : fBars_) {
      // lazer adds 3% of a full bar so the tail finishes faster.
      bar -= factor * (bar + 0.03f);
      if (bar < 0.0f) {
        bar = 0.0f;
      }
    }
  }

  void ensureTables() {
    if (!fWindow.empty()) {
      return;
    }
    fWindow.resize(kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
      fWindow[i] = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> *
                                          static_cast<float>(i) /
                                          static_cast<float>(kFftSize - 1));
    }
    fRe.resize(kFftSize);
    fIm.resize(kFftSize);
  }

  void fft() {
    constexpr std::size_t n = kFftSize;
    for (std::size_t i = 1, j = 0; i < n; ++i) {
      std::size_t bit = n >> 1;
      for (; (j & bit) != 0; bit >>= 1) {
        j ^= bit;
      }
      j ^= bit;
      if (i < j) {
        std::swap(fRe[i], fRe[j]);
        std::swap(fIm[i], fIm[j]);
      }
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
      const float ang =
          -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
      const float wRe = std::cos(ang);
      const float wIm = std::sin(ang);
      for (std::size_t i = 0; i < n; i += len) {
        float curRe = 1.0f;
        float curIm = 0.0f;
        for (std::size_t k = 0; k < len / 2; ++k) {
          const float uRe = fRe[i + k];
          const float uIm = fIm[i + k];
          const float vRe =
              fRe[i + k + len / 2] * curRe - fIm[i + k + len / 2] * curIm;
          const float vIm =
              fRe[i + k + len / 2] * curIm + fIm[i + k + len / 2] * curRe;
          fRe[i + k] = uRe + vRe;
          fIm[i + k] = uIm + vIm;
          fRe[i + k + len / 2] = uRe - vRe;
          fIm[i + k + len / 2] = uIm - vIm;
          const float nextRe = curRe * wRe - curIm * wIm;
          curIm = curRe * wIm + curIm * wRe;
          curRe = nextRe;
        }
      }
    }
  }

  std::array<float, kBars> fBars_{};
  std::array<float, kBins> fBins{};
  std::size_t fIndexOffset = 0;
  std::vector<float> fWindow;
  std::vector<float> fRe;
  std::vector<float> fIm;
  double fLastAnalysisMs = 0.0;
  double fLastDecayMs = 0.0;
};

} // namespace client
