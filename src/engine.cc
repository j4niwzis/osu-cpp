export module osu.engine;

import std;
import osu.types;
import osu.curves;
import osu.beatmap;
import osu.rules;

export namespace osu {

enum class InputAction { kPress, kRelease, kMove };

struct InputEvent {
  double fTime; // ms
  Vec2 fPos;    // cursor position in playfield coordinates
  InputAction fAction;
};

struct HitEvent {
  std::size_t fIndex;
  Judgement fResult;
  // What was judged: the object itself (a circle, a slider's head, a
  // spinner), one of a slider's ticks or repeats, or a slider's tail. Only
  // basic results carry a judgement worth showing.
  HitKind fKind = HitKind::kBasic;
  // Signed judgement delta. Only meaningful as *tap timing* for circles hit
  // by input; sliders and spinners are finalized at their end, so their
  // delta is the finalization offset, not a tap. For timing statistics use
  // Engine::tapDeltas().
  double fDelta = 0.0;
};

// A slider's tick, repeat or tail, at the time it is judged.
struct Nested {
  double fTime = 0.0;
  HitKind fKind = HitKind::kLargeTick;
};

// SliderEventGenerator.TAIL_LENIENCY: the tail is judged 36 ms before the
// slider actually ends.
inline constexpr double kTailLeniency = 36.0;

class Engine {
public:
  explicit Engine(const Beatmap &map, ModSet mods = mod::kNone)
      : fMap(map), fMods(mods) {
    fDiff = applyMods(map.fDiff, mods);
    for (std::size_t i = 0; i < map.fObjects.size(); ++i) {
      if (const Slider *s = std::get_if<Slider>(&map.fObjects[i])) {
        fPaths.push_back(SliderPath::from(*s));
      } else {
        fPaths.emplace_back();
      }
      fStates.emplace_back();
      if (const Slider *s = std::get_if<Slider>(&map.fObjects[i])) {
        fStates.back().fNested = this->buildNested(*s);
      }
    }
    this->computeMaxima();
  }

  [[nodiscard]] const Beatmap &map() const noexcept { return fMap; }
  [[nodiscard]] ModSet mods() const noexcept { return fMods; }
  [[nodiscard]] const ScoreState &score() const noexcept { return fScore; }
  [[nodiscard]] const Vec2 &cursor() const noexcept { return fCursor; }
  [[nodiscard]] double clockRate() const noexcept { return fDiff.fClockRate; }
  [[nodiscard]] std::span<const HitEvent> events() const noexcept {
    return fEvents;
  }
  // Signed tap-timing deltas (press vs object start) for every successful
  // head hit: circles and slider heads. This is the series UR / mean hit
  // error are defined over.
  [[nodiscard]] std::span<const double> tapDeltas() const noexcept {
    return fTapDeltas;
  }
  [[nodiscard]] bool finished() const noexcept {
    return fProcessedObjects == fMap.fObjects.size();
  }
  [[nodiscard]] bool isJudged(std::size_t index) const noexcept {
    return index < fStates.size() && fStates[index].fJudged;
  }
  [[nodiscard]] bool headHit(std::size_t index) const noexcept {
    return index < fStates.size() && fStates[index].fHeadHit;
  }
  [[nodiscard]] bool isTracking(std::size_t index) const noexcept {
    return index < fStates.size() && fStates[index].fTracking;
  }
  [[nodiscard]] int spinnerRotations(std::size_t index) const noexcept {
    return index < fStates.size() ? fStates[index].fRotations : 0;
  }
  // Every object plus everything nested under it: the combo a perfect play
  // ends on, and the same number the server reports for a beatmap.
  [[nodiscard]] int maxAchievableCombo() const noexcept {
    int total = 0;
    for (const auto &state : fStates) {
      total += 1 + static_cast<int>(state.fNested.size());
    }
    return total;
  }

  // Submit an input event. Events must be submitted in non-decreasing time
  // order.
  void submit(const InputEvent &ev) {
    fCursor = ev.fPos;
    this->advance(ev.fTime);

    if (ev.fAction == InputAction::kPress) {
      fKeyDown = true;
      this->tryHitHead(ev.fTime, ev.fPos);
    } else if (ev.fAction == InputAction::kRelease) {
      fKeyDown = false;
    }

    // Update active slider tracking state at this time.
    if (ev.fAction != InputAction::kRelease) {
      this->updateTracking(ev.fTime);
    }
  }

