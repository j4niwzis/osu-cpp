export module osu.stars:ranked;

import :core;
import std;
import osu.types;
import osu.rules;
import osu.beatmap;

// The difficulty calculator the servers run: what a beatmap page shows as its
// star rating, and what every ranked score was weighed against. ppy/osu master
// has moved on to a reworked set of skills, so the two disagree -- by three and
// a half percent on a map we measured -- and both are worth having.
//
// Ported from the algorithm as rosu-pp implements it, which reproduces the
// published rating exactly. The preprocessing is shared with the rework and
// selected by DifficultyMode::kRanked; what differs is here: the aim and speed
// evaluators, the rhythm evaluator, the strain aggregation, and the ratings.
export namespace osu {
namespace stars {
namespace ranked {

inline constexpr double kSectionLength = 400.0;
inline constexpr double kDecayWeight = 0.9;
inline constexpr double kReducedStrainBaseline = 0.75;
inline constexpr double kDifficultyMultiplier = 0.0675;
inline constexpr double kStarRatingMultiplier = 0.0265;
inline constexpr double kPerformanceBaseMultiplier = 1.14;

[[nodiscard]] inline double lerp(double a, double b, double t) noexcept {
  return a + (b - a) * t;
}

[[nodiscard]] inline double strainDecay(double ms, double base) noexcept {
  return std::pow(base, ms / 1000.0);
}

// pow(5 * max(1, d / 0.0675) - 4, 3) / 100000
[[nodiscard]] inline double difficultyToPerformance(double difficulty) noexcept {
  return std::pow(5.0 * std::max(1.0, difficulty / kDifficultyMultiplier) - 4.0,
                  3.0) /
         100000.0;
}

// How likely the object can be tapped together with the next one. The rework
// asks this differently, so this is the ranked calculator's own.
[[nodiscard]] inline double doubletapness(const OsuDifficultyHitObject &c,
                                          const OsuDifficultyHitObject *next,
                                          double hitWindow) noexcept {
  if (next == nullptr) {
    return 0.0;
  }
  const double window =
      std::holds_alternative<Spinner>(*c.fBase) ? 0.0 : hitWindow;
  const double currDelta = std::max(c.fRawDT, 1.0);
  const double nextDelta = std::max(next->fRawDT, 1.0);
  const double deltaDiff = std::abs(nextDelta - currDelta);
  const double speedRatio = currDelta / std::max(currDelta, deltaDiff);
  const double windowRatio =
      std::pow(window > 0.0 ? std::min(1.0, currDelta / window) : 1.0, 2.0);
  return 1.0 - std::pow(speedRatio, 1.0 - windowRatio);
}

class AimEvaluator {
public:
  static double eval(const OsuDifficultyHitObject &c, bool withSliders) {
    constexpr double kWideAngle = 1.5, kAcuteAngle = 2.55, kSlider = 1.35,
                     kVelocityChange = 0.75, kWiggle = 1.02;

    const auto *last = c.prev(0);
    const auto *last2 = c.prev(1);
    if (last == nullptr || last2 == nullptr ||
        std::holds_alternative<Spinner>(*c.fBase) ||
        std::holds_alternative<Spinner>(*last->fBase)) {
      return 0.0;
    }

    double currVel = c.fLJD / c.fADT;
    if (std::holds_alternative<Slider>(*last->fBase) && withSliders) {
      const double travelVel = last->fTD / last->fTT;
      const double movementVel = c.fMJD / c.fMJT;
      currVel = std::max(currVel, movementVel + travelVel);
    }
    double prevVel = last->fLJD / last->fADT;
    if (std::holds_alternative<Slider>(*last2->fBase) && withSliders) {
      const double travelVel = last2->fTD / last2->fTT;
      const double movementVel = last->fMJD / last->fMJT;
      prevVel = std::max(prevVel, movementVel + travelVel);
    }

    double wideAngleBonus = 0.0, acuteAngleBonus = 0.0, sliderBonus = 0.0,
           velChangeBonus = 0.0, wiggleBonus = 0.0;
    double strain = currVel;

    if (c.fA && last->fA) {
      const double currAngle = *c.fA;
      const double lastAngle = *last->fA;
      const double angleBonus = std::min(currVel, prevVel);

      if (std::max(c.fADT, last->fADT) < 1.25 * std::min(c.fADT, last->fADT)) {
        acuteAngleBonus = acute(currAngle);
        acuteAngleBonus *=
            0.08 + 0.92 * (1.0 - std::min(acuteAngleBonus,
                                          std::pow(acute(lastAngle), 3.0)));
        acuteAngleBonus *=
            angleBonus *
            diffutil::smootherstep(diffutil::msToBpm(c.fADT, 2), 300.0, 400.0) *
            diffutil::smootherstep(c.fLJD, kND, kND * 2.0);
      }

      wideAngleBonus = wide(currAngle);
      wideAngleBonus *=
          1.0 - std::min(wideAngleBonus, std::pow(wide(lastAngle), 3.0));
      wideAngleBonus *=
          angleBonus * diffutil::smootherstep(c.fLJD, 0.0, kND);

      wiggleBonus =
          angleBonus * diffutil::smootherstep(c.fLJD, kNR, kND) *
          std::pow(diffutil::reverseLerp(c.fLJD, kND * 3.0, kND), 1.8) *
          diffutil::smootherstep(currAngle, std::numbers::pi * 110.0 / 180.0,
                                 std::numbers::pi * 60.0 / 180.0) *
          diffutil::smootherstep(last->fLJD, kNR, kND) *
          std::pow(diffutil::reverseLerp(last->fLJD, kND * 3.0, kND), 1.8) *
          diffutil::smootherstep(lastAngle, std::numbers::pi * 110.0 / 180.0,
                                 std::numbers::pi * 60.0 / 180.0);

      if (const auto *last2obj = c.prev(2); last2obj != nullptr) {
        const Vec2 a =
            OsuDifficultyHitObject::sp(*last2obj->fBase, last2obj->fCsR);
        const Vec2 b = OsuDifficultyHitObject::sp(*last->fBase, last->fCsR);
        const double distance = (a - b).length();
        if (distance < 1.0) {
          wideAngleBonus *= 1.0 - 0.35 * (1.0 - distance);
        }
      }
    }

    if (std::max(prevVel, currVel) != 0.0) {
      // The average velocity over the whole object, not the jump alone.
      prevVel = (last->fLJD + last2->fTD) / last->fADT;
      currVel = (c.fLJD + last->fTD) / c.fADT;

      const double distRatio = diffutil::smoothstep(
          std::abs(prevVel - currVel) / std::max(prevVel, currVel), 0.0, 1.0);
      const double overlapVelBuff =
          std::min(kND * 1.25 / std::min(c.fADT, last->fADT),
                   std::abs(prevVel - currVel));
      velChangeBonus = overlapVelBuff * distRatio;
      const double bonusBase =
          std::min(c.fADT, last->fADT) / std::max(c.fADT, last->fADT);
      velChangeBonus *= std::pow(bonusBase, 2.0);
    }

    if (std::holds_alternative<Slider>(*last->fBase)) {
      sliderBonus = last->fTD / last->fTT;
    }

    strain += wiggleBonus * kWiggle;
    strain += velChangeBonus * kVelocityChange;
    strain += std::max(acuteAngleBonus * kAcuteAngle, wideAngleBonus * kWideAngle);
    strain *= c.fSCB;
    if (withSliders) {
      strain += sliderBonus * kSlider;
    }
    return strain;
  }

private:
  static double wide(double angle) {
    return diffutil::smoothstep(angle, std::numbers::pi * 40.0 / 180.0,
                                std::numbers::pi * 140.0 / 180.0);
  }
  static double acute(double angle) {
    return diffutil::smoothstep(angle, std::numbers::pi * 140.0 / 180.0,
                                std::numbers::pi * 40.0 / 180.0);
  }
};

class SpeedEvaluator {
public:
  static double eval(const OsuDifficultyHitObject &c, double hitWindow) {
    constexpr double kSingleSpacing = kND * 1.25;
    constexpr double kMinSpeedBonus = 200.0;
    constexpr double kSpeedBalancing = 40.0;
    constexpr double kDistMultiplier = 0.8;

    if (std::holds_alternative<Spinner>(*c.fBase)) {
      return 0.0;
    }

    const auto *prev = c.prev(0);
    const auto *next = c.next(0);

    double strainTime = c.fADT;
    const double tapness = 1.0 - doubletapness(c, next, hitWindow);

    strainTime /= std::clamp((strainTime / hitWindow) / 0.93, 0.92, 1.0);

    double speedBonus = 0.0;
    if (diffutil::msToBpm(strainTime) > kMinSpeedBonus) {
      const double base =
          (diffutil::bpmToMs(kMinSpeedBonus) - strainTime) / kSpeedBalancing;
      speedBonus = 0.75 * std::pow(base, 2.0);
    }

    const double travel = prev != nullptr ? prev->fTD : 0.0;
    double dist = std::min(kSingleSpacing, travel + c.fMJD);
    double distBonus = std::pow(dist / kSingleSpacing, 3.95) * kDistMultiplier;
    distBonus *= std::sqrt(c.fSCB);

    const double difficulty = (1.0 + speedBonus + distBonus) * 1000.0 / strainTime;
    return difficulty * tapness;
  }
};

class RhythmEvaluator {
  struct Island {
    double fEps = 0.0;
    int fDelta = 0;
    int fCount = 0;

