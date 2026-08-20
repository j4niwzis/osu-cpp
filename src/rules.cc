export module osu.rules;

import std;
import osu.types;

export namespace osu {

struct ModSet {
  std::uint32_t fValue = 0;

  constexpr ModSet() = default;
  constexpr explicit ModSet(std::uint32_t value) : fValue(value) {}

  constexpr ModSet operator|(ModSet rhs) const noexcept {
    return ModSet(fValue | rhs.fValue);
  }
  constexpr ModSet operator&(ModSet rhs) const noexcept {
    return ModSet(fValue & rhs.fValue);
  }
  constexpr ModSet &operator|=(ModSet rhs) noexcept {
    fValue |= rhs.fValue;
    return *this;
  }
  constexpr ModSet &operator&=(ModSet rhs) noexcept {
    fValue &= rhs.fValue;
    return *this;
  }
  constexpr bool operator==(const ModSet &) const = default;
  constexpr explicit operator std::uint32_t() const noexcept { return fValue; }
  constexpr explicit operator bool() const noexcept { return fValue != 0; }
};

namespace mod {
inline constexpr ModSet kNone{0};
inline constexpr ModSet kNoFail{1 << 0};
inline constexpr ModSet kEasy{1 << 1};
inline constexpr ModSet kHidden{1 << 3};
inline constexpr ModSet kHardRock{1 << 4};
inline constexpr ModSet kDoubleTime{1 << 6};
inline constexpr ModSet kHalfTime{1 << 8};
inline constexpr ModSet kAuto{1 << 11};
inline constexpr ModSet kAutopilot{1 << 13};
} // namespace mod

[[nodiscard]] constexpr bool hasMod(ModSet mods, ModSet flag) noexcept {
  return (mods & flag).fValue != 0;
}

struct EffectiveDifficulty {
  double fHp;
  double fCs;
  double fOd;
  double fAr;
  double fClockRate;
};

namespace detail {

inline double applyHardRock(double val, double min, double max) noexcept {
  return min + max - val;
}

inline double clampArOd(double ar, double rate) noexcept {
  if (ar > 5.0) {
    ar = (ar - 13.0) / 3.0 + 13.0 / 3.0;
  } else {
    ar = (ar - 5.0) / 3.0 - 5.0 / 3.0;
  }
  ar *= 2.0;
  return ar / rate;
}

} // namespace detail

[[nodiscard]] inline EffectiveDifficulty applyMods(const Difficulty &diff,
                                                   ModSet mods) noexcept {
  double hp = diff.fHp;
  double cs = diff.fCs;
  double od = diff.fOd;
  double ar = diff.fAr;
  double rate = 1.0;
  double multiplier = 1.0;

  if (hasMod(mods, mod::kEasy)) {
    hp *= 0.5;
    cs *= 0.5;
    od *= 0.5;
    ar *= 0.5;
    multiplier = 0.5;
  }
  if (hasMod(mods, mod::kHardRock)) {
    hp = std::min(10.0, detail::applyHardRock(hp, 0.0, 10.0));
    cs = std::min(10.0, detail::applyHardRock(cs, 0.0, 10.0));
    od = std::min(10.0, detail::applyHardRock(od, 0.0, 10.0));
    ar = std::min(10.0, detail::applyHardRock(ar, 0.0, 10.0));
    multiplier = 1.06;
  }
  if (hasMod(mods, mod::kDoubleTime)) {
    rate = 1.5;
    multiplier = 1.12;
  } else if (hasMod(mods, mod::kHalfTime)) {
    rate = 0.75;
    multiplier = 0.3;
  }
  if (hasMod(mods, mod::kHidden)) {
    multiplier = 1.06;
  }
  if (hasMod(mods, mod::kNoFail)) {
    multiplier = 0.5;
  }

  od *= rate;
  ar *= rate;

  return {hp, cs, od, ar, rate};
}

[[nodiscard]] inline double modMultiplier(ModSet mods) noexcept {
  if (hasMod(mods, mod::kDoubleTime))
    return 1.12;
  if (hasMod(mods, mod::kHalfTime))
    return 0.3;
  if (hasMod(mods, mod::kHardRock) && hasMod(mods, mod::kHidden))
    return 1.12;
  if (hasMod(mods, mod::kHardRock))
    return 1.06;
  if (hasMod(mods, mod::kHidden))
    return 1.06;
  if (hasMod(mods, mod::kNoFail))
    return 0.5;
  if (hasMod(mods, mod::kEasy))
    return 0.5;
  return 1.0;
}

// Hit windows as a beatmap file gets them. A converted beatmap uses the
// legacy windows, which are the value rounded down to a whole millisecond
// with half of one taken off -- so OD 7 great is 37.5 ms rather than 38, and
// OD 9.2 is 23.5 rather than 24.8. Checked against lazer for OD 3, 7, 9 and
// 9.6.
[[nodiscard]] inline double legacyWindow(double raw) noexcept {
  return std::floor(raw) - 0.5;
}

// A beatmap's difficulty values live in single precision on the other side,
// and that matters wherever a floor follows: AR 9.6 as a float works out to
// 509.99994 and floors to 509, where the same sum in double gives
// 510.0000000000001 and floors to 510. One millisecond of preempt is enough
// to move the reading value by a third of a percent.
[[nodiscard]] inline double asSingle(double value) noexcept {
  return static_cast<double>(static_cast<float>(value));
}

[[nodiscard]] inline double windowGreat(double od) noexcept {
  return legacyWindow(80.0 - 6.0 * asSingle(od));
}
[[nodiscard]] inline double windowGood(double od) noexcept {
  return legacyWindow(140.0 - 8.0 * asSingle(od));
}
[[nodiscard]] inline double windowMeh(double od) noexcept {
  return legacyWindow(200.0 - 10.0 * asSingle(od));
}

[[nodiscard]] inline Judgement judgeDelta(double delta, double od) noexcept {
  const double absDelta = std::abs(delta);
  if (absDelta <= windowGreat(od)) {
    return judgement::Great{};
  }
  if (absDelta <= windowGood(od)) {
    return judgement::Good{};
  }
  if (absDelta <= windowMeh(od)) {
    return judgement::Meh{};
  }
  return judgement::Miss{};
}

// TimePreempt, floored to a whole millisecond. AR 8.3 gives 704 and not 705,
// AR 9.3 gives 554 and not 555 -- and since neither 8.3 nor 9.3 is exact in
// binary, the value that gets floored is a hair under the round number.
[[nodiscard]] inline double preemptTime(double ar) noexcept {
  const double value = asSingle(ar);
  const double raw = value <= 5.0 ? 1200.0 + 600.0 * (5.0 - value) / 5.0
                                  : 1200.0 - 750.0 * (value - 5.0) / 5.0;
  return std::floor(raw);
}

[[nodiscard]] inline double fadeInTime(double ar) noexcept {
  return std::min(400.0, preemptTime(ar));
}

[[nodiscard]] inline double approachFadeInTime(double ar) noexcept {
  return std::min(800.0, preemptTime(ar));
}

// OsuHitObject.Radius: the object radius of 64 times the scale a circle size
// gives, which is where the rounding allowance lives. One source for both
// this and the stack offsets, since lazer computes them from the same Scale.
[[nodiscard]] inline double circleRadius(double cs) noexcept {
  return static_cast<double>(64.0f * static_cast<float>(circleScale(cs)));
}

// IBeatmapDifficultyInfo.DifficultyRange, the same shape the hit windows use.
[[nodiscard]] inline double difficultyRange(double value, double min,
                                            double mid, double max) noexcept {
  if (value > 5.0) {
    return mid + (max - mid) * (value - 5.0) / 5.0;
  }
  if (value < 5.0) {
    return mid - (mid - min) * (5.0 - value) / 5.0;
  }
  return mid;
}

// Spinner.ApplyDefaultsToSelf. Two rates: the one that clears the spinner and
// the one that also collects every bonus spin, both in revolutions per minute
// against the overall difficulty. The gap of two spins between them is where
// the bonus ticks begin.
inline constexpr int kBonusSpinsGap = 2;

[[nodiscard]] inline int spinsRequired(double durationMs, double od) noexcept {
  const double minRps = difficultyRange(od, 90.0, 150.0, 225.0) / 60.0;
  return static_cast<int>(minRps * (durationMs / 1000.0) + 0.0001);
}

[[nodiscard]] inline int maximumBonusSpins(double durationMs,
                                           double od) noexcept {
  const double maxRps = difficultyRange(od, 250.0, 380.0, 430.0) / 60.0;
  const int total =
      static_cast<int>(maxRps * (durationMs / 1000.0) + 0.0001);
  return std::max(0, total - spinsRequired(durationMs, od) - kBonusSpinsGap);
}

[[nodiscard]] inline double spinnerProgress(int rotations, double durationMs,
                                            double od) noexcept {
  return static_cast<double>(rotations) /
         std::max(1.0, static_cast<double>(spinsRequired(durationMs, od)));
}

// DrawableSpinner.CheckForResult: cleared, nearly, barely, or not at all.
[[nodiscard]] inline Judgement spinnerJudgement(double progress) noexcept {
  if (progress >= 1.0) {
    return judgement::Great{};
  }
  if (progress > 0.9) {
    return judgement::Good{};
  }
  if (progress > 0.75) {
    return judgement::Meh{};
  }
  return judgement::Miss{};
}

[[nodiscard]] inline int scoreValue(const Judgement &j) noexcept {
  return std::visit(Overloaded{
                        [](judgement::Miss) { return 0; },
                        [](judgement::Meh) { return 50; },
                        [](judgement::Good) { return 100; },
                        [](judgement::Great) { return 300; },
                    },
                    j);
}

// A slider is not one judgement in lazer. Its head is judged like a circle,
// its ticks and repeats are large ticks, and its tail is a result of its own;
// each carries its own base score, and each has its own say about the combo.
// These are the numbers ScoreProcessor.GetBaseScoreForResult returns.
enum class HitKind {
  kBasic,       // circle, slider head, spinner: 300 / 100 / 50 / miss
  kLargeTick,   // slider tick or repeat: 30 or nothing
  kSliderTail,  // the slider's end: 150 or nothing
  kSmallBonus,  // a spinner's spin: 10, and nothing else
  kLargeBonus,  // a spinner's spin past the bonus gap: 50
};

inline constexpr int kLargeTickScore = 30;
inline constexpr int kSliderTailScore = 150;
inline constexpr int kSmallBonusScore = 10;
inline constexpr int kLargeBonusScore = 50;

// A bonus result is scored, but it is not part of accuracy and not part of the
// combo: it lands straight on the total as its own portion.
[[nodiscard]] inline bool isBonus(HitKind kind) noexcept {
  return kind == HitKind::kSmallBonus || kind == HitKind::kLargeBonus;
}

// Combo and accuracy are two different questions and lazer answers them
// separately. HitResultExtensions.AffectsCombo lists large ticks and slider
// tails alongside the basic results, so hitting them raises the combo; a
// missed large tick breaks it, while a missed tail does not (its minimum
// result is IgnoreMiss). Accuracy is the base score over the maximum the
// judgements seen so far could have given, which is why a dropped tail costs
// 150 of accuracy rather than turning a 300 into a 100.
struct ScoreState {
  std::uint64_t fScore = 0;
  int fCombo = 0;
  int fMaxCombo = 0;
  int fGreat = 0;
  int fGood = 0;
  int fMeh = 0;
  int fMiss = 0;
  int fLargeTickHit = 0;
  int fLargeTickMiss = 0;
  int fSmallBonus = 0;
  int fLargeBonus = 0;
  double fBonusPortion = 0.0;
  int fTailHit = 0;
  int fTailMiss = 0;
  double fHealth = 1.0;

