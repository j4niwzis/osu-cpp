export module osu.stars;

import std;
import osu.types;
import osu.rules;
import osu.curves;
import osu.beatmap;

export namespace osu {

struct StarRating {
  double fAim = 0.0;
  double fSpeed = 0.0;
  double fTotal = 0.0;
};

namespace diffutil {
inline double logistic(double ex, double maxValue = 1.0) {
  return maxValue / (1.0 + std::exp(ex));
}
inline double logistic(double x, double mid, double mult,
                       double maxValue = 1.0) {
  return maxValue / (1.0 + std::exp(mult * (mid - x)));
}
inline double norm(double p, double a, double b, double c = 0.0) {
  double sum = std::pow(a, p) + std::pow(b, p);
  if (c > 0.0)
    sum += std::pow(c, p);
  return std::pow(sum, 1.0 / p);
}
inline double smoothstep(double x, double a, double b) {
  x = std::clamp((x - a) / (b - a), 0.0, 1.0);
  return x * x * (3.0 - 2.0 * x);
}
inline double smootherstep(double x, double a, double b) {
  x = std::clamp((x - a) / (b - a), 0.0, 1.0);
  return x * x * x * (x * (6.0 * x - 15.0) + 10.0);
}
inline double reverseLerp(double x, double a, double b) {
  return std::clamp((x - a) / (b - a), 0.0, 1.0);
}
inline double bpmToMs(double bpm, int del = 4) { return 60000.0 / del / bpm; }
inline double msToBpm(double ms, int del = 4) { return 60000.0 / (ms * del); }
inline double smoothstepBellCurve(double x) {
  x = 0.5 - std::abs(x - 0.5);
  x = std::clamp(x * 2.0, 0.0, 1.0);
  return x * x * (3.0 - 2.0 * x);
}
} // namespace diffutil

inline double hitWindowGreat(double od) noexcept {
  return 2.0 * (80.0 - 6.0 * od);
}
[[nodiscard]] constexpr double lazerCircleRadius(double cs) noexcept {
  return 54.4 - 4.48 * cs;
}

namespace stars {

inline constexpr int kNR = 50;
inline constexpr int kND = kNR * 2;
inline constexpr int kMDT = 25;
inline constexpr float kMSR = static_cast<float>(kNR) * 2.4f;
inline constexpr float kASR = static_cast<float>(kNR) * 1.8f;

inline double clockRate(ModSet mods) {
  if (hasMod(mods, mod::kDoubleTime))
    return 1.5;
  if (hasMod(mods, mod::kHalfTime))
    return 0.75;
  return 1.0;
}

class OsuDifficultyHitObject {
public:
  const std::vector<OsuDifficultyHitObject> *fAll = nullptr;
  int fIdx = 0;
  const HitObject *fBase = nullptr;
  const HitObject *fLast = nullptr;
  double fRawDT = 0.0;
  double fStart = 0.0, fEnd = 0.0;
  double fRate = 1.0;
  double fHW = 0.0;
  double fADT = 25.0;
  double fLEDT = 25.0;
  double fPreempt = 0.0;
  double fJD = 0.0, fLJD = 0.0, fMJD = 0.0;
  double fMJT = kMDT;
  double fTD = 0.0, fTT = 0.0;
  Vec2 fLEP{};
  double fLTD = 0.0, fLTT = 0.0;
  std::optional<double> fA, fNVA;
  double fSCB = 1.0, fODv = 8.0, fCsR = 5.0;
  bool fHasLE = false;
  double fRad = 0.0;
  Vec2 fSliderSecondLastNestedPos{};