    static Island empty(double eps) { return Island{eps, 0, 0}; }
    static Island of(int delta, double eps) {
      return Island{eps, std::max(delta, kMDT), 1};
    }
    void add(int delta) {
      if (fDelta == std::numeric_limits<int>::max()) {
        fDelta = std::max(delta, kMDT);
      }
      ++fCount;
    }
    [[nodiscard]] bool similarPolarity(const Island &o) const {
      return fCount % 2 == o.fCount % 2;
    }
    [[nodiscard]] bool isDefault() const {
      return std::abs(fEps) < std::numeric_limits<double>::epsilon() &&
             fDelta == 0 && fCount == 0;
    }
    [[nodiscard]] bool equals(const Island &o) const {
      return std::abs(fDelta - o.fDelta) < fEps && fCount == o.fCount;
    }
  };

  struct IslandCount {
    Island fIsland;
    int fCount = 1;
  };

public:
  static double eval(const OsuDifficultyHitObject &c, double hitWindow) {
    constexpr double kHistoryTimeMax = 5000.0;
    constexpr int kHistoryObjectsMax = 32;
    constexpr double kOverallMultiplier = 1.0;
    constexpr double kRatioMultiplier = 15.0;

    if (std::holds_alternative<Spinner>(*c.fBase)) {
      return 0.0;
    }

    double complexity = 0.0;
    const double eps = hitWindow * 0.3;

    Island island = Island::empty(eps);
    Island prevIsland = Island::empty(eps);
    std::vector<IslandCount> counts;
    double startRatio = 0.0;
    bool firstDeltaSwitch = false;

    const int historical = std::min(c.fIdx, kHistoryObjectsMax);
    int rhythmStart = 0;
    while (c.prev(rhythmStart) != nullptr &&
           rhythmStart + 2 < historical &&
           c.fStart - c.prev(rhythmStart)->fStart < kHistoryTimeMax) {
      ++rhythmStart;
    }

    const auto *prevObj = c.prev(rhythmStart);
    const auto *lastObj = c.prev(rhythmStart + 1);
    if (prevObj == nullptr || lastObj == nullptr) {
      return std::sqrt(4.0) / 2.0 *
             (1.0 - doubletapness(c, c.next(0), hitWindow));
    }

    for (int i = rhythmStart; i >= 1; --i) {
      const auto *currObj = c.prev(i - 1);
      if (currObj == nullptr) {
        break;
      }

      const double timeDecay =
          (kHistoryTimeMax - (c.fStart - currObj->fStart)) / kHistoryTimeMax;
      const double noteDecay = static_cast<double>(historical - i) /
                               static_cast<double>(historical);
      const double historicalDecay = std::min(noteDecay, timeDecay);

      const double currDelta = std::max(currObj->fRawDT, 1e-7);
      const double prevDelta = std::max(prevObj->fRawDT, 1e-7);
      const double lastDelta = std::max(lastObj->fRawDT, 1e-7);

      const double deltaDifference = std::max(prevDelta, currDelta) /
                                     std::min(prevDelta, currDelta);
      const double fraction = deltaDifference - std::trunc(deltaDifference);
      const double currRatio =
          1.0 + kRatioMultiplier *
                    std::min(0.5, diffutil::smoothstepBellCurve(fraction));
      const double differenceMultiplier =
          std::clamp(2.0 - deltaDifference / 8.0, 0.0, 1.0);
      const double windowPenalty = std::min(
          1.0, std::max(0.0, std::abs(prevDelta - currDelta) - eps) / eps);

      double effectiveRatio = windowPenalty * currRatio * differenceMultiplier;

      if (firstDeltaSwitch) {
        if (std::abs(prevDelta - currDelta) < eps) {
          island.add(static_cast<int>(currDelta));
        } else {
          if (std::holds_alternative<Slider>(*currObj->fBase)) {
            effectiveRatio *= 0.125;
          }
          if (std::holds_alternative<Slider>(*prevObj->fBase)) {
            effectiveRatio *= 0.3;
          }
          if (island.similarPolarity(prevIsland)) {
            effectiveRatio *= 0.5;
          }
          if (lastDelta > prevDelta + eps && prevDelta > currDelta + eps) {
            effectiveRatio *= 0.125;
          }
          if (prevIsland.fCount == island.fCount) {
            effectiveRatio *= 0.5;
          }

          auto found = std::ranges::find_if(counts, [&](const IslandCount &e) {
            return e.fIsland.equals(island) && !e.fIsland.isDefault();
          });
          if (found != counts.end()) {
            if (prevIsland.equals(island)) {
              ++found->fCount;
            }
            const double power = diffutil::logistic(
                static_cast<double>(island.fDelta), 58.33, 0.24, 2.75);
            effectiveRatio *=
                std::min(3.0 / found->fCount,
                         std::pow(1.0 / found->fCount, power));
          } else {
            counts.push_back({island, 1});
          }

          effectiveRatio *=
              1.0 - doubletapness(*prevObj, currObj, hitWindow) * 0.75;

          complexity += std::sqrt(effectiveRatio * startRatio) * historicalDecay;
          startRatio = effectiveRatio;
          prevIsland = island;

          if (prevDelta + eps < currDelta) {
            firstDeltaSwitch = false;
          }
          island = Island::of(static_cast<int>(currDelta), eps);
        }
      } else if (prevDelta > currDelta + eps) {
        firstDeltaSwitch = true;
        if (std::holds_alternative<Slider>(*currObj->fBase)) {
          effectiveRatio *= 0.6;
        }
        if (std::holds_alternative<Slider>(*prevObj->fBase)) {
          effectiveRatio *= 0.6;
        }
        startRatio = effectiveRatio;
        island = Island::of(static_cast<int>(currDelta), eps);
      }

      lastObj = prevObj;
      prevObj = currObj;
    }

    double rhythm = std::sqrt(4.0 + complexity * kOverallMultiplier) / 2.0;
    rhythm *= 1.0 - doubletapness(c, c.next(0), hitWindow);
    return rhythm;
  }
};

// The classic strain skill: fixed sections of 400 ms, a peak per section, and
// the peaks summed in descending order with a geometric weight, after the top
// few have been dragged down towards a baseline.
class SectionPeaks {
public:
  explicit SectionPeaks(int reducedSections) : fReduced(reducedSections) {}

