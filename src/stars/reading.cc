export module osu.stars:reading;

import :core;
import std;
import osu.types;

export namespace osu {
namespace stars {

class ReadingEvaluator {
  static constexpr double kRW = 3000.0;
  static constexpr double kDIT = kND * 1.5;

public:
  static double eval(const OsuDifficultyHitObject &c) {
    if (std::holds_alternative<Spinner>(*c.fBase) || c.fIdx == 0)
      return 0;
    const auto *nextObj = c.next(0);

    double velocity = std::max(1.0, c.fLJD / c.fADT);

    double currentDensity = retrieveCurrentVisibleObjectDensity(c);
    double pastInfluence = getPastObjectDifficultyInfluence(c);
    double angleNerf = getConstantAngleNerfFactor(c);

    double densityDiff = calculateDensityDifficulty(
        nextObj, velocity, angleNerf, pastInfluence, currentDensity);
    double preemptDiff =
        calculatePreemptDifficulty(velocity, angleNerf, c.fPreempt);

    double rd = diffutil::norm(1.5, preemptDiff, 0.0, densityDiff);
    if (!std::isfinite(rd))
      return 0.0;
    rd *= hbb(c.fADT);
    return rd;
  }

private:
  static double
  calculateDensityDifficulty(const OsuDifficultyHitObject *nextObj,
                             double velocity, double angleNerf,
                             double pastInfluence, double currentDensity) {
    double futureInfluence = std::sqrt(currentDensity);
    if (nextObj)
      futureInfluence *= diffutil::smootherstep(nextObj->fLJD, 15.0, kDIT);
    double ndd = std::pow(pastInfluence + futureInfluence, 1.7) * 0.4 *
                 angleNerf * velocity;
    ndd = std::max(0.0, ndd - 2.5);
    ndd = std::pow(ndd, 0.45) * 2.4;
    return ndd;
  }

  static double calculatePreemptDifficulty(double velocity, double angleNerf,
                                           double preempt) {
    constexpr double kPF = 140000.0;
    constexpr double kPS = 500.0;
    double pd =
        std::pow((kPS - preempt + std::abs(preempt - kPS)) / 2.0, 2.5) / kPF;
    return pd * angleNerf * velocity;
  }

  static double
  getPastObjectDifficultyInfluence(const OsuDifficultyHitObject &c) {
    double influence = 0;
    for (int i = 0; i < c.fIdx; ++i) {
      const auto *p = c.prev(i);
      if (!p || c.fStart - p->fStart > kRW || p->fStart < c.fStart - c.fPreempt)
        break;
      double d = c.opacityAt(startTime(*p->fBase)) *
                 diffutil::smootherstep(p->fLJD, 15.0, kDIT);
      double dt = c.fStart - p->fStart;
      d *= timeNerfFactor(dt);
      influence += d;
    }
    return influence;
  }

  static double
  retrieveCurrentVisibleObjectDensity(const OsuDifficultyHitObject &c) {
    double count = 0;
    const OsuDifficultyHitObject *ho = c.next(0);
    while (ho) {
      if (ho->fStart - c.fStart > kRW || c.fStart < ho->fStart - ho->fPreempt)
        break;
      double dt = ho->fStart - c.fStart;
      count += ho->opacityAt(startTime(*c.fBase)) * timeNerfFactor(dt);
      ho = ho->next(0);
    }
    return count;
  }

