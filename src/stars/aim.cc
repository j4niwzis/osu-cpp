export module osu.stars:aim;

import :core;
import :speed;
import std;
import osu.types;
import osu.curves;

export namespace osu {
namespace stars {

class SnapAimEvaluator {
public:
  static double Eval(const OsuDifficultyHitObject &c, bool wst) {
    if (std::holds_alternative<Spinner>(*c.fBase) || c.fIdx <= 1 ||
        std::holds_alternative<Spinner>(*c.prev(0)->fBase))
      return 0;
    constexpr double kW = 9.67, kA = 2.41, kS = 1.5, kV = 0.9, kWM = 1.02;
    const auto &pr = *c.prev(0);
    const auto *pll = c.prev(2);
    double cd = wst ? c.fLJD : c.fJD;
    double cv = cd / c.fADT;
    if (std::holds_alternative<Slider>(*pr.fBase) && wst) {
      double sd = pr.fLTD + c.fLJD;
      cv = std::max(cv, sd / c.fADT);
    }
    double pd = wst ? pr.fLJD : pr.fJD;
    double pv = pd / pr.fADT;
    double s = cv;
    s *= var(c, pr);
    if (c.fA && pr.fA) {
      double ca = *c.fA, pa = *pr.fA;
      double vi = std::min(cv, pv);
      double ab = 0;
      // If rhythms are the same
      if (std::max(c.fADT, pr.fADT) < 1.25 * std::min(c.fADT, pr.fADT)) {
        ab = acu(ca);
        ab *= 0.08 + 0.92 * (1.0 - std::min(ab, std::pow(acu(pa), 3)));
        ab *=
            vi *
            diffutil::smootherstep(diffutil::msToBpm(c.fADT, 2), 300.0, 400.0) *
            diffutil::smootherstep(cd, 0.0, kND * 2.0);
      }
      double wb = wid(ca);
      wb *= 0.25 + 0.75 * (1.0 - std::min(wb, std::pow(wid(pa), 3)));
      constexpr double kT = 1.45;
      double wcv = cd / std::pow(c.fADT, kT);
      if (std::holds_alternative<Slider>(*pr.fBase) && wst) {
        double sliderDist = pr.fLTD + c.fLJD;
        wcv = std::max(wcv, sliderDist / std::pow(c.fADT, kT));
      }
      double wpv = pd / std::pow(pr.fADT, kT);
      wb *= std::min(wcv, wpv);

      // Back-and-forth nerf: if last2 and last objects are at the same position
      if (pll) {
        Vec2 lPos = OsuDifficultyHitObject::sp(*pr.fBase, pr.fCsR);
        Vec2 l2Pos = OsuDifficultyHitObject::sp(*pll->fBase, pll->fCsR);
        double dist = (l2Pos - lPos).length();
        if (dist < 1.0)
          wb *= 1.0 - 0.55 * (1.0 - dist);
      }

      s += std::max(ab * kA, wb * kW);

      double wgl = vi * diffutil::smootherstep(cd, kNR, kND) *
                   std::pow(diffutil::reverseLerp(cd, kND * 3.0, kND), 1.8) *
                   diffutil::smootherstep(ca, std::acos(-1.0) * 110.0 / 180.0,
                                          std::acos(-1.0) * 60.0 / 180.0) *
                   diffutil::smootherstep(pd, kNR, kND) *
                   std::pow(diffutil::reverseLerp(pd, kND * 3.0, kND), 1.8) *
                   diffutil::smootherstep(pa, std::acos(-1.0) * 110.0 / 180.0,
                                          std::acos(-1.0) * 60.0 / 180.0);
      s += wgl * kWM;
    }
    if (std::max(pv, cv) != 0) {
      if (wst)
        cv = cd / c.fADT;
      double dr =
          diffutil::smoothstep(std::abs(pv - cv) / std::max(pv, cv), 0.0, 1.0);
      double ob =
          std::min(kND * 1.25 / std::min(c.fADT, pr.fADT), std::abs(pv - cv));
      double vc = ob * dr;
      vc *=
          std::pow(std::min(c.fADT, pr.fADT) / std::max(c.fADT, pr.fADT), 2.0);
      s += vc * kV;
    }
    if (std::holds_alternative<Slider>(*c.fBase) && wst) {
      double sb = c.fTD / c.fTT;
      s += (sb < 1.0 ? sb : std::pow(sb, 0.75)) * kS;
    }
    s *= c.fSCB * hbb(c.fADT);
    return s;
  }
  static double acu(double a) {
    return diffutil::smoothstep(a, std::acos(-1.0) * 140.0 / 180.0,
                                std::acos(-1.0) * 40.0 / 180.0);
  }

private:
  static double hbb(double ms) {
    return 1.0 / (1.0 - std::pow(0.03, std::pow(ms / 1000.0, 0.65)));
  }
  static double wid(double a) {
    return diffutil::smoothstep(a, std::acos(-1.0) * 40.0 / 180.0,
                                std::acos(-1.0) * 140.0 / 180.0);
  }
  static double var(const OsuDifficultyHitObject &c,
                    const OsuDifficultyHitObject &p) {
    if (!c.fA || !p.fA)
      return 1.0;
    constexpr double kMN = 0.15, kMV = 0.5;
    double cc = 0;
    for (int i = 0; i < 6; ++i) {
      const auto *pp = c.prev(i);
      if (!pp)
        break;
      if (std::max(c.fADT, pp->fADT) > 1.1 * std::min(c.fADT, pp->fADT))
        break;
      if (pp->fNVA && c.fNVA) {
        double d = std::abs(*c.fNVA - *pp->fNVA);
        cc += std::cos(8.0 * std::min(std::acos(-1.0) * 11.25 / 180.0, d));
      }
    }
    if (cc <= 0)
      return 1.0;
    double r = std::pow(std::min(0.5 / cc, 1.0), 2.0);
    double st = diffutil::smootherstep(c.fLJD, 0.0, kND);
    double ad = std::cos(2.0 * std::min(std::acos(-1.0) * 45.0 / 180.0,
                                        std::abs(*c.fA - *p.fA) * st));
    double bn = 1.0 - kMN * acu(*p.fA) * ad;
    return std::pow(bn + (1.0 - bn) * r * kMV * st, 2.0);
  }
};

class FlowAimEvaluator {
public:
  static double Eval(const OsuDifficultyHitObject &c, bool wst) {
    if (std::holds_alternative<Spinner>(*c.fBase) || c.fIdx <= 1 ||
        std::holds_alternative<Spinner>(*c.prev(0)->fBase))
      return 0;
    constexpr double kV = 0.52;
    const auto &pr = *c.prev(0);
    const auto *pll = c.prev(1);
    double cd = wst ? c.fLJD : c.fJD;
    double pd = wst ? pr.fLJD : pr.fJD;
    double cv = cd / c.fADT;
    if (std::holds_alternative<Slider>(*pr.fBase) && wst) {
      double sd = pr.fLTD + c.fLJD;
      cv = std::max(cv, sd / c.fADT);
    }
    double pv = pd / pr.fADT;
    double fl = cv * std::sqrt(c.fSCB);
    fl *= 1.0 + std::min(0.25, std::pow((std::max(c.fADT, pr.fADT) -
                                         std::min(c.fADT, pr.fADT)) /
                                            50.0,
                                        4));
    if (c.fA && pr.fA) {
      double ad = std::sin(std::abs(*c.fA - *pr.fA) / 2.0) * 180.0;
      fl *= 0.8 + std::sqrt(ad / (c.fADT * 0.1) / 270.0);
    }
    double overlappedNotesWeight = 1.0;
    if (c.fIdx > 2 && pll) {
      double o1 = calcOverlap(c, pr);
      double o2 = calcOverlap(c, *pll);
      double o3 = calcOverlap(pr, *pll);
      overlappedNotesWeight = 1.0 - o1 * o2 * o3;
    }
    if (c.fA)
      fl += cv * SnapAimEvaluator::acu(*c.fA) * overlappedNotesWeight;
    if (std::max(pv, cv) != 0) {
      if (wst)
        cv = cd / c.fADT;
      fl +=
          std::min(kND * 1.25 / std::min(c.fADT, pr.fADT), std::abs(pv - cv)) *
          diffutil::smoothstep(std::abs(pv - cv) / std::max(pv, cv), 0.0, 1.0) *
          overlappedNotesWeight * kV;
    }
    if (std::holds_alternative<Slider>(*c.fBase) && wst)
      fl += c.fTD / c.fTT;
    fl = std::pow(fl, 1.45);
    return fl * diffutil::smootherstep(cd, 0.0, kNR);
  }

private:
  static double calcOverlap(const OsuDifficultyHitObject &a,
                            const OsuDifficultyHitObject &b) {
    Vec2 pa = OsuDifficultyHitObject::sp(*a.fBase, a.fCsR);
    Vec2 pb = OsuDifficultyHitObject::sp(*b.fBase, b.fCsR);
    double r = a.fRad;
    double d = (pa - pb).length();
    return std::clamp(1.0 - std::pow(std::max(d - r, 0.0) / r, 2.0), 0.0, 1.0);
  }
};

// --- Skills ---

class AimSkill {
public:
  static constexpr double kDW = 0.9;
  static constexpr int kSL = 400;