  void start(double startTime) {
    fSectionEnd = std::ceil(startTime / kSectionLength) * kSectionLength;
  }

  // A new section starts from the running strain decayed to *that section's*
  // end -- StrainSkill.startNewSectionFrom is handed currentSectionEnd, which
  // moves with every turn of the loop, and StrainDecaySkill decays from the
  // previous object's start time to it. Decaying to the current object's
  // start instead, which is what this did, is a longer gap and so too small a
  // strain, and it gave every section in a run of them the same value rather
  // than a falling one.
  void advance(double startTime, double strain, double decayBase,
               double previousStart) {
    while (startTime > fSectionEnd) {
      fPeaks.push_back(fSectionPeak);
      fSectionPeak = strain * strainDecay(fSectionEnd - previousStart,
                                          decayBase);
      fSectionEnd += kSectionLength;
    }
  }

  void record(double strain) { fSectionPeak = std::max(strain, fSectionPeak); }

  // Every section's peak, in order, which is what a reference implementation
  // can be diffed against section by section.
  [[nodiscard]] std::vector<double> sectionPeaks() const {
    std::vector<double> peaks(fPeaks.begin(), fPeaks.end());
    peaks.push_back(fSectionPeak);
    return peaks;
  }

  [[nodiscard]] double difficulty() const {
    std::vector<double> peaks;
    peaks.reserve(fPeaks.size() + 1);
    for (double p : fPeaks) {
      if (p > 0.0) {
        peaks.push_back(p);
      }
    }
    if (fSectionPeak > 0.0) {
      peaks.push_back(fSectionPeak);
    }
    std::ranges::sort(peaks, std::greater<>{});

    const int reduced = std::min(static_cast<int>(peaks.size()), fReduced);
    for (int i = 0; i < reduced; ++i) {
      const double clamped =
          std::clamp(static_cast<double>(static_cast<float>(i) /
                                         static_cast<float>(fReduced)),
                     0.0, 1.0);
      const double scale = std::log10(lerp(1.0, 10.0, clamped));
      peaks[static_cast<std::size_t>(i)] *=
          lerp(kReducedStrainBaseline, 1.0, scale);
    }
    std::ranges::sort(peaks, std::greater<>{});

    double difficulty = 0.0;
    double weight = 1.0;
    for (double p : peaks) {
      difficulty += p * weight;
      weight *= kDecayWeight;
    }
    return difficulty;
  }

private:
  std::vector<double> fPeaks;
  double fSectionPeak = 0.0;
  double fSectionEnd = 0.0;
  int fReduced;
};

// StrainSkill.CountTopWeightedStrains: how many objects carry a strain worth
// counting, as a soft count rather than a threshold. The reference strain is
// what the top one would be if every object were equally hard, which is the
// difficulty value times (1 - DecayWeight).
[[nodiscard]] inline double
countTopWeightedStrains(const std::vector<double> &strains,
                        double difficultyValue) {
  if (strains.empty()) {
    return 0.0;
  }
  const double consistentTop = difficultyValue * (1.0 - kDecayWeight);
  if (consistentTop == 0.0) {
    return static_cast<double>(strains.size());
  }
  double total = 0.0;
  for (const double s : strains) {
    total += diffutil::logistic(s / consistentTop, 0.88, 10.0, 1.1);
  }
  return total;
}

class AimSkill {
public:
  explicit AimSkill(bool withSliders)
      : fWithSliders(withSliders), fPeaks(10) {}