  OsuDifficultyHitObject(const HitObject &ho, const HitObject &lo, double cr,
                         const std::vector<OsuDifficultyHitObject> &all,
                         int idx, const Beatmap &bm) {
    fAll = &all;
    fIdx = idx;
    fBase = &ho;
    fLast = &lo;
    fRawDT = (startTime(ho) - startTime(lo)) / cr;
    fADT = std::max(fRawDT, static_cast<double>(kMDT));
    fStart = startTime(ho) / cr;
    fEnd = objectEndTime(ho, bm) / cr;
    fRate = cr;
    fRad = lazerCircleRadius(bm.fDiff.fCs);
    fHW = hitWindowGreat(bm.fDiff.fOd) / cr;
    const auto *p = prev();
    if (p)
      fLEDT = std::max(fStart - p->fEnd, static_cast<double>(kMDT));
    else
      fLEDT = fADT;
    fPreempt = osu::preemptTime(bm.fDiff.fAr) / cr;
    fSCB = std::max(1.0, 1.0 + (30.0 - fRad) / 70.0);
    fODv = (79.5 - fHW / 2.0) / 6.0;
    fCsR = bm.fDiff.fCs;
    computeSlider(bm);
    setDistances(cr, bm);
  }

  [[nodiscard]] const OsuDifficultyHitObject *prev(int skip = 0) const {
    int id = fIdx - (skip + 1);
    if (id >= 0 && fAll && id < static_cast<int>(fAll->size()))
      return &(*fAll)[static_cast<std::size_t>(id)];
    return nullptr;
  }
  [[nodiscard]] const OsuDifficultyHitObject *next(int skip = 0) const {
    int id = fIdx + (skip + 1);
    if (fAll && id >= 0 && id < static_cast<int>(fAll->size()))
      return &(*fAll)[static_cast<std::size_t>(id)];
    return nullptr;
  }
  [[nodiscard]] static Vec2 sp(const HitObject &o, double cs) {
    return std::visit(
        Overloaded{
            [cs](const Circle &c) { return osu::stackedPosition(c, cs); },
            [cs](const Slider &s) { return osu::stackedPosition(s, cs); },
            [](const Spinner &) -> Vec2 { return kPlayfieldCenter; },
        },
        o);
  }

  double
  calculateDoubleTapFeasibility(const OsuDifficultyHitObject *nxt) const {
    if (!nxt)
      return 0;
    double a = std::max(1.0, fRawDT);
    double b = std::max(1.0, nxt->fRawDT);
    double dd = std::abs(b - a);
    double sr = a / std::max(a, dd);
    double wr = std::pow(std::min(1.0, a / fHW), 5.0);
    double df = std::pow(diffutil::reverseLerp(fLJD, kND, kNR), 2.0);
    return 1.0 - std::pow(sr, df * (1.0 - wr));
  }

