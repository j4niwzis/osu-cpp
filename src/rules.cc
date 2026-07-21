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

[[nodiscard]] inline double windowGreat(double od) noexcept {
  return 80.0 - 6.0 * od;
}
[[nodiscard]] inline double windowGood(double od) noexcept {
  return 140.0 - 8.0 * od;
}
[[nodiscard]] inline double windowMeh(double od) noexcept {
  return 200.0 - 10.0 * od;
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

[[nodiscard]] inline double preemptTime(double ar) noexcept {
  if (ar <= 5.0) {
    return 1200.0 + 600.0 * (5.0 - ar) / 5.0;
  }
  return 1200.0 - 750.0 * (ar - 5.0) / 5.0;
}

[[nodiscard]] inline double fadeInTime(double ar) noexcept {
  return std::min(400.0, preemptTime(ar));
}

[[nodiscard]] inline double approachFadeInTime(double ar) noexcept {
  return std::min(800.0, preemptTime(ar));
}

[[nodiscard]] inline double circleRadius(double cs) noexcept {
  return (109.0 - 9.0 * cs) / 2.0;
}

[[nodiscard]] inline double spinnerRotationsPerSecond(double od) noexcept {
  const double base = (od < 5.0) ? 3.0 + 0.4 * od : 2.5 + 0.5 * od;
  return base * 0.7;
}

[[nodiscard]] inline double spinnerRequiredRotations(double durationMs,
                                                     double od) noexcept {
  return durationMs / 1000.0 * spinnerRotationsPerSecond(od);
}

[[nodiscard]] inline double spinnerProgress(int rotations, double durationMs,
                                            double od) noexcept {
  return static_cast<double>(rotations) /
         std::max(1.0, spinnerRequiredRotations(durationMs, od));
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

struct ScoreState {
  std::uint64_t fScore = 0;
  int fCombo = 0;
  int fMaxCombo = 0;
  int fGreat = 0;
  int fGood = 0;
  int fMeh = 0;
  int fMiss = 0;
  double fHealth = 1.0;

  [[nodiscard]] int totalHits() const noexcept {
    return fGreat + fGood + fMeh + fMiss;
  }

  [[nodiscard]] double accuracy() const noexcept {
    const int total = totalHits();
    if (total == 0) {
      return 1.0;
    }
    return (300 * fGreat + 100 * fGood + 50 * fMeh) / (300.0 * total);
  }
};

inline void registerHit(ScoreState &state, const Judgement &j, ModSet mods,
                        double hp = 5.0) noexcept {
  auto applyHp = [&](double inc) {
    if (state.fHealth >= 0.0) {
      state.fHealth += inc;
      state.fHealth = std::min(1.0, state.fHealth);
    }
  };
  std::visit(Overloaded{
                 [&state, &applyHp, hp](judgement::Great) {
                   ++state.fGreat;
                   state.fCombo++;
                   applyHp(0.01 * (10.2 - hp));
                 },
                 [&state, &applyHp, hp](judgement::Good) {
                   ++state.fGood;
                   state.fCombo++;
                   applyHp(0.01 * (8.0 - hp));
                 },
                 [&state, &applyHp, hp](judgement::Meh) {
                   ++state.fMeh;
                   state.fCombo = 0;
                   applyHp(0.01 * (4.0 - hp));
                 },
                 [&state, &applyHp, hp](judgement::Miss) {
                   ++state.fMiss;
                   state.fCombo = 0;
                   applyHp(-0.02 * hp);
                 },
             },
             j);
  state.fMaxCombo = std::max(state.fMaxCombo, state.fCombo);
  const double comboFactor = 1.0 + state.fCombo / 25.0;
  const double modFactor = modMultiplier(mods);
  state.fScore +=
      static_cast<std::uint64_t>(scoreValue(j) * comboFactor * modFactor);
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