  void process(const OsuDifficultyHitObject &c) {
    if (c.fIdx == 0) {
      fPeaks.start(c.fStart);
    } else {
      const auto *prev = c.prev(0);
      fPeaks.advance(c.fStart, fStrain, kDecayBase,
                     prev != nullptr ? prev->fStart : 0.0);
    }
    // The aim strain decays over the raw delta, not the capped one.
    fStrain *= strainDecay(c.fRawDT, kDecayBase);
    fStrain += AimEvaluator::eval(c, fWithSliders) * kSkillMultiplier;
    fPeaks.record(fStrain);
    fObjectStrains.push_back(fStrain);
  }

  [[nodiscard]] double difficulty() const { return fPeaks.difficulty(); }
  [[nodiscard]] std::vector<double> sectionPeaks() const {
    return fPeaks.sectionPeaks();
  }
  // Per object rather than per section: the counts below are about how many
  // objects are hard, which the section peaks have already thrown away.
  [[nodiscard]] const std::vector<double> &objectStrains() const noexcept {
    return fObjectStrains;
  }

private:
  static constexpr double kDecayBase = 0.15;
  static constexpr double kSkillMultiplier = 26.0;
  bool fWithSliders;
  double fStrain = 0.0;
  SectionPeaks fPeaks;
  std::vector<double> fObjectStrains;
};

class SpeedSkill {
public:
  explicit SpeedSkill(double hitWindow) : fHitWindow(hitWindow), fPeaks(5) {}