  // Running totals for the standardised score. The maxima come from a
  // simulated perfect play and are filled in by the engine before any
  // gameplay is registered.
  double fBaseScore = 0.0;
  double fMaxBaseScore = 0.0;
  double fComboPortion = 0.0;
  int fAccuracyJudgements = 0;
  double fMaxComboPortion = 0.0;
  int fMaxAccuracyJudgements = 0;

  [[nodiscard]] int totalHits() const noexcept {
    return fGreat + fGood + fMeh + fMiss;
  }

  [[nodiscard]] double accuracy() const noexcept {
    if (fMaxBaseScore <= 0.0) {
      return 1.0;
    }
    return fBaseScore / fMaxBaseScore;
  }

  // An .osr header carries the four legacy counts and nothing about ticks or
  // tails, so a score read back from one is scored on those alone.
  void adoptLegacyCounts() noexcept {
    fBaseScore = 300.0 * fGreat + 100.0 * fGood + 50.0 * fMeh;
    fMaxBaseScore = 300.0 * this->totalHits();
    fAccuracyJudgements = this->totalHits();
    fMaxAccuracyJudgements = this->totalHits();
  }
};

// ScoreProcessor.ComputeTotalScore: half of the million rides on the combo
// scaled by accuracy, half on accuracy to the fifth scaled by how much of the
// map has been judged.
[[nodiscard]] inline std::uint64_t standardisedScore(const ScoreState &state,
                                                     ModSet mods) noexcept {
  const double acc = state.accuracy();
  const double comboProgress =
      state.fMaxComboPortion > 0.0 ? state.fComboPortion / state.fMaxComboPortion
                                   : 1.0;
  const double accuracyProgress =
      state.fMaxAccuracyJudgements > 0
          ? static_cast<double>(state.fAccuracyJudgements) /
                static_cast<double>(state.fMaxAccuracyJudgements)
          : 1.0;
  const double total = 500000.0 * acc * comboProgress +
                       500000.0 * std::pow(acc, 5.0) * accuracyProgress +
                       state.fBonusPortion;
  return static_cast<std::uint64_t>(
      std::llround(total * modMultiplier(mods)));
}

// The maximum a result of this kind can give, which is what the combo portion
// and the accuracy denominator are both counted in.
[[nodiscard]] inline int maxBaseScore(HitKind kind) noexcept {
  switch (kind) {
  case HitKind::kLargeTick:
    return kLargeTickScore;
  case HitKind::kSliderTail:
    return kSliderTailScore;
  case HitKind::kBasic:
  default:
    return 300;
  }
}

// One judgement, of any kind. `hit` is only consulted for ticks and tails;
// for a basic result the judgement itself says whether it was a hit.
// Health is not touched here: the engine owns it, because it needs the drain
// between judgements and the bonus at the end of a combo, and neither is a
// property of the judgement alone.
inline void registerResult(ScoreState &state, HitKind kind, const Judgement &j,
                           bool hit, ModSet mods,
                           [[maybe_unused]] double hp = 5.0) noexcept {
  int base = 0;
  bool increasesCombo = false;
  bool breaksCombo = false;

  switch (kind) {
  case HitKind::kBasic:
    std::visit(Overloaded{
                   [&](judgement::Great) {
                     ++state.fGreat;
                     base = 300;
                     increasesCombo = true;
                   },
                   [&](judgement::Good) {
                     ++state.fGood;
                     base = 100;
                     increasesCombo = true;
                   },
                   [&](judgement::Meh) {
                     ++state.fMeh;
                     base = 50;
                     increasesCombo = true; // a 50 keeps the combo
                   },
                   [&](judgement::Miss) {
                     ++state.fMiss;
                     breaksCombo = true;
                   },
               },
               j);
    break;
  case HitKind::kLargeTick:
    if (hit) {
      ++state.fLargeTickHit;
      base = kLargeTickScore;
      increasesCombo = true;
    } else {
      ++state.fLargeTickMiss;
      breaksCombo = true;
    }
    break;
  case HitKind::kSliderTail:
    if (hit) {
      ++state.fTailHit;
      base = kSliderTailScore;
      increasesCombo = true;
    } else {
      ++state.fTailMiss; // IgnoreMiss: no combo break, but accuracy pays
    }
    break;
  case HitKind::kSmallBonus:
  case HitKind::kLargeBonus:
    if (hit) {
      const int amount = kind == HitKind::kSmallBonus ? kSmallBonusScore
                                                      : kLargeBonusScore;
      (kind == HitKind::kSmallBonus ? state.fSmallBonus : state.fLargeBonus)++;
      state.fBonusPortion += amount;
    }
    // A bonus touches neither accuracy nor combo, so nothing else happens.
    state.fScore = standardisedScore(state, mods);
    return;
  }

  if (increasesCombo) {
    ++state.fCombo;
  } else if (breaksCombo) {
    state.fCombo = 0;
  }
  state.fMaxCombo = std::max(state.fMaxCombo, state.fCombo);

  state.fBaseScore += base;
  state.fMaxBaseScore += maxBaseScore(kind);
  ++state.fAccuracyJudgements;
  // A dropped tail is an IgnoreMiss, which is not scorable, so it adds
  // nothing to the combo portion. Everything else does -- a miss adds its
  // maximum times a combo of zero, which is nothing, but by the same rule.
  if (increasesCombo || breaksCombo) {
    state.fComboPortion +=
        maxBaseScore(kind) * std::sqrt(static_cast<double>(state.fCombo));
  }
  state.fScore = standardisedScore(state, mods);
}

// Kept for callers that only deal in basic results.
inline void registerHit(ScoreState &state, const Judgement &j, ModSet mods,
                        double hp = 5.0) noexcept {
  registerResult(state, HitKind::kBasic, j, true, mods, hp);
}

// osu!'s health, which is two things at once: an increase for every judgement
// and a continuous drain in between. The drain rate is not a constant -- it is
// searched for so that a perfect play would dip to a target health and no
// lower, which is what makes a high HP map punishing and a low one forgiving.
//
// What one judgement does to the health bar, before the end-of-combo bonus.
// OsuHealthProcessor.getHealthIncreaseFor.
[[nodiscard]] inline double healthIncreaseFor(HitKind kind,
                                              const Judgement &j, bool hit,
                                              double hp,
                                              bool tick = false) noexcept {
  switch (kind) {
  case HitKind::kLargeTick:
    return hit ? (tick ? 0.015 : 0.02)
               : difficultyRange(hp, -0.02, -0.075, -0.14);
  case HitKind::kSliderTail:
    // A slider's tail is a large tick that is not a SliderTick, and missing it
    // is an IgnoreMiss, which costs no health at all.
    return hit ? 0.02 : 0.0;
  case HitKind::kSmallBonus:
    return hit ? 0.0085 : 0.0;
  case HitKind::kLargeBonus:
    return hit ? 0.01 : 0.0;
  case HitKind::kBasic:
  default:
    break;
  }
  return std::visit(
      Overloaded{
          [hp](judgement::Miss) {
            return difficultyRange(hp, -0.03, -0.125, -0.2);
          },
          [](judgement::Meh) { return 0.002; },
          [](judgement::Good) { return 0.011; },
          [](judgement::Great) { return 0.03; },
      },
      j);
}

// How well the combo just finished went, which decides the bonus for its last
// object. It only ever degrades within a combo.
enum class ComboResult { kNone = 0, kGood = 1, kPerfect = 2 };

[[nodiscard]] inline double comboBonusFor(ComboResult result) noexcept {
  switch (result) {
  case ComboResult::kPerfect:
    return 0.07;
  case ComboResult::kGood:
    return 0.05;
  case ComboResult::kNone:
  default:
    return 0.03;
  }
}

// DrainingHealthProcessor.ComputeDrainRate: double the adjustment until a
// perfect play's lowest health lands within a hundredth of the target.
[[nodiscard]] inline double
computeDrainRate(std::span<const std::pair<double, double>> increases,
                 std::span<const BreakPeriod> breaks, double drainStartTime,
                 double targetMinimumHealth) noexcept {
  if (increases.size() <= 1) {
    return 0.0;
  }
  constexpr double kMinimumHealthError = 0.01;
  int adjustment = 1;
  double result = 1.0;
  while (adjustment > 0) {
    double currentHealth = 1.0;
    double lowestHealth = 1.0;
    std::size_t currentBreak = 0;
    for (std::size_t i = 0; i < increases.size(); ++i) {
      const double currentTime = increases[i].first;
      double lastTime = i > 0 ? increases[i - 1].first : drainStartTime;
      while (currentBreak < breaks.size() &&
             breaks[currentBreak].fEnd <= currentTime) {
        lastTime = currentTime;
        ++currentBreak;
      }
      currentHealth -= (currentTime - lastTime) * result;
      lowestHealth = std::min(lowestHealth, currentHealth);
      currentHealth = std::min(1.0, currentHealth + increases[i].second);
      if (lowestHealth < 0.0) {
        break;
      }
    }
    if (std::abs(lowestHealth - targetMinimumHealth) <= kMinimumHealthError) {
      break;
    }
    adjustment *= 2;
    const double sign = lowestHealth > targetMinimumHealth   ? 1.0
                        : lowestHealth < targetMinimumHealth ? -1.0
                                                             : 0.0;
    result += 1.0 / adjustment * sign;
  }
  return result;
}

[[nodiscard]] inline double targetMinimumHealth(double hp,
                                                double lenience = 0.0) noexcept {
  double target = difficultyRange(hp, 0.99, 0.9, 0.4);
  target += lenience * (1.0 - target);
  return std::clamp(target, 0.0, 1.0);
}

[[nodiscard]] inline Grade computeGrade(const ScoreState &state) noexcept {
  if (state.fHealth < 0.0) {
    return grade::F{};
  }
  const double acc = state.accuracy();
  if (acc >= 1.0) {
    return grade::SS{};
  }
  if (acc >= 0.95) {
    return grade::S{};
  }
  if (acc >= 0.9) {
    return grade::A{};
  }
  if (acc >= 0.8) {
    return grade::B{};
  }
  if (acc >= 0.7) {
    return grade::C{};
  }
  return grade::D{};
}

[[nodiscard]] inline const char *gradeString(const Grade &g) noexcept {
  return std::visit(Overloaded{
                        [](grade::SS) { return "SS"; },
                        [](grade::S) { return "S"; },
                        [](grade::A) { return "A"; },
                        [](grade::B) { return "B"; },
                        [](grade::C) { return "C"; },
                        [](grade::D) { return "D"; },
                        [](grade::F) { return "F"; },
                    },
                    g);
}

[[nodiscard]] inline std::pair<const char *, std::array<std::uint8_t, 3>>
judgementInfo(const Judgement &j) noexcept {
  return std::visit(
      Overloaded{
          [](judgement::Great)
              -> std::pair<const char *, std::array<std::uint8_t, 3>> {
            return {"great", {102, 204, 255}};
          },
          [](judgement::Good)
              -> std::pair<const char *, std::array<std::uint8_t, 3>> {
            return {"good", {136, 179, 0}};
          },
          [](judgement::Meh)
              -> std::pair<const char *, std::array<std::uint8_t, 3>> {
            return {"meh", {255, 204, 34}};
          },
          [](judgement::Miss)
              -> std::pair<const char *, std::array<std::uint8_t, 3>> {
            return {"miss", {237, 17, 33}};
          },
      },
      j);
}

} // namespace osu