  void advance(double time) {
    // Update tracking state for the active slider/spinner at the current time.
    this->updateTracking(time);

    for (std::size_t i = fProcessedObjects; i < fMap.fObjects.size(); ++i) {
      if (fStates[i].fJudged)
        continue;
      // Nothing to say about an object whose window has not opened yet, and
      // nothing after it can have anything to say either.
      if (startTime(fMap.fObjects[i]) - windowMeh(fDiff.fOd) > time)
        break;
      this->progressObject(i, time);
    }
    while (fProcessedObjects < fMap.fObjects.size() &&
           fStates[fProcessedObjects].fJudged) {
      ++fProcessedObjects;
    }
  }

private:
  struct ObjectState {
    bool fHeadHit = false;    // the head was hit rather than missed
    bool fHeadJudged = false; // the head has a result either way
    bool fTracking = false;
    bool fJudged = false; // the object and everything under it is resolved
    int fRotations = 0;
    double fLastAngle = 0.0;
    double fTotalAngle = 0.0;
    bool fSpinnerInitialized = false;
    std::vector<Nested> fNested;
    std::size_t fNextNested = 0;
  };

  const Beatmap &fMap;
  ModSet fMods = mod::kNone;
  EffectiveDifficulty fDiff;
  std::size_t fProcessedObjects = 0;
  ScoreState fScore;
  std::vector<HitEvent> fEvents;
  std::vector<double> fTapDeltas;
  std::vector<SliderPath> fPaths;
  std::vector<ObjectState> fStates;
  Vec2 fCursor{kPlayfieldCenter};
  bool fKeyDown = false;

  [[nodiscard]] double objectEnd(std::size_t i) const noexcept {
    return std::visit(Overloaded{
                          [this](const Slider &o) -> double {
                            return o.fTime +
                                   fMap.sliderSpanDuration(o) * o.fRepeat;
                          },
                          [](const Spinner &o) -> double { return o.fEnd; },
                          [](const Circle &o) -> double { return o.fTime; },
                      },
                      fMap.fObjects[i]);
  }

  // Everything a single object still owes at this time: a head that ran out
  // of window, ticks and a tail whose moments have passed, a spinner that
  // ended.
  void progressObject(std::size_t i, double time) {
    auto &st = fStates[i];
    std::visit(
        Overloaded{
            [&](const Circle &o) {
              if (time > o.fTime + windowMeh(fDiff.fOd)) {
                this->judge(i, judgement::Miss{}, time - o.fTime);
              }
            },
            [&](const Slider &o) {
              if (!st.fHeadJudged && time > o.fTime + windowMeh(fDiff.fOd)) {
                st.fHeadJudged = true;
                // A missed head does not end the slider: lazer keeps judging
                // its ticks, and tracking can still pick it up.
                this->emit(i, HitKind::kBasic, judgement::Miss{}, false,
                           time - o.fTime);
              }
              while (st.fNextNested < st.fNested.size() &&
                     time >= st.fNested[st.fNextNested].fTime) {
                const auto &n = st.fNested[st.fNextNested];
                // TryJudgeNestedObject: nothing under a slider is judged
                // before its head is, and what it gets is whether the slider
                // is being tracked at that moment.
                const bool hit = st.fHeadJudged && st.fTracking;
                this->emit(i, n.fKind,
                           hit ? Judgement{judgement::Great{}}
                               : Judgement{judgement::Miss{}},
                           hit, time - n.fTime);
                ++st.fNextNested;
              }
              if (st.fHeadJudged && st.fNextNested >= st.fNested.size() &&
                  time >= this->objectEnd(i)) {
                st.fJudged = true;
              }
            },
            [&](const Spinner &o) {
              if (time >= o.fEnd + windowMeh(fDiff.fOd)) {
                this->finalizeSpinner(i);
              }
            },
        },
        fMap.fObjects[i]);
  }

