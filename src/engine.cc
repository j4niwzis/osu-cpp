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
  // Signed judgement delta. Only meaningful as *tap timing* for circles hit
  // by input; sliders and spinners are finalized at their end, so their
  // delta is the finalization offset, not a tap. For timing statistics use
  // Engine::tapDeltas().
  double fDelta = 0.0;
};

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
    }
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

    // Finalize any sliders/spinners whose window has passed without input.
    for (std::size_t i = fProcessedObjects; i < fMap.fObjects.size(); ++i) {
      if (fStates[i].fJudged)
        continue;
      const double end = this->objectEnd(i);
      const double missWindow = end + windowMeh(fDiff.fOd);
      if (time < missWindow)
        break;
      this->finalizeObject(i);
    }
  }

private:
  struct ObjectState {
    bool fHeadHit = false;
    bool fTracking = false;
    bool fJudged = false;
    Judgement fPending = judgement::Great{};
    int fRotations = 0;
    double fLastAngle = 0.0;
    double fTotalAngle = 0.0;
    bool fSpinnerInitialized = false;
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

  void finalizeObject(std::size_t i) {
    if (fStates[i].fJudged)
      return;

    const double end = this->objectEnd(i);
    std::visit(
        Overloaded{
            [this, i, end](const Circle &) {
              this->judge(i, judgement::Miss{}, end - this->circleTime(i));
            },
            [this, i, end](const Slider &) {
              if (fStates[i].fHeadHit && fStates[i].fTracking) {
                this->judge(i, fStates[i].fPending, end - this->circleTime(i));
              } else if (fStates[i].fHeadHit) {
                this->judge(i, judgement::Good{}, end - this->circleTime(i));
              } else {
                this->judge(i, judgement::Miss{}, end - this->circleTime(i));
              }
            },
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

  void judge(std::size_t i, Judgement j, double delta) {
    fStates[i].fJudged = true;
    fEvents.push_back({i, j, delta});
    registerHit(fScore, j, fMods, fDiff.fHp);
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
            if (fStates[i].fHeadHit)
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
            fStates[i].fHeadHit = true;
            fStates[i].fPending = judgeDelta(delta, fDiff.fOd);
            fTapDeltas.push_back(delta);
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
                       if (!fStates[i].fHeadHit)
                         return;
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