  void process(const OsuDifficultyHitObject &c) {
    if (c.fIdx == 0) {
      fPeaks.start(c.fStart);
    } else {
      const auto *prev = c.prev(0);
      fPeaks.advance(c.fStart, fStrain * fRhythm, kDecayBase,
                     prev != nullptr ? prev->fStart : 0.0);
    }
    fStrain *= strainDecay(c.fADT, kDecayBase);
    fStrain += SpeedEvaluator::eval(c, fHitWindow) * kSkillMultiplier;
    fRhythm = RhythmEvaluator::eval(c, fHitWindow);
    fPeaks.record(fStrain * fRhythm);
    fObjectStrains.push_back(fStrain * fRhythm);
  }

  [[nodiscard]] const std::vector<double> &objectStrains() const noexcept {
    return fObjectStrains;
  }

  // Speed.RelevantNoteCount: the notes that carry the speed rating, weighted
  // by how close each one is to the hardest of them rather than counted past
  // a threshold.
  [[nodiscard]] double relevantNoteCount() const {
    double top = 0.0;
    for (const double s : fObjectStrains) {
      top = std::max(top, s);
    }
    if (top <= 0.0) {
      return 0.0;
    }
    double total = 0.0;
    for (const double s : fObjectStrains) {
      total += 1.0 / (1.0 + std::exp(-(s / top * 12.0 - 6.0)));
    }
    return total;
  }