  void finalizeSpinner(std::size_t i) {
    if (fStates[i].fJudged)
      return;
    const double end = this->objectEnd(i);
    std::visit(
        Overloaded{
            [](const Circle &) {},
            [](const Slider &) {},
            [this, i, end](const Spinner &o) {
              const double progress = spinnerProgress(
                  fStates[i].fRotations, o.fEnd - o.fTime, fDiff.fOd);
              if (progress >= 1.0) {
                this->judge(i, judgement::Great{}, end - o.fTime);
              } else if (progress >= 0.9) {
                this->judge(i, judgement::Good{}, end - o.fTime);
              } else if (progress >= 0.75) {
                this->judge(i, judgement::Meh{}, end - o.fTime);
              } else {
                this->judge(i, judgement::Miss{}, end - o.fTime);
              }
            },
        },
        fMap.fObjects[i]);

    while (fProcessedObjects < fMap.fObjects.size() &&
           fStates[fProcessedObjects].fJudged) {
      ++fProcessedObjects;
    }
  }

  // A slider's ticks, repeats and tail, at the times they are judged.
  //
  // This is SliderEventGenerator: ticks every tick distance along each span,
  // dropped when they would land within ten milliseconds of the span's end, a
  // repeat at the end of every span but the last, and the tail at the later
  // of half the slider's duration and its end less the 36 ms of leniency.
  [[nodiscard]] std::vector<Nested> buildNested(const Slider &s) const {
    std::vector<Nested> nested;
    const double spanDuration = fMap.sliderSpanDuration(s);
    const double length = s.fPixelLength;
    if (spanDuration <= 0.0 || length <= 0.0) {
      return nested;
    }
    const double velocity = length / spanDuration;
    const double minDistanceFromEnd = velocity * 10.0;
    const double tickDistance =
        std::clamp(fMap.sliderTickDistance(s), 0.0, length);

    for (int span = 0; span < s.fRepeat; ++span) {
      const double spanStart = s.fTime + span * spanDuration;
      const bool reversed = (span % 2) == 1;
      if (tickDistance > 0.0) {
        for (double d = tickDistance; d < length; d += tickDistance) {
          if (d >= length - minDistanceFromEnd) {
            break;
          }
          const double pathProgress = d / length;
          const double timeProgress = reversed ? 1.0 - pathProgress
                                               : pathProgress;
          nested.push_back(
              {spanStart + timeProgress * spanDuration, HitKind::kLargeTick});
        }
      }
      if (span < s.fRepeat - 1) {
        nested.push_back({spanStart + spanDuration, HitKind::kLargeTick});
      }
    }
    std::ranges::stable_sort(nested, {}, &Nested::fTime);

    const double total = spanDuration * s.fRepeat;
    double tailTime = std::max(s.fTime + total / 2.0,
                               s.fTime + total - kTailLeniency);
    if (!nested.empty()) {
      // The tail can only be judged once every tick before it has been.
      tailTime = std::max(tailTime, nested.back().fTime);
    }
    nested.push_back({tailTime, HitKind::kSliderTail});
    return nested;
  }

  // What a perfect play would have scored, which is the denominator of both
  // halves of the standardised score.
  void computeMaxima() {
    ScoreState perfect;
    for (std::size_t i = 0; i < fMap.fObjects.size(); ++i) {
      registerResult(perfect, HitKind::kBasic, judgement::Great{}, true, fMods,
                     fDiff.fHp);
      for (const auto &n : fStates[i].fNested) {
        registerResult(perfect, n.fKind, judgement::Great{}, true, fMods,
                       fDiff.fHp);
      }
    }
    fScore.fMaxComboPortion = perfect.fComboPortion;
    fScore.fMaxAccuracyJudgements = perfect.fAccuracyJudgements;
  }

  void emit(std::size_t i, HitKind kind, Judgement j, bool hit, double delta) {
    fEvents.push_back({i, j, kind, delta});
    registerResult(fScore, kind, j, hit, fMods, fDiff.fHp);
  }

  void judge(std::size_t i, Judgement j, double delta) {
    fStates[i].fJudged = true;
    const bool hit = !std::holds_alternative<judgement::Miss>(j);
    this->emit(i, HitKind::kBasic, j, hit, delta);
  }

