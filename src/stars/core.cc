export module osu.stars:core;

import std;
import osu.types;
import osu.curves;
import osu.rules;
import osu.beatmap;

export namespace osu {

struct StarRating {
  double fAim = 0.0;
  double fSpeed = 0.0;
  double fTotal = 0.0;
  double fReading = 0.0;
  double fReadingValue = 0.0;   // before the rating curve
  double fReducedNotes = 0.0;   // how many notes the first minute covers
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

// DifficultyHitObject.HitWindow: twice the legacy window, so the same
// rounding the ruleset judges by.
inline double hitWindowGreat(double od) noexcept {
  return 2.0 * osu::windowGreat(od);
}
[[nodiscard]] inline double lazerCircleRadius(double cs) noexcept {
  return osu::circleRadius(cs);
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
    double spanDur = bm.sliderSpanDuration(*sl);
    SliderPath path = SliderPath::from(*sl);
    Vec2 slStacked = stackedPosition(*sl, fCsR);
    Vec2 stackOff = slStacked - sl->fPos;
    fHasLE = true;
    Vec2 cur = slStacked;
    double sf = kNR / fRad;

    double vel = bm.sliderVelocityAt(sl->fTime);
    double pathLen = path.length();
    double tickDist = std::clamp(bm.sliderTickDistance(*sl), 0.0, pathLen);
    double minDistEnd = vel * 10.0;

    // The nested objects, with their times: the head, then every span's ticks
    // and its repeat, then the tail. The times matter because of the rule
    // below about a tick that falls after the tracking ends.
    struct Nested {
      Vec2 fPos;
      double fTime = 0.0;
      bool fRepeat = false;
      bool fTick = false;
    };
    std::vector<Nested> nested;
    nested.push_back({slStacked, rawStart, false, false});

    for (int span = 0; span < sl->fRepeat; ++span) {
      const double spanStart = rawStart + span * spanDur;
      bool reversed = (span % 2 == 1);
      if (tickDist > 0) {
        for (double d = tickDist; d <= pathLen; d += tickDist) {
          if (d >= pathLen - minDistEnd)
            break;
          double progress = d / pathLen;
          double ratio = reversed ? 1.0 - progress : progress;
          nested.push_back({path.positionAt(ratio * pathLen) + stackOff,
                            spanStart + ratio * spanDur, false, true});
        }
      }
      if (span < sl->fRepeat - 1) {
        double repeatProgress = (span + 1) % 2;
        nested.push_back({path.positionAt(repeatProgress * pathLen) + stackOff,
                          spanStart + spanDur, true, false});
      } else {
        double tailProgress = static_cast<double>(sl->fRepeat % 2);
        nested.push_back({path.positionAt(tailProgress * pathLen) + stackOff,
                          rawStart + rawDur, false, false});
      }
    }

    // OsuDifficultyHitObject.computeSliderCursorPosition: when the last real
    // tick falls after tracking would have ended, tracking is extended to it
    // and that tick is moved to the end of the list. lazer calls this "not
    // correct from a difficulty calculation perspective" and keeps it for a
    // zero diff against known output, so we keep it too.
    auto lastTick = nested.end();
    for (auto it = nested.begin(); it != nested.end(); ++it) {
      if (it->fTick)
        lastTick = it;
    }
    if (lastTick != nested.end() && lastTick->fTime > trackingEndTime) {
      trackingEndTime = lastTick->fTime;
      Nested moved = *lastTick;
      nested.erase(lastTick);
      nested.push_back(moved);
    }

    // The lazy end position starts as the path position at the moment
    // tracking ends, and is replaced below by wherever the cursor actually
    // ended up.
    fLTT = trackingEndTime - rawStart;
    double endTimeMin = fLTT / spanDur;
    if (!std::isfinite(endTimeMin))
      endTimeMin = 0.0;
    if (static_cast<int>(endTimeMin) % 2 >= 1)
      endTimeMin = 1.0 - std::fmod(endTimeMin, 1.0);
    else
      endTimeMin = std::fmod(endTimeMin, 1.0);
    fLEP = path.positionAt(endTimeMin * path.length()) + stackOff;

    std::vector<Vec2> nestedPositions;
    nestedPositions.reserve(nested.size());
    for (const auto &n : nested)
      nestedPositions.push_back(n.fPos);

    if (nestedPositions.size() >= 2)
      fSliderSecondLastNestedPos = nestedPositions[nestedPositions.size() - 2];

    for (std::size_t i = 1; i < nestedPositions.size(); ++i) {
      Vec2 mv = nestedPositions[i] - cur;
      double ml = sf * mv.length();
      // A repeat asks for a tighter movement than a tick does -- but the last
      // nested object is handled by the branch below whatever it is, and
      // lazer's "else if" means it keeps the wider threshold there.
      double req = kASR;
      if (i != nestedPositions.size() - 1 && nested[i].fRepeat) {
        req = kNR;
      }
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
      // Slider.RepeatCount is the number of repeats, which is one less than
      // the number of spans the file records: a plain slider repeats zero
      // times. Feeding the span count in here inflated the travel distance of
      // every repeat slider.
      const double repeats = static_cast<double>(std::max(0, cs->fRepeat - 1));
      fTD = fLTD * std::max(1.0, std::pow(repeats, 0.3));
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

      const SliderPath path = SliderPath::from(*ls);
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
      double sliderA =
          this->calcSliderAngle(*ld, last2CP, bm.fDiff.fCs);
      // NormalisedVectorAngle is assigned whatever atan2 gives, including
      // for a zero vector -- which happens whenever the cursor is already
      // where the next object is, as it is when a slider is followed by
      // another at the same place. Leaving it unset instead made
      // vectorAngleRepetition count nothing for those objects, so its nerf
      // came out as no nerf at all.
      Vec2 v = sp(*fBase, bm.fDiff.fCs) - lastCP;
      fNVA = std::atan2(std::abs(v.fY), std::abs(v.fX));
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
  // calculateSliderAngle: the same angle as above, but with the previous
  // slider's second-to-last nested object standing in for where the cursor
  // came from. The vertex is this object -- passing the previous object's
  // position, as this did, makes the second vector exactly zero, and then
  // atan2(0, 0) decides the angle by the sign of a floating point zero: it
  // came out as pi (leaving the real angle) when both components of the other
  // vector were negative, and as zero (destroying it, since the two are
  // min()ed) otherwise. That is why the angle was right on some objects and
  // absent on others.
  [[nodiscard]] double calcSliderAngle(const OsuDifficultyHitObject &ld,
                                       Vec2 last2CP, double cs) const {
    Vec2 lastCP = getEndPos(ld);
    if (std::holds_alternative<Slider>(*ld.fBase) && ld.fTD > 0)
      last2CP = ld.fSliderSecondLastNestedPos;
    return calcAngle(sp(*fBase, cs), lastCP, last2CP);
  }
};

} // namespace stars

} // namespace osu
