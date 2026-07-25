export module osu.stars:rhythm;

import :core;
import std;
import osu.types;

export namespace osu {
namespace stars {

class RhEvaluator {
  struct Island {
    int fDelta;
    int fDeltaCount = 1;
    int fOccurrences = 1;
    explicit Island(int delta) : fDelta(std::max(delta, kMDT)) {}
    void addDelta(int d) {
      if (fDelta == std::numeric_limits<int>::max())
        fDelta = std::max(d, kMDT);
      ++fDeltaCount;
    }
    bool isSimilarPolarity(const Island &o, double eps) const {
      if (fDeltaCount <= 1 || o.fDeltaCount <= 1)
        return false;
      return std::abs(fDelta - o.fDelta) < eps &&
             fDeltaCount % 2 == o.fDeltaCount % 2;
    }
    bool almostEquals(const Island &o, double eps) const {
      return std::abs(fDelta - o.fDelta) < eps && fDeltaCount == o.fDeltaCount;
    }
  };

  static double getEffectiveDifficulty(double ratio) {
    constexpr double km = 26.0;
    double frac = ratio - std::trunc(ratio);
    return 1.0 + km * std::min(0.5, diffutil::smoothstepBellCurve(frac));
  }

public:
  static double eval(const OsuDifficultyHitObject &c) {
    if (std::holds_alternative<Spinner>(*c.fBase))
      return 0;

    constexpr int kHistoryTimeMax = 5000;
    constexpr int kHistoryObjMax = 32;
    constexpr double kOverallMul = 0.95;

    double rcSum = 0;
    double eps = c.fHW * 0.3;

    Island island(std::numeric_limits<int>::max());
    Island prevIsland(std::numeric_limits<int>::max());
    std::vector<Island> islands;
    double startDifficulty = 0;
    bool firstDeltaSwitch = false;

    int histCount = std::min(c.fIdx, kHistoryObjMax);
    int rhythmStart = 0;
    while (rhythmStart < histCount - 2 &&
           c.fStart - c.prev(rhythmStart)->fStart <
               static_cast<double>(kHistoryTimeMax))
      ++rhythmStart;

    const auto *pPrevObj = c.prev(rhythmStart);
    const auto *pPrevPrevObj = c.prev(rhythmStart + 1);

    for (int i = rhythmStart; i > 0; --i) {
      const auto *pCurrObj = c.prev(i - 1);
      if (std::holds_alternative<Spinner>(*pCurrObj->fBase))
        continue;

      double timeDecay = (kHistoryTimeMax - (c.fStart - pCurrObj->fStart)) /
                         static_cast<double>(kHistoryTimeMax);
      double noteDecay =
          static_cast<double>(histCount - i) / static_cast<double>(histCount);
      double currHistDecay = std::min(noteDecay, timeDecay);

      constexpr double kDeltaMin = 1e-7;
      double currDelta = std::max(pCurrObj->fRawDT, kDeltaMin);
      double prevDelta = std::max(pPrevObj->fRawDT, kDeltaMin);

      double deltaDiff = std::abs(prevDelta - currDelta);

      if (island.fDelta == std::numeric_limits<int>::max())
        island = Island(static_cast<int>(currDelta));

      double deltaDiffRatio =
          std::max(prevDelta, currDelta) / std::min(prevDelta, currDelta);
      double diffMul = std::clamp(2.0 - deltaDiffRatio / 8.0, 0.0, 1.0);
      double windowPenalty = std::clamp((deltaDiff - eps) / eps, 0.0, 1.0);

      double effectiveDiff =
          getEffectiveDifficulty(deltaDiffRatio) * windowPenalty * diffMul;

      if (std::holds_alternative<Slider>(*pPrevObj->fBase)) {
        double slideLazyEndDelta = pCurrObj->fMJT;
        double slideLazyRatio = std::max(slideLazyEndDelta, currDelta) /
                                std::min(slideLazyEndDelta, currDelta);
        double slideRealEndDelta = pCurrObj->fLEDT;
        double slideRealRatio = std::max(slideRealEndDelta, currDelta) /
                                std::min(slideRealEndDelta, currDelta);
        double slideEff = std::min(getEffectiveDifficulty(slideLazyRatio),
                                   getEffectiveDifficulty(slideRealRatio));
        effectiveDiff = std::min(slideEff, effectiveDiff);
      }

      if (deltaDiff < eps) {
        island.addDelta(static_cast<int>(currDelta));
      }

      if (firstDeltaSwitch) {
        if (deltaDiff > eps) {
          if (std::holds_alternative<Slider>(*pCurrObj->fBase))
            effectiveDiff *= 0.5;

          if (island.isSimilarPolarity(prevIsland, eps))
            effectiveDiff *= 0.5;

          if (std::max(pPrevPrevObj->fRawDT, kDeltaMin) > prevDelta + eps &&
              prevDelta > currDelta + eps)
            effectiveDiff *= 0.125;

          if (prevIsland.fDeltaCount == island.fDeltaCount)
            effectiveDiff *= 0.5;

          bool speedingUp = prevDelta > currDelta + eps;
          if (speedingUp)
            effectiveDiff *= 0.65;

          bool found = false;
          for (auto &ex : islands) {
            if (ex.almostEquals(island, eps)) {
              if (prevIsland.almostEquals(island, eps))
                ++ex.fOccurrences;
              double power = diffutil::logistic(
                  static_cast<double>(island.fDelta), 58.33, 0.24, 2.75);
              effectiveDiff *= std::min(3.0 / ex.fOccurrences,
                                        std::pow(1.0 / ex.fOccurrences, power));
              found = true;
              break;
            }
          }

          if (!found && island.fDeltaCount > 0)
            islands.push_back(island);

          effectiveDiff *=
              1 - pPrevObj->calculateDoubleTapFeasibility(pCurrObj) * 0.75;

          if (island.fDeltaCount > 1)
            rcSum += std::sqrt(effectiveDiff * startDifficulty) * currHistDecay;
          else
            rcSum += 0.7 * currHistDecay;

          startDifficulty = effectiveDiff;

          if (prevDelta + eps < currDelta)
            firstDeltaSwitch = false;

          prevIsland = island;
          island = Island(static_cast<int>(currDelta));
        }
      } else if (prevDelta > currDelta + eps) {
        firstDeltaSwitch = true;

        if (std::holds_alternative<Slider>(*pCurrObj->fBase))
          effectiveDiff *= 0.6;
        if (std::holds_alternative<Slider>(*pPrevObj->fBase))
          effectiveDiff *= 0.6;

        startDifficulty = effectiveDiff;
        island = Island(static_cast<int>(currDelta));
      }

      pPrevPrevObj = pPrevObj;
      pPrevObj = pCurrObj;
    }

    rcSum *= diffutil::reverseLerp(static_cast<double>(island.fDeltaCount),
                                   22.0, 3.0);

    return std::sqrt(4.0 + rcSum * kOverallMul) / 2.0;
  }
};

} // namespace stars
} // namespace osu