  bool fInc;
  double fCur = 0;
  struct Pk {
    double v, l;
  };
  struct Q {
    double v, t;
  };
  double fSecP = 0, fSecB = 0, fSecE = 0;
  std::vector<Pk> fPks;
  std::vector<Q> fQ;
  double fLenSum = 0;
  int fObjCount = 0;
  std::vector<TracePoint> fTrace;

  AimSkill(bool inc) : fInc(inc) {}

  // Per-object strain, kept for diagnostics: this is the series the section
  // machinery is fed, and comparing it against lazer's is the only way to
  // tell an evaluator's mistake from an aggregation one.
  struct TracePoint {
    double fTime = 0.0;
    double fSnap = 0.0;
    double fAgility = 0.0;
    double fFlow = 0.0;
    double fStrain = 0.0;
  };
  [[nodiscard]] std::span<const TracePoint> trace() const { return fTrace; }

  void process(const OsuDifficultyHitObject &c) {
    ++fObjCount;
    if (c.fIdx == 0) {
      fSecB = c.fStart;
      fSecE = fSecB + kSL;
      fSecP = strainOf(c);
      this->traceLast(c, fSecP);
      return;
    }
    backfill(c);
    double s = strainOf(c);
    this->traceLast(c, s);
    if (s > fSecP) {
      fQ.clear();
      // The section that just ended is as long as the time spent in it, not
      // a whole nominal section: saveCurrentPeak(StartTime - sectionBegin).
      // Handing it the full length stretched every peak that a higher strain
      // cut short, and since DifficultyValue weights a peak by its length,
      // the aim value came out over.
      save(c.fStart - fSecB);
      fSecB = c.fStart;
      fSecE = fSecB + kSL;
      fSecP = s;
    } else {
      while (!fQ.empty() && fQ.back().v < s)
        fQ.pop_back();
      fQ.push_back({s, c.fStart});
    }
  }