  [[nodiscard]] double opacityAt(double rawTime, bool hidden = false) const {
    double rawStart = startTime(*fBase);
    if (rawTime > rawStart)
      return 0.0;
    double rawPreempt = fPreempt * fRate;
    double fadeInStart = rawStart - rawPreempt;
    double fadeInDuration = 400.0 * std::min(1.0, rawPreempt / 450.0);
    return std::clamp((rawTime - fadeInStart) / fadeInDuration, 0.0, 1.0);
  }

private:
  void computeSlider(const Beatmap &bm) {
    const auto *sl = std::get_if<Slider>(fBase);
    if (!sl)
      return;
    double rawDur =
        bm.sliderSpanDuration(*sl) * static_cast<double>(sl->fRepeat);
    double rawStart = sl->fTime;
    double trackingEndTime =
        std::max(rawStart + rawDur + (-36.0), rawStart + rawDur / 2.0);
    fLTT = trackingEndTime - rawStart;
    double spanDur = bm.sliderSpanDuration(*sl);
    double endTimeMin = fLTT / spanDur;
    if (!std::isfinite(endTimeMin))
      endTimeMin = 0.0;
    if (static_cast<int>(endTimeMin) % 2 >= 1)
      endTimeMin = 1.0 - std::fmod(endTimeMin, 1.0);
    else
      endTimeMin = std::fmod(endTimeMin, 1.0);
    SliderPath path(sl->fCurveType, sl->fControl, sl->fPixelLength);
    Vec2 slStacked = stackedPosition(*sl, fCsR);
    Vec2 stackOff = slStacked - sl->fPos;
    fLEP = path.positionAt(endTimeMin * path.length()) + stackOff;
    fHasLE = true;
    Vec2 cur = slStacked;
    double sf = kNR / fRad;

    double vel = bm.sliderVelocityAt(sl->fTime);
    double pathLen = path.length();
    double tickDist = std::clamp(bm.sliderTickDistance(*sl), 0.0, pathLen);
    double minDistEnd = vel * 10.0;

    std::vector<Vec2> nestedPositions;
    nestedPositions.push_back(slStacked);

    for (int span = 0; span < sl->fRepeat; ++span) {
      bool reversed = (span % 2 == 1);
      if (tickDist > 0) {
        for (double d = tickDist; d <= pathLen; d += tickDist) {
          if (d >= pathLen - minDistEnd)
            break;
          double progress = d / pathLen;
          double ratio = reversed ? 1.0 - progress : progress;
          nestedPositions.push_back(path.positionAt(ratio * pathLen) +
                                    stackOff);
        }
      }
      if (span < sl->fRepeat - 1) {
        double repeatProgress = (span + 1) % 2;
        nestedPositions.push_back(path.positionAt(repeatProgress * pathLen) +
                                  stackOff);
      } else {
        double tailProgress = static_cast<double>(sl->fRepeat % 2);
        nestedPositions.push_back(path.positionAt(tailProgress * pathLen) +
                                  stackOff);
      }
    }

    if (nestedPositions.size() >= 2)
      fSliderSecondLastNestedPos = nestedPositions[nestedPositions.size() - 2];

    for (std::size_t i = 1; i < nestedPositions.size(); ++i) {
      Vec2 mv = nestedPositions[i] - cur;
      double ml = sf * mv.length();
      double req = kASR;
      if (i == nestedPositions.size() - 1) {
        Vec2 lm = fLEP - cur;
        if (lm.length() < mv.length())
          mv = lm;
        ml = sf * mv.length();
      }
      if (ml > req) {
        cur = cur + mv * ((ml - req) / ml);
        ml *= (ml - req) / ml;
        fLTD += ml;
      }
    }
    fLEP = cur;
    fHasLE = true;
  }

  void setDistances(double cr, const Beatmap &bm) {
    const auto *cs = std::get_if<Slider>(fBase);
    const auto *ls = std::get_if<Slider>(fLast);
    if (cs) {
      fTD =
          fLTD * std::max(1.0, std::pow(static_cast<double>(cs->fRepeat), 0.3));
      fTT = std::max(fLTT / cr, static_cast<double>(kMDT));
    }
    fMJT = fADT;

    if (std::holds_alternative<Spinner>(*fBase) ||
        std::holds_alternative<Spinner>(*fLast))
      return;

    double sf = kNR / fRad;
    const auto *ld = prev();
    Vec2 lc = ld ? getEndPos(*ld) : sp(*fLast, bm.fDiff.fCs);

    fJD = (sp(*fLast, bm.fDiff.fCs) - sp(*fBase, bm.fDiff.fCs)).length() * sf;
    fLJD = (sp(*fBase, bm.fDiff.fCs) - lc).length() * sf;
    fMJD = fLJD;

    if (ls && ld) {
      double lt = std::max(ld->fLTT / cr, static_cast<double>(kMDT));
      fMJT = std::max(fADT - lt, static_cast<double>(kMDT));

      const SliderPath path(ls->fCurveType, ls->fControl, ls->fPixelLength);
      Vec2 tail = path.positionAt(path.length()) +
                  stackOffset(ls->fStack, bm.fDiff.fCs);
      double tailJump = (tail - sp(*fBase, bm.fDiff.fCs)).length() * sf;
      fMJD = std::max(0.0, std::min(fLJD - (kMSR - kASR), tailJump - kMSR));
    }

    const auto *lld = prev(1);
    if (lld && !std::holds_alternative<Spinner>(*lld->fBase)) {
      Vec2 lastCP = lc;
      if (ld && std::holds_alternative<Slider>(*ld->fBase) && ld->fTD > 0)
        lastCP = sp(*ld->fBase, bm.fDiff.fCs);
      Vec2 last2CP = getEndPos(*lld);
      double angle = calcAngle(sp(*fBase, bm.fDiff.fCs), lastCP, last2CP);
      double sliderA = calcSliderAngle(ld, last2CP, bm);
      Vec2 v = sp(*fBase, bm.fDiff.fCs) - lastCP;
      fNVA = (std::abs(v.fX) > 1e-10 || std::abs(v.fY) > 1e-10)
                 ? std::optional(std::atan2(std::abs(v.fY), std::abs(v.fX)))
                 : std::nullopt;
      fA = std::min(angle, sliderA);
    }
  }

