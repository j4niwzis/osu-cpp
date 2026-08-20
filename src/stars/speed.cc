export module osu.stars:speed;

import :core;
import :rhythm;
import std;
import osu.types;

export namespace osu {
namespace stars {

class AgilityEvaluator {
public:
  static double eval(const OsuDifficultyHitObject &c) {
    if (std::holds_alternative<Spinner>(*c.fBase))
      return 0;
    double cap = kND * 1.2;
    const auto *p = c.fIdx > 0 ? c.prev(0) : nullptr;
    double dd = std::min((p ? p->fLTD : 0.0) + c.fLJD, cap) / cap;
    double a = dd * 1000.0 / c.fADT;
    a *= std::pow(c.fSCB, 1.5) * (1.0 / (1.0 - std::pow(0.2, c.fADT / 1000.0)));
    return a;
  }
};

class SpeedEvaluator {
public:
  static double eval(const OsuDifficultyHitObject &c) {
    if (std::holds_alternative<Spinner>(*c.fBase))
      return 0;
    double st = c.fADT;
    double df = 1.0 - c.calculateDoubleTapFeasibility(c.next(0));
    st /= std::clamp((st / c.fHW) / 0.93, 0.92, 1.0);
    double sb =
        diffutil::msToBpm(st) > 200.0
            ? 0.75 * std::pow((diffutil::bpmToMs(200.0) - st) / 40.0, 2.0)
            : 0.0;
    double sp = (1.0 + sb) * 1000.0 / st;
    sp *= 1.0 / (1.0 - std::pow(0.3, c.fADT / 1000.0));
    return sp * df;
  }
};

class SpeedSkill {
public:
  void process(const OsuDifficultyHitObject &c) {
    // A spinner is processed like anything else: its evaluators return zero,
    // but the strain still decays across it and the object still counts.
    double d = std::pow(0.3, c.fADT / 1000.0);
    fCur *= d;
    fCur += SpeedEvaluator::eval(c) * (1.0 - d) * 1.16;
    double total = fCur * RhEvaluator::eval(c);
    fDiffs.push_back(total);
  }

  [[nodiscard]] double difficulty() const {
    if (fDiffs.empty())
      return 0;
    auto ds = fDiffs;
    std::ranges::sort(ds, std::greater<>{});
    double diff = 0;
    int i = 0;
    fWeightSum = 0;
    for (double v : ds) {
      if (v <= 0)
        break;
      double w =
          (1.0 + 20.0 / (1.0 + i)) /
          (std::pow(static_cast<double>(i), 0.9) + 1.0 + 20.0 / (1.0 + i));
      fWeightSum += w;
      diff += v * w;
      ++i;
    }
    return diff;
  }

  [[nodiscard]] double weightSum() const { return fWeightSum; }
  [[nodiscard]] int noteCount() const {
    return static_cast<int>(fDiffs.size());
  }

private:
  std::vector<double> fDiffs;
  double fCur = 0;
  mutable double fWeightSum = 0;
};

} // namespace stars
} // namespace osu
