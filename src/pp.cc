export module osu.pp;

import std;
import osu.types;
import osu.rules;
import osu.stars;

// Performance points for the calculator the servers run.
//
// This is a pure function of the difficulty attributes and what the player
// did, which is what made it possible to check: the same arithmetic was
// written in Python first, handed rosu-pp's own attributes, and compared
// against rosu-pp's own pp over 410 scores on two maps -- every random
// combination of accuracy, combo and misses, plus the corners. Agreement was
// 5.6e-16 on the total, which is double precision and nothing else.
//
// One thing that check caught before any of this was written: the accuracy a
// score is multiplied by is the lazer one, which counts slider tails at 150
// and large ticks at 30 on both sides of the fraction. Using the classic
// formula put the aim out by five percent.
export namespace osu {

// What the player did, in the terms a performance calculation asks for.
struct ScoreInput {
  int fGreat = 0;
  int fOk = 0;
  int fMeh = 0;
  int fMiss = 0;
  int fMaxCombo = 0;
  int fSliderTailHits = 0; // of attrs.fSliders
  int fLargeTickHits = 0;  // of attrs.fLargeTicks
};

struct Performance {
  double fTotal = 0.0;
  double fAim = 0.0;
  double fSpeed = 0.0;
  double fAccuracy = 0.0;
  double fEffectiveMisses = 0.0;
  double fSpeedDeviation = 0.0;
  bool fHasSpeedDeviation = false;
};

namespace detail {

inline constexpr double kPerformanceBaseMultiplier = 1.14;

[[nodiscard]] inline double reverseLerp(double x, double a, double b) noexcept {
  return std::clamp((x - a) / (b - a), 0.0, 1.0);
}

// Aim and Speed both use this curve; it is not the one the star rating uses.
[[nodiscard]] inline double difficultyToPerformance(double d) noexcept {
  return std::pow(5.0 * std::max(1.0, d / 0.0675) - 4.0, 3.0) / 100000.0;
}

// A player misses the hard parts, so the penalty is softened on a map that
// has many of them and sharpened on one that has few.
[[nodiscard]] inline double missPenalty(double misses,
                                        double difficultStrains) noexcept {
  return 0.96 /
         ((misses / (4.0 * std::pow(std::log(difficultStrains), 0.94))) + 1.0);
}

// The lazer accuracy: slider tails and large ticks are scored, so they are in
// the fraction on both sides. The classic one counts only the four basic
// results and is a different number.
[[nodiscard]] inline double accuracyOf(const StarRating &a,
                                       const ScoreInput &s) noexcept {
  const int basic = s.fGreat + s.fOk + s.fMeh + s.fMiss;
  const double base = 300.0 * s.fGreat + 100.0 * s.fOk + 50.0 * s.fMeh +
                      150.0 * s.fSliderTailHits + 30.0 * s.fLargeTickHits;
  const double max = 300.0 * basic + 150.0 * a.fSliders + 30.0 * a.fLargeTicks;
  return max > 0.0 ? base / max : 1.0;
}

// How far off the beat the taps were, inferred from the judgements rather
// than measured: the great count gives a proportion, the proportion gives a
// standard deviation, and the tails outside the ok window are taken back off.
[[nodiscard]] inline std::optional<double>
deviation(const StarRating &a, double great, double ok, double meh) {
  if (great + ok + meh <= 0.0) {
    return std::nullopt;
  }
  const double n = std::max(1.0, great + ok);
  const double p = great / n;
  constexpr double kZ = 2.32634787404; // one-tailed 99%
  const double lower =
      std::min(p, (n * p + kZ * kZ / 2.0) / (n + kZ * kZ) -
                      kZ / (n + kZ * kZ) *
                          std::sqrt(n * p * (1.0 - p) + kZ * kZ / 4.0));

  double dev = 0.0;
  if (lower > 0.01) {
    // erfInv by Newton on std::erf, which is exact to the last bit here.
    const auto erfInv = [](double y) {
      double x = 0.0;
      if (y > -1.0 && y < 1.0) {
        // A starting point good to a few digits, then Newton closes it.
        const double w = -std::log((1.0 - y) * (1.0 + y));
        if (w < 5.0) {
          const double t = w - 2.5;
          x = y * (2.81022636e-08 * t + 3.43273939e-07);
          x = y * ((((((x * t - 3.5233877e-06) * t) + -4.39150654e-06) * t +
                     0.00021858087) *
                        t +
                    -0.00125372503) *
                       t +
                   -0.00417768164);
          x = y * ((x * t + 0.246640727) * t + 1.50140941);
        } else {
          const double t = std::sqrt(w) - 3.0;
          x = y * (-0.000200214257 * t + 0.000100950558);
          x = y * ((((((x * t + 0.00134934322) * t + -0.00367342844) * t +
                      0.00573950773) *
                         t +
                     -0.0076224613) *
                        t +
                    0.00943887047) *
                       t +
                   1.00167406);
          x = y * ((x * t + 2.83297682) * t);
        }
        for (int i = 0; i < 4; ++i) {
          const double err = std::erf(x) - y;
          x -= err / (2.0 / std::sqrt(std::numbers::pi) * std::exp(-x * x));
        }
      }
      return x;
    };
    dev = a.fGreatWindow / (std::sqrt(2.0) * erfInv(lower));
    const double tail =
        std::sqrt(2.0 / std::numbers::pi) * a.fOkWindow *
        std::exp(-0.5 * std::pow(a.fOkWindow / dev, 2.0)) /
        (dev * std::erf(a.fOkWindow / (std::sqrt(2.0) * dev)));
    dev *= std::sqrt(1.0 - tail);
  } else {
    dev = a.fOkWindow / std::sqrt(3.0); // a score of nothing but oks
  }

  // Mehs are assumed to be spread evenly across their window.
  const double mehVar = (a.fMehWindow * a.fMehWindow +
                         a.fOkWindow * a.fMehWindow + a.fOkWindow * a.fOkWindow) /
                        3.0;
  return std::sqrt(((great + ok) * dev * dev + meh * mehVar) /
                   (great + ok + meh));
}

// Speed above what the tapping accuracy can account for is taken to be
// improperly tapped, and scaled back towards the honest value.
[[nodiscard]] inline double highDeviationNerf(const StarRating &a,
                                              double dev) noexcept {
  const double value = difficultyToPerformance(a.fSpeed);
  const double cutoff = 100.0 + 220.0 * std::pow(22.0 / dev, 6.5);
  if (value <= cutoff) {
    return 1.0;
  }
  constexpr double kScale = 50.0;
  double adjusted =
      kScale * (std::log((value - cutoff) / kScale + 1.0) + cutoff / kScale);
  const double towards = 1.0 - reverseLerp(dev, 22.0, 27.0);
  adjusted += (value - adjusted) * towards;
  return adjusted / value;
}

} // namespace detail

// Mods are not taken here yet: every branch they would touch -- Relax,
// Autopilot, Flashlight, Blinds, Touch Device, NoFail, SpinOut, Classic --
// is one this client has no mod for, and writing them untested would be
// writing them wrong.
[[nodiscard]] inline Performance performanceRanked(const StarRating &a,
                                                   const ScoreInput &s) {
  using namespace detail;
  Performance out;
  const int totalHits = s.fGreat + s.fOk + s.fMeh + s.fMiss;
  if (totalHits == 0) {
    return out;
  }
  const double acc = accuracyOf(a, s);
  const double total = static_cast<double>(totalHits);
  const double imperfect = static_cast<double>(s.fOk + s.fMeh + s.fMiss);
  const int tailsDropped = a.fSliders - s.fSliderTailHits;
  const int ticksMissed = a.fLargeTicks - s.fLargeTickHits;

  // Combo says more about breaks than the miss count does: a drop of combo
  // with no miss recorded is a slider that was let go of.
  double misses = static_cast<double>(s.fMiss);
  if (a.fSliders > 0) {
    const double threshold = static_cast<double>(a.fMaxCombo - tailsDropped);
    if (static_cast<double>(s.fMaxCombo) < threshold) {
      misses = threshold / std::max(1.0, static_cast<double>(s.fMaxCombo));
    }
    misses = std::min(misses, static_cast<double>(ticksMissed + s.fMiss));
  }
  misses = std::clamp(misses, static_cast<double>(s.fMiss), total);
  out.fEffectiveMisses = misses;

  const double lengthBonus =
      0.95 + 0.4 * std::min(total / 2000.0, 1.0) +
      (total > 2000.0 ? std::log10(total / 2000.0) * 0.5 : 0.0);

  // ---- aim -------------------------------------------------------------
  double aimDifficulty = a.fAim;
  if (a.fSliders > 0 && a.fAimDifficultSliders > 0.0) {
    const double dropped = std::clamp(
        static_cast<double>(tailsDropped + ticksMissed), 0.0,
        a.fAimDifficultSliders);
    const double nerf =
        (1.0 - a.fSliderFactor) *
            std::pow(1.0 - dropped / a.fAimDifficultSliders, 3.0) +
        a.fSliderFactor;
    aimDifficulty *= nerf;
  }
  double aim = difficultyToPerformance(aimDifficulty) * lengthBonus;
  if (misses > 0.0) {
    aim *= missPenalty(std::min(misses, imperfect + ticksMissed),
                       a.fAimDifficultStrains);
  }
  aim *= acc;
  out.fAim = aim;

  // ---- speed -----------------------------------------------------------
  double speedNotes = a.fSpeedNoteCount;
  speedNotes += (total - a.fSpeedNoteCount) * 0.1;
  // Assume every mistake landed on a note that mattered for speed.
  const double relMiss = std::min(static_cast<double>(s.fMiss), speedNotes);
  const double relMeh = std::min(static_cast<double>(s.fMeh), speedNotes - relMiss);
  const double relOk =
      std::min(static_cast<double>(s.fOk), speedNotes - relMiss - relMeh);
  const double relGreat = std::max(0.0, speedNotes - relMiss - relMeh - relOk);

  double speed = 0.0;
  if (s.fGreat + s.fOk + s.fMeh > 0) {
    if (const auto dev = deviation(a, relGreat, relOk, relMeh)) {
      out.fSpeedDeviation = *dev;
      out.fHasSpeedDeviation = true;
      speed = difficultyToPerformance(a.fSpeed) * lengthBonus;
      if (misses > 0.0) {
        speed *= missPenalty(std::min(misses, imperfect + ticksMissed),
                             a.fSpeedDifficultStrains);
      }
      speed *= highDeviationNerf(a, *dev);

      // The accuracy of the notes that carry the speed, assuming the worst.
      const double spare = std::max(0.0, total - a.fSpeedNoteCount);
      const double r300 = std::max(0.0, s.fGreat - spare);
      const double r100 = std::max(0.0, s.fOk - std::max(0.0, spare - s.fGreat));
      const double r50 = std::max(
          0.0, s.fMeh - std::max(0.0, spare - s.fGreat - s.fOk));
      const double relAcc =
          a.fSpeedNoteCount == 0.0
              ? 0.0
              : (r300 * 6.0 + r100 * 2.0 + r50) / (a.fSpeedNoteCount * 6.0);
      const double od = (79.5 - a.fGreatWindow) / 6.0;
      speed *= std::pow((acc + relAcc) / 2.0, (14.5 - od) / 2.0);
    }
  }
  out.fSpeed = speed;

  // ---- accuracy --------------------------------------------------------
  const int withAcc = a.fCircles + a.fSliders;
  double better = 0.0;
  if (withAcc > 0) {
    better = static_cast<double>((s.fGreat - std::max(totalHits - withAcc, 0)) * 6 +
                                 s.fOk * 2 + s.fMeh) /
             static_cast<double>(withAcc * 6);
  }
  better = std::max(0.0, better);
  const double od = (79.5 - a.fGreatWindow) / 6.0;
  double accuracy = std::pow(1.52163, od) * std::pow(better, 24.0) * 2.83;
  accuracy *= std::min(std::pow(static_cast<double>(withAcc) / 1000.0, 0.3), 1.15);
  out.fAccuracy = accuracy;

  out.fTotal = std::pow(std::pow(aim, 1.1) + std::pow(speed, 1.1) +
                            std::pow(accuracy, 1.1),
                        1.0 / 1.1) *
               kPerformanceBaseMultiplier;
  return out;
}

} // namespace osu