  static Vec2 getEndPos(const OsuDifficultyHitObject &d) {
    return d.fHasLE ? d.fLEP : d.sp(*d.fBase, d.fCsR);
  }
  static double calcAngle(Vec2 cur, Vec2 last, Vec2 lastLast) {
    Vec2 v1 = lastLast - last;
    Vec2 v2 = cur - last;
    return std::abs(std::atan2(v1.fX * v2.fY - v1.fY * v2.fX,
                               v1.fX * v2.fX + v1.fY * v2.fY));
  }
  static double calcSliderAngle(const OsuDifficultyHitObject *ld, Vec2 last2CP,
                                const Beatmap &bm) {
    Vec2 lastCP = getEndPos(*ld);
    if (ld && std::holds_alternative<Slider>(*ld->fBase) && ld->fTD > 0)
      last2CP = ld->fSliderSecondLastNestedPos;
    return calcAngle(sp(*ld->fBase, bm.fDiff.fCs), lastCP, last2CP);
  }
};

// --- Evaluators ---
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
      if (pll && !std::holds_alternative<Spinner>(*pll->fBase)) {
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

// --- RhythmEvaluator ---
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

// --- ReadingEvaluator ---
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
  int fObjCount = 0;

  AimSkill(bool inc) : fInc(inc) {}

  void process(const OsuDifficultyHitObject &c) {
    ++fObjCount;
    if (c.fIdx == 0) {
      fSecB = c.fStart;
      fSecE = fSecB + kSL;
      fSecP = strainOf(c);
      return;
    }
    backfill(c);
    double s = strainOf(c);
    if (s > fSecP) {
      fQ.clear();
      save(fSecE - fSecB);
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

  std::vector<Pk> reduced() const {
    constexpr int kChunk = 20;
    constexpr int kReducedTime = 4000;
    constexpr double kBase = 0.727;

    std::vector<Pk> all = fPks;
    all.push_back({fSecP, std::round(fSecE - fSecB)});
    for (auto it = all.begin(); it != all.end();)
      if (it->v <= 0)
        it = all.erase(it);
      else
        ++it;
    std::ranges::sort(all, [](const Pk &a, const Pk &b) { return a.v > b.v; });

    std::vector<Pk> result;
    result.reserve(all.size() + 200);
    double timeSum = 0;
    int skip = 0;
    for (const auto &p : all) {
      if (skip >= static_cast<int>(all.size()) || timeSum >= kReducedTime)
        break;
      for (int added = 0; added < static_cast<int>(p.l); added += kChunk) {
        double scale = std::log10(
            std::clamp((timeSum + added) / static_cast<double>(kReducedTime),
                       0.0, 1.0) *
                9.0 +
            1.0);
        double m = kBase + (1.0 - kBase) * scale;
        result.push_back(
            {p.v * m, std::min(static_cast<double>(kChunk), p.l - added)});
      }
      timeSum += p.l;
      ++skip;
    }
    result.insert(result.end(), all.begin() + skip, all.end());
    std::ranges::sort(result,
                      [](const Pk &a, const Pk &b) { return a.v > b.v; });
    return result;
  }

private:
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

  void save(double l) { fPks.push_back({fSecP, std::round(l)}); }
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

class SpeedSkill {
public:
  void process(const OsuDifficultyHitObject &c) {
    if (std::holds_alternative<Spinner>(*c.fBase))
      return;
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

class ReadingSkill {
public:
  void process(const OsuDifficultyHitObject &c) {
    if (std::holds_alternative<Spinner>(*c.fBase))
      return;
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
    int reducedCount =
        static_cast<int>(std::min(result.size(), calculateReducedNoteCount()));
    for (int i = 0; i < reducedCount; ++i) {
      double scale = std::log10(
          std::clamp(static_cast<double>(i) / static_cast<double>(reducedCount),
                     0.0, 1.0) *
              9.0 +
          1.0);
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

[[nodiscard]] inline StarRating calculateStars(const Beatmap &bm,
                                               ModSet mods = mod::kNone) {
  using namespace stars;
  if (bm.fObjects.size() < 2)
    return {};
  double rate = clockRate(mods);
  std::vector<OsuDifficultyHitObject> objs;
  objs.reserve(bm.fObjects.size());
  for (std::size_t i = 1; i < bm.fObjects.size(); ++i)
    objs.push_back(OsuDifficultyHitObject(bm.fObjects[i], bm.fObjects[i - 1],
                                          rate, objs,
                                          static_cast<int>(objs.size()), bm));
  if (objs.empty())
    return {};
  AimSkill aimSkill(true);
  SpeedSkill spdSkill;
  ReadingSkill rdSkill;
  for (auto &o : objs) {
    if (o.fIdx < 3) {
      double s = SnapAimEvaluator::Eval(o, true);
      double f = FlowAimEvaluator::Eval(o, true);
      double a = AgilityEvaluator::eval(o);
      double sp = SpeedEvaluator::eval(o);
      double rh = RhEvaluator::eval(o);
      auto typeName = [](const HitObject &o) -> std::string {
        if (std::holds_alternative<Circle>(o))
          return "C";
        if (std::holds_alternative<Slider>(o))
          return "S";
        return "X";
      };
      std::println(
          "obj[{}]: snap={:.3f} flow={:.3f} ag={:.3f} spd={:.3f} rh={:.3f} "
          "dt={:.0f} dist={:.1f} hw={:.1f} od={:.2f} angle={} type={}",
          o.fIdx, s, f, a, sp, rh, o.fADT, o.fLJD, o.fHW, o.fODv,
          o.fA ? std::format("{:.2f}", *o.fA) : "null", typeName(*o.fBase));
    }
    aimSkill.process(o);
    spdSkill.process(o);
    rdSkill.process(o);
  }
  double aimDiff = aimSkill.difficulty();
  double spdDiff = spdSkill.difficulty();
  double rdDiff = rdSkill.difficulty();
  auto rpks = aimSkill.reduced();
  double topV = rpks.empty() ? 0 : rpks[0].v;
  double top10V = 0;
  double top10L = 0;
  for (int i = 0; i < 10 && i < static_cast<int>(rpks.size()); ++i) {
    top10V = rpks[i].v;
    top10L = rpks[i].l;
    if (i < 2)
      std::println("  peak[{}]: v={:.1f} l={:.0f}", i, rpks[i].v, rpks[i].l);
  }
  double totalWtSum = 0, totalTime = 0;
  for (auto &p : rpks) {
    double st = totalTime;
    double et = totalTime + p.l / 400.0;
    double w = std::pow(0.9, st) - std::pow(0.9, et);
    totalWtSum += w;
    totalTime = et;
  }
  std::println(
      "aimDiff={:.2f} spdDiff={:.2f} rdDiff={:.2f} aimPks={} reducedPks={} "
      "topV={:.0f} top10V={:.0f} top10L={:.0f} wtSum={:.6f} totTime={:.2f}",
      aimDiff, spdDiff, rdDiff, aimSkill.fPks.size(), rpks.size(), topV, top10V,
      top10L, totalWtSum, totalTime);

  double ar = std::pow(aimDiff, 0.63) * 0.02275;
  double speedRating = std::sqrt(spdDiff) * 0.0675;
  double readingRating = std::sqrt(rdDiff) * 0.0675;

  double ba = 4.0 * std::pow(ar, 3.0);
  double bs = 4.0 * std::pow(speedRating, 3.0);
  double br = 4.0 * std::pow(readingRating, 3.0);
  double bp = diffutil::norm(1.1, ba, bs, br);

  double total = std::cbrt(bp * 1.12);

  std::println(
      "aimRating={:.2f} spdRating={:.2f} rdRating={:.2f} totalStars={:.2f}", ar,
      speedRating, readingRating, total);

  return {ar, speedRating, total};
}

} // namespace osu