  [[nodiscard]] double difficulty() const {
    double d = 0, t = 0;
    std::vector<Pk> peakList = reduced();
    for (auto &p : peakList) {
      double st = t;
      double et = t + p.l / kSL;
      double w = std::pow(kDW, st) - std::pow(kDW, et);
      d += p.v * w;
      t = et;
    }
    return d / (1.0 - kDW);
  }

  // getReducedStrainPeaks. The awkward part is that lazer walks the very list
  // it is appending to: the chunks it emits for a peak land at the end of
  // that same list, and when the original peaks run out before the first four
  // seconds are accounted for, it starts chunking its own chunks. That only
  // happens on maps with a handful of objects -- but there it doubles the
  // answer, and this is meant to be the same calculator lazer is.
  std::vector<Pk> reduced() const {
    constexpr int kChunk = 20;
    constexpr double kReducedTime = 4000.0;
    constexpr double kBase = 0.727;

    std::vector<Pk> strains = fPks; // kept sorted by value as they are saved
    strains.push_back({fSecP, std::round(fSecE - fSecB)});
    std::erase_if(strains, [](const Pk &p) { return p.v <= 0; });
    std::ranges::sort(strains,
                      [](const Pk &a, const Pk &b) { return a.v > b.v; });

    double time = 0.0;
    std::size_t skip = 0;
    while (strains.size() > skip && time < kReducedTime) {
      const Pk strain = strains[skip]; // by value: the vector grows below
      for (double added = 0; added < strain.l; added += kChunk) {
        const double scale = std::log10(
            std::clamp((time + added) / kReducedTime, 0.0, 1.0) * 9.0 + 1.0);
        strains.push_back(
            {strain.v * (kBase + (1.0 - kBase) * scale),
             std::round(std::min(static_cast<double>(kChunk), strain.l - added))});
      }
      time += strain.l;
      ++skip;
    }

    std::vector<Pk> result(strains.begin() + static_cast<std::ptrdiff_t>(skip),
                           strains.end());
    std::ranges::sort(result,
                      [](const Pk &a, const Pk &b) { return a.v > b.v; });
    return result;
  }

private:
  void traceLast(const OsuDifficultyHitObject &c, double strain) {
    fTrace.push_back({c.fStart, SnapAimEvaluator::Eval(c, fInc),
                      AgilityEvaluator::eval(c),
                      FlowAimEvaluator::Eval(c, fInc), strain});
  }