  static double getConstantAngleNerfFactor(const OsuDifficultyHitObject &c) {
    constexpr double kAMT = 2000.0;
    constexpr double kAMi = 200.0;

    double count = 0;
    int idx = 0;
    double timeGap = 0;
    const OsuDifficultyHitObject *lp0 = &c;
    const OsuDifficultyHitObject *lp1 = nullptr;
    const OsuDifficultyHitObject *lp2 = nullptr;

    while (timeGap < kAMT) {
      const auto *lp = c.prev(idx);
      if (!lp)
        break;
      double longInterval = 1.0 - diffutil::reverseLerp(lp->fADT, kAMi, kAMT);

      if (lp->fA && c.fA) {
        double aDiff = std::abs(*c.fA - *lp->fA);
        double aAlt = std::acos(-1.0);

        if (lp0->fA && lp1 && lp1->fA && lp2 && lp2->fA) {
          aAlt = std::abs(lp1->fA.value() - lp->fA.value());
          aAlt += std::abs(lp2->fA.value() - lp0->fA.value());

          double w = 1.0;
          w *= diffutil::reverseLerp(std::min(lp->fA.value(), lp0->fA.value()) *
                                         180.0 / std::acos(-1.0),
                                     20.0, 5.0);
          w *= diffutil::reverseLerp(std::max(lp->fA.value(), lp0->fA.value()) *
                                         180.0 / std::acos(-1.0),
                                     60.0, 120.0);
          double lerpFactor = std::isfinite(w) ? w : 0.0;
          aAlt = std::acos(-1.0) * (1.0 - lerpFactor) + 0.1 * aAlt * lerpFactor;
        }

        double stackF = diffutil::smootherstep(lp->fLJD, 0.0, kNR);
        double minAngle = std::min(aDiff, aAlt);
        if (!std::isfinite(minAngle))
          minAngle = 0.0;
        count += std::cos(3.0 * std::min(std::acos(-1.0) * 30.0 / 180.0,
                                         minAngle * stackF)) *
                 longInterval;
      }

      timeGap = c.fStart - lp->fStart;
      ++idx;
      lp2 = lp1;
      lp1 = lp0;
      lp0 = lp;
    }
    if (!std::isfinite(count) || count <= 0.0)
      return 1.0;
    return std::clamp(2.0 / count, 0.2, 1.0);
  }

  static double timeNerfFactor(double dt) {
    return std::clamp(2.0 - dt / (kRW / 2.0), 0.0, 1.0);
  }

  static double hbb(double ms) {
    return 1.0 / (1.0 - std::pow(0.8, ms / 1000.0));
  }
};

class ReadingSkill {
public:
  void process(const OsuDifficultyHitObject &c) {
    // Spinners are processed like anything else -- the evaluator returns zero
    // for them, but the strain decays across them and they count towards the
    // reduced-note window.
    fObjList.push_back({c.fStart, c.fADT});
    double d = std::pow(0.8, c.fRawDT / 1000.0);
    fCur *= d;
    double rawReading = ReadingEvaluator::eval(c);
    rawReading *= 0.825 + std::pow(std::max(0.0, c.fODv), 2.2) / 1125.0;
    fCur += rawReading * (1.0 - d) * kSM;
    fDiffs.push_back(fCur);
  }

  [[nodiscard]] double difficulty() const {
    if (fDiffs.empty())
      return 0;
    auto ds = fDiffs;
    ds = getTransformedDifficulties(ds);
    std::ranges::sort(ds, std::greater<>{});
    double diff = 0;
    int i = 0;
    fWeightSum = 0;
    for (double v : ds) {
      if (v <= 0)
        break;
      double w =
          (1.0 + 1.0 / (1.0 + i)) /
          (std::pow(static_cast<double>(i), 0.9) + 1.0 + 1.0 / (1.0 + i));
      fWeightSum += w;
      diff += v * w;
      ++i;
    }
    return diff;
  }

  [[nodiscard]] double weightSum() const { return fWeightSum; }
  [[nodiscard]] double reducedNoteCount() const {
    return static_cast<double>(this->calculateReducedNoteCount());
  }

private:
  static constexpr double kSM = 2.5;

  std::vector<std::pair<double, double>> fObjList;
  std::vector<double> fDiffs;
  double fCur = 0;
  mutable double fWeightSum = 0;

  std::vector<double>
  getTransformedDifficulties(std::vector<double> &ds) const {
    std::vector<double> result;
    for (double v : ds)
      if (v > 0)
        result.push_back(v);
    // The ramp divides by the note count of the first minute, not by however
    // many of those notes ended up with a difficulty above zero: lazer takes
    // the smaller of the two only for how many notes to scale, and divides by
    // reducedNoteCount throughout.
    const double noteCount = static_cast<double>(calculateReducedNoteCount());
    const int reducedCount =
        static_cast<int>(std::min<double>(static_cast<double>(result.size()),
                                          noteCount));
    for (int i = 0; i < reducedCount; ++i) {
      double scale = std::log10(
          std::clamp(static_cast<double>(i) / noteCount, 0.0, 1.0) * 9.0 + 1.0);
      result[i] *= scale; // lerp(0, 1, scale) = scale
    }
    return result;
  }

  std::size_t calculateReducedNoteCount() const {
    if (fObjList.empty())
      return 0;
    double reducedDur = fObjList.front().first + 60000.0;
    std::size_t count = 0;
    for (auto &[t, _] : fObjList)
      if (t <= reducedDur)
        ++count;
      else
        break;
    return count;
  }
};

} // namespace stars
} // namespace osu