  [[nodiscard]] double difficulty() const { return fPeaks.difficulty(); }
  [[nodiscard]] std::vector<double> sectionPeaks() const {
    return fPeaks.sectionPeaks();
  }

private:
  static constexpr double kDecayBase = 0.3;
  static constexpr double kSkillMultiplier = 1.47;
  double fHitWindow;
  double fStrain = 0.0;
  std::vector<double> fObjectStrains;
  double fRhythm = 0.0;
  SectionPeaks fPeaks;
};

// The approach rate a preempt time corresponds to, which is how the rating
// calculator sees a map under a rate change.
[[nodiscard]] inline double approachRateFrom(double preempt) noexcept {
  return preempt > 1200.0 ? (1800.0 - preempt) / 120.0
                          : (1200.0 - preempt) / 150.0 + 5.0;
}

[[nodiscard]] inline double
lengthBonus(int totalHits) noexcept {
  const double hits = static_cast<double>(totalHits);
  double bonus = 0.95 + 0.4 * std::min(1.0, hits / 2000.0);
  if (totalHits > 2000) {
    bonus += std::log10(hits / 2000.0) * 0.5;
  }
  return bonus;
}

[[nodiscard]] inline StarRating calculate(
    const Beatmap &bm, ModSet mods = mod::kNone,
    std::vector<double> *aimPeaks = nullptr,
    std::vector<double> *speedPeaks = nullptr) {
  if (bm.fObjects.size() < 2) {
    return {};
  }
  const double rate = clockRate(mods);

  std::vector<OsuDifficultyHitObject> objs;
  objs.reserve(bm.fObjects.size());
  for (std::size_t i = 1; i < bm.fObjects.size(); ++i) {
    objs.push_back(OsuDifficultyHitObject(bm.fObjects[i], bm.fObjects[i - 1],
                                          rate, objs,
                                          static_cast<int>(objs.size()), bm,
                                          DifficultyMode::kRanked));
  }
  if (objs.empty()) {
    return {};
  }

  const double hitWindow = objs.front().fHW;
  AimSkill aim(true);
  // The same skill again with sliders left out: the ratio of the two is what
  // a performance calculation uses to tell how much of the aim is slider
  // movement, which it nerfs when the sliders were not followed.
  AimSkill aimNoSliders(false);
  SpeedSkill speed(hitWindow);
  for (const auto &o : objs) {
    aim.process(o);
    aimNoSliders.process(o);
    speed.process(o);
  }

  if (aimPeaks != nullptr) {
    *aimPeaks = aim.sectionPeaks();
  }
  if (speedPeaks != nullptr) {
    *speedPeaks = speed.sectionPeaks();
  }

  const double aimValue = aim.difficulty();
  const double speedValue = speed.difficulty();

  const double approachRate =
      approachRateFrom(osu::preemptTime(bm.fDiff.fAr) / rate);
  const double overallDifficulty = objs.front().fODv;
  const auto totalHits = static_cast<int>(bm.fObjects.size());
  const double arLength = lengthBonus(totalHits);

  double aimRating = std::sqrt(aimValue) * kDifficultyMultiplier;
  {
    const double arFactor = approachRate > 10.33
                                ? 0.3 * (approachRate - 10.33)
                                : (approachRate < 8.0
                                       ? 0.05 * (8.0 - approachRate)
                                       : 0.0);
    double multiplier = 1.0 + arFactor * arLength;
    multiplier *= 0.98 + std::pow(std::max(0.0, overallDifficulty), 2.0) / 2500.0;
    aimRating *= std::cbrt(multiplier);
  }

  double speedRating = std::sqrt(speedValue) * kDifficultyMultiplier;
  {
    const double arFactor =
        approachRate > 10.33 ? 0.3 * (approachRate - 10.33) : 0.0;
    double multiplier = 1.0 + arFactor * arLength;
    multiplier *= 0.95 + std::pow(std::max(0.0, overallDifficulty), 2.0) / 750.0;
    speedRating *= std::cbrt(multiplier);
  }

  const double basePerformance =
      diffutil::norm(1.1, difficultyToPerformance(aimRating),
                     difficultyToPerformance(speedRating));
  const double total =
      basePerformance <= 0.00001
          ? 0.0
          : std::cbrt(kPerformanceBaseMultiplier) * kStarRatingMultiplier *
                (std::cbrt(100000.0 / std::pow(2.0, 1.0 / 1.1) *
                           basePerformance) +
                 4.0);

  StarRating out;
  out.fAim = aimRating;
  out.fSpeed = speedRating;
  out.fTotal = total;
  // Ratios and counts, taken before the rating multipliers so that they say
  // what they mean: sliderFactor is a property of the aim skill, not of the
  // approach rate.
  const double aimNoSlidersValue = aimNoSliders.difficulty();
  const double plainAim = std::sqrt(aimValue) * kDifficultyMultiplier;
  const double plainAimNoSliders =
      std::sqrt(aimNoSlidersValue) * kDifficultyMultiplier;
  out.fSliderFactor = plainAim > 0.0 ? plainAimNoSliders / plainAim : 1.0;
  out.fSpeedNoteCount = speed.relevantNoteCount();
  // fAimDifficultSliders is not measured yet: it needs the per-slider strains
  // kept separately, which is its own piece of work. Left at zero, which is
  // the value that turns the slider nerf off rather than a wrong one that
  // applies it -- the branch that uses it requires a positive count.
  out.fAimDifficultSliders = 0.0;
  out.fAimDifficultStrains =
      countTopWeightedStrains(aim.objectStrains(), aimValue);
  out.fSpeedDifficultStrains =
      countTopWeightedStrains(speed.objectStrains(), speedValue);
  out.fGreatWindow = windowGreat(bm.fDiff.fOd) / rate;
  out.fOkWindow = windowGood(bm.fDiff.fOd) / rate;
  out.fMehWindow = windowMeh(bm.fDiff.fOd) / rate;
  for (const auto &obj : bm.fObjects) {
    std::visit(Overloaded{
                   [&out](const Circle &) { ++out.fCircles; },
                   [&out](const Slider &) { ++out.fSliders; },
                   [&out](const Spinner &) { ++out.fSpinners; },
               },
               obj);
  }
  // fMaxCombo is left alone: the combo a map can reach depends on the ticks
  // and tails the engine builds, and building them here to count them would
  // be a second implementation of the same thing. Whoever holds an Engine
  // fills it in.
  return out;
}

} // namespace ranked
} // namespace stars
} // namespace osu