  double strainOf(const OsuDifficultyHitObject &c) {
    double d = std::pow(0.2, c.fADT / 1000.0);
    fCur *= d;
    fCur += aim(c) * (1.0 - d);
    return fCur;
  }
  double aim(const OsuDifficultyHitObject &c) {
    double snap = SnapAimEvaluator::Eval(c, fInc) * 70.9;
    double ag = AgilityEvaluator::eval(c) * 2.35;
    double flow = FlowAimEvaluator::Eval(c, fInc) * 242.0;
    double cs = diffutil::norm(1.2, snap, ag);
    double ps = prob(flow / std::max(cs, 1e-10));
    double total = cs * ps + flow * (1.0 - ps);
    total *= 1.12;
    total *= 0.985 + std::pow(std::max(0.0, c.fODv), 2.0) / 4000.0;
    return total;
  }
  static double prob(double r) {
    if (r <= 0)
      return 0;
    if (std::isnan(r))
      return 1;
    return diffutil::logistic(-7.27 * std::log(r));
  }

  // VariableLengthStrainSkill.saveCurrentPeak: peaks are kept sorted by
  // value, and the list is capped -- once the sections stored add up to more
  // than 11/(1-decay) sections' worth of time, the weakest are dropped. On
  // anything longer than about three quarters of a minute this changes the
  // answer, so it is not optional.
  void save(double l) {
    const Pk peak{fSecP, std::round(l)};
    auto pos = std::ranges::upper_bound(fPks, peak.v, std::greater<>{},
                                        [](const Pk &p) { return p.v; });
    fPks.insert(pos, peak);
    fLenSum += peak.l;
    constexpr double kMaxStored = 11.0 / (1.0 - kDW);
    while (fLenSum > kMaxStored * kSL && !fPks.empty()) {
      fLenSum -= fPks.back().l;
      fPks.pop_back();
    }
  }
  void backfill(const OsuDifficultyHitObject &c) {
    while (c.fStart > fSecE) {
      save(fSecE - fSecB);
      fSecB = fSecE;
      if (!fQ.empty()) {
        auto [v, t] = fQ.front();
        fQ.erase(fQ.begin());
        fSecE = t + kSL;
        fSecP = std::max(
            fCur * std::pow(0.2, (fSecB - c.prev(0)->fStart) / 1000.0), v);
      } else {
        fSecE = fSecB + kSL;
        fSecP = fCur * std::pow(0.2, (fSecB - c.prev(0)->fStart) / 1000.0);
      }
    }
  }
};

} // namespace stars
} // namespace osu