export template <>
struct std::formatter<osu::ModSet> : std::formatter<std::string_view> {
  template <class FormatContext>
  auto format(osu::ModSet mods, FormatContext &ctx) const {
    auto out = ctx.out();
    bool first = true;
    auto append = [&](std::string_view name) {
      if (!first) {
        out = std::format_to(out, " ");
      }
      first = false;
      out = std::ranges::copy(name, out).out;
    };
    if (mods & osu::mod::kAuto)
      append("Auto");
    if (mods & osu::mod::kAutopilot)
      append("Pilot");
    if (mods & osu::mod::kDoubleTime)
      append("DT");
    if (mods & osu::mod::kHalfTime)
      append("HT");
    if (mods & osu::mod::kHardRock)
      append("HR");
    if (mods & osu::mod::kEasy)
      append("EZ");
    if (mods & osu::mod::kHidden)
      append("HD");
    if (mods & osu::mod::kNoFail)
      append("NF");
    return out;
  }
};

export template <>
struct std::formatter<osu::ScoreState> : std::formatter<std::string_view> {
  template <class FormatContext>
  auto format(const osu::ScoreState &state, FormatContext &ctx) const {
    return std::format_to(ctx.out(),
                          "Score: {}\nCombo: {}/{}\nAccuracy: {:.4f}\n"
                          "Great/Good/Meh/Miss: {}/{}/{}/{}\nGrade: {}",
                          state.fScore, state.fCombo, state.fMaxCombo,
                          state.accuracy(), state.fGreat, state.fGood,
                          state.fMeh, state.fMiss,
                          osu::gradeString(osu::computeGrade(state)));
  }
};