  void tryHitHead(double time, Vec2 pos) {
    for (std::size_t i = fProcessedObjects; i < fMap.fObjects.size(); ++i) {
      if (fStates[i].fJudged)
        continue;

      auto visitor = Overloaded{
          [&](const Circle &) -> bool {
            if (pos.distanceTo(this->circlePosition(i)) >
                circleRadius(fDiff.fCs)) {
              return false;
            }
            const double delta = time - startTime(fMap.fObjects[i]);
            if (std::abs(delta) > windowMeh(fDiff.fOd)) {
              return false;
            }
            fTapDeltas.push_back(delta);
            this->judge(i, judgeDelta(delta, fDiff.fOd), delta);
            return true;
          },
          [&](const Slider &) -> bool {
            if (fStates[i].fHeadJudged)
              return false;
            const double start = startTime(fMap.fObjects[i]);
            if (pos.distanceTo(this->circlePosition(i)) >
                circleRadius(fDiff.fCs)) {
              return false;
            }
            const double delta = time - start;
            if (std::abs(delta) > windowMeh(fDiff.fOd)) {
              return false;
            }
            // The head is a circle in its own right: it scores, it pops, and
            // it is the last the slider's accuracy hears of the tap. What
            // happens to the body is the ticks' and the tail's business.
            fStates[i].fHeadHit = true;
            fStates[i].fHeadJudged = true;
            fStates[i].fTracking = true;
            fTapDeltas.push_back(delta);
            this->emit(i, HitKind::kBasic, judgeDelta(delta, fDiff.fOd), true,
                       delta);
            return true;
          },
          [&](const Spinner &) -> bool {
            return false; // spinner is finalized by rotation, not by head hit
          },
      };
      if (std::visit(visitor, fMap.fObjects[i])) {
        while (fProcessedObjects < fMap.fObjects.size() &&
               fStates[fProcessedObjects].fJudged) {
          ++fProcessedObjects;
        }
        return;
      }
    }
  }

  void updateTracking(double time) {
    for (std::size_t i = fProcessedObjects; i < fMap.fObjects.size(); ++i) {
      if (fStates[i].fJudged)
        continue;
      std::visit(Overloaded{
                     [this, i, time](const Slider &o) {
                       const double local = (time - o.fTime);
                       const double span = fMap.sliderSpanDuration(o);
                       if (span <= 0.0 || local < 0)
                         return;
                       if (static_cast<int>(local / span) >= o.fRepeat)
                         return;
                       const Vec2 ball = sliderBallPosition(
                           fPaths[i], local, span, o.fPixelLength);
                       const bool inside = fCursor.distanceTo(ball) <=
                                           circleRadius(fDiff.fCs) * 2.4;
                       fStates[i].fTracking = fKeyDown && inside;
                     },
                     [this, i, time](const Spinner &o) {
                       if (time < o.fTime || time > o.fEnd)
                         return;
                       if (!fKeyDown) {
                         fStates[i].fSpinnerInitialized = false;
                         return;
                       }
                       const double angle = fCursor.angleTo(kPlayfieldCenter);
                       if (!fStates[i].fSpinnerInitialized) {
                         fStates[i].fLastAngle = angle;
                         fStates[i].fSpinnerInitialized = true;
                         return;
                       }
                       double delta = angle - fStates[i].fLastAngle;
                       if (delta < -std::numbers::pi)
                         delta += 2.0 * std::numbers::pi;
                       if (delta > std::numbers::pi)
                         delta -= 2.0 * std::numbers::pi;
                       fStates[i].fTotalAngle += delta;
                       fStates[i].fLastAngle = angle;
                       fStates[i].fRotations =
                           static_cast<int>(std::abs(fStates[i].fTotalAngle) /
                                            (2.0 * std::numbers::pi));
                     },
                     [](const Circle &) {},
                 },
                 fMap.fObjects[i]);
    }
  }

  [[nodiscard]] double circleTime(std::size_t i) const noexcept {
    return startTime(fMap.fObjects[i]);
  }

  [[nodiscard]] Vec2 circlePosition(std::size_t i) const noexcept {
    return std::visit(
        Overloaded{
            [](const Circle &o) -> Vec2 { return o.fPos; },
            [](const Slider &o) -> Vec2 { return o.fPos; },
            [](const Spinner &) -> Vec2 { return kPlayfieldCenter; },
        },
        fMap.fObjects[i]);
  }
};

} // namespace osu
