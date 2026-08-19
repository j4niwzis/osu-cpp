export module client.spectrum;

import std;

export namespace client {

// Circular-visualiser spectrum, modelled on osu!lazer's LogoVisualisation.
//
// lazer gets FFT amplitudes for free from BASS; OpenAL has no such API, so we
// keep the decoded PCM around and run our own windowed FFT at the current
// playback position. The bar behaviour is lazer's: take the new amplitude if
// it exceeds the current bar (peak hold), otherwise decay continuously.
class Spectrum {
public:
  static constexpr std::size_t kBars = 200;
  static constexpr std::size_t kFftSize = 1024; // power of two
  static constexpr double kUpdateIntervalMs = 50.0;
  static constexpr float kDecayPerMs = 0.0024f;

  // mono: full decoded track, single channel. posSec: playback position.
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
    if (start < 0 ||
        start + static_cast<std::ptrdiff_t>(kFftSize) >=
            static_cast<std::ptrdiff_t>(mono.size())) {
      return; // outside the track: let the bars decay away
    }

    this->ensureTables();

    // Hann-windowed real input into the complex buffer.
    for (std::size_t i = 0; i < kFftSize; ++i) {
      const float sample =
          static_cast<float>(mono[static_cast<std::size_t>(start) + i]) /
          32768.0f;
      fRe[i] = sample * fWindow[i];
      fIm[i] = 0.0f;
    }
    this->fft();

    // Magnitudes of the lower half, mapped onto the bars. Only the first
    // quarter of the spectrum carries musical energy worth showing, so the
    // bars cover 0..rate/8 with a mild logarithmic emphasis on the low end.
    constexpr std::size_t kUsableBins = kFftSize / 8;
    for (std::size_t b = 0; b < kBars; ++b) {
      const float t = static_cast<float>(b) / static_cast<float>(kBars);
      const auto lo = static_cast<std::size_t>(
          std::pow(static_cast<float>(kUsableBins), t));
      const auto hi = std::max(
          lo + 1, static_cast<std::size_t>(std::pow(
                      static_cast<float>(kUsableBins),
                      static_cast<float>(b + 1) / static_cast<float>(kBars))));
      float peak = 0.0f;
      for (std::size_t k = lo; k < std::min(hi, kUsableBins); ++k) {
        const float mag =
            std::sqrt(fRe[k] * fRe[k] + fIm[k] * fIm[k]) /
            static_cast<float>(kFftSize / 4);
        peak = std::max(peak, mag);
      }
      // Gentle compression: raw magnitudes are far too spiky to look good.
      const float target = std::clamp(std::sqrt(peak) * 0.9f, 0.0f, 1.0f);
      if (target > fBars[b]) {
        fBars[b] = target;
      }
    }
  }

  [[nodiscard]] std::span<const float> bars() const noexcept { return fBars; }

  // Low-frequency energy, useful for pulsing the logo in place of lazer's
  // beat-synced containers (we have no timing points loaded in the menu).
  [[nodiscard]] float bass() const noexcept {
    float sum = 0.0f;
    for (std::size_t i = 0; i < 12 && i < kBars; ++i) {
      sum += fBars[i];
    }
    return sum / 12.0f;
  }

  void reset() noexcept {
    fBars.fill(0.0f);
    fLastAnalysisMs = 0.0;
  }

private:
  void decay(double nowMs) noexcept {
    const double dt = fLastDecayMs > 0.0 ? std::min(50.0, nowMs - fLastDecayMs)
                                         : 0.0;
    fLastDecayMs = nowMs;
    const float factor = static_cast<float>(dt) * kDecayPerMs;
    for (float &bar : fBars) {
      bar -= factor * (bar + 0.03f); // lazer's 3% floor speeds up the tail
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

  // Iterative in-place radix-2 Cooley-Tukey.
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
      const float ang = -2.0f * std::numbers::pi_v<float> /
                        static_cast<float>(len);
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

  std::array<float, kBars> fBars{};
  std::vector<float> fWindow;
  std::vector<float> fRe;
  std::vector<float> fIm;
  double fLastAnalysisMs = 0.0;
  double fLastDecayMs = 0.0;
};

} // namespace client
