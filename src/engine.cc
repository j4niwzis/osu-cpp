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
  explicit Engine(const Beatmap &map, ModSet mods = mod::kNone,
                  RuleSet rules = RuleSet::kLazer)
      : fMap(map), fMods(mods), fRules(rules) {
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
      } else if (const Spinner *sp = std::get_if<Spinner>(&map.fObjects[i])) {
        fStates.back().fNested = this->buildSpinnerTicks(*sp);
      }
    }
    this->computeMaxima();
    if (fRules == RuleSet::kLazer) {
      this->computeDrain();
    }
    fLastDrainTime = fDrainStart;
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
  // Health reached zero and the play is over. lazer stops counting anything
  // after this: the judgement is still created, but the score processor sees
  // FailedAtJudgement on it and returns without touching the score, the
  // accuracy or the combo.
  [[nodiscard]] bool failed() const noexcept { return fFailed; }
  [[nodiscard]] RuleSet rules() const noexcept { return fRules; }
  [[nodiscard]] bool legacy() const noexcept {
    return fRules == RuleSet::kLegacyClient;
  }

  [[nodiscard]] int spinnerRotations(std::size_t index) const noexcept {
    return index < fStates.size() ? fStates[index].fRotations : 0;
  }
  // Every object plus everything nested under it: the combo a perfect play
  // ends on, and the same number the server reports for a beatmap.
  [[nodiscard]] int maxAchievableCombo() const noexcept {
    int total = 0;
    for (const auto &state : fStates) {
      total += 1; // the object itself: a circle, a slider's head, a spinner
      for (const auto &n : state.fNested) {
        if (!isBonus(n.fKind)) {
          ++total; // a spinner's spins are bonus and raise no combo
        }
      }
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
    this->drainTo(time);
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
    Judgement fLegacyPending = judgement::Great{};
  };

  const Beatmap &fMap;
  ModSet fMods = mod::kNone;
  RuleSet fRules = RuleSet::kLazer;
  EffectiveDifficulty fDiff;
  std::size_t fProcessedObjects = 0;
  ScoreState fScore;
  std::vector<HitEvent> fEvents;
  std::vector<double> fTapDeltas;
  std::vector<SliderPath> fPaths;
  std::vector<ObjectState> fStates;
  Vec2 fCursor{kPlayfieldCenter};
  bool fKeyDown = false;
  double fDrainRate = 0.0;
  double fDrainStart = 0.0;
  double fGameplayEnd = 0.0;
  double fLastDrainTime = 0.0;
  bool fFailed = false;
  ComboResult fComboResult = ComboResult::kPerfect;

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
              while (!this->legacy() && st.fNextNested < st.fNested.size() &&
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
              if (this->legacy()) {
                // The old model: nothing is judged until the slider ends, and
                // then the whole thing is one result.
                if (time >= this->objectEnd(i) && !st.fJudged) {
                  const Judgement result =
                      !st.fHeadHit          ? Judgement{judgement::Miss{}}
                      : st.fTracking        ? st.fLegacyPending
                                            : Judgement{judgement::Good{}};
                  st.fJudged = true;
                  fEvents.push_back(
                      {i, result, HitKind::kBasic, time - o.fTime});
                  registerResult(fScore, HitKind::kBasic, result,
                                 !std::holds_alternative<judgement::Miss>(
                                     result),
                                 fMods, fDiff.fHp);
                  this->legacyHealth(result);
                }
                return;
              }
              if (st.fHeadJudged && st.fNextNested >= st.fNested.size() &&
                  time >= this->objectEnd(i)) {
                st.fJudged = true;
              }
            },
            [&](const Spinner &o) {
              // One spin, one tick: they are handed out as the rotations
              // arrive rather than on a clock, and whatever is left over when
              // the spinner ends is missed.
              const int spun = st.fRotations;
              while (st.fNextNested < st.fNested.size() &&
                     static_cast<int>(st.fNextNested) < spun &&
                     time <= o.fEnd) {
                const auto &n = st.fNested[st.fNextNested];
                this->emit(i, n.fKind, judgement::Great{}, true, 0.0);
                ++st.fNextNested;
              }
              if (time >= o.fEnd) {
                while (st.fNextNested < st.fNested.size()) {
                  const auto &n = st.fNested[st.fNextNested];
                  this->emit(i, n.fKind, judgement::Miss{}, false, 0.0);
                  ++st.fNextNested;
                }
              }
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
              this->judge(i, spinnerJudgement(progress), end - o.fTime);
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

  // A spinner's spins, as objects: one per revolution, and past the required
  // count plus a gap of two they become the larger bonus. They are bonus
  // results -- no accuracy, no combo, straight onto the score.
  [[nodiscard]] std::vector<Nested> buildSpinnerTicks(const Spinner &s) const {
    std::vector<Nested> nested;
    const double duration = s.fEnd - s.fTime;
    if (duration <= 0.0) {
      return nested;
    }
    const int required = spinsRequired(duration, fDiff.fOd);
    const int bonus = maximumBonusSpins(duration, fDiff.fOd);
    const int total = required + kBonusSpinsGap + bonus;
    for (int i = 0; i < total; ++i) {
      const double at =
          s.fTime + static_cast<double>(i + 1) / total * duration;
      nested.push_back({at, i < required + kBonusSpinsGap
                                ? HitKind::kSmallBonus
                                : HitKind::kLargeBonus});
    }
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
    if (fFailed) {
      return; // judged, but it counts for nothing
    }
    registerResult(fScore, kind, j, hit, fMods, fDiff.fHp);
    if (this->legacy()) {
      this->legacyHealth(j);
      return;
    }
    this->applyHealth(i, kind, j, hit);
  }

  // OsuHealthProcessor: an increase per judgement, plus a bonus on the last
  // object of a combo that depends on how the combo went.
  void applyHealth(std::size_t i, HitKind kind, const Judgement &j, bool hit) {
    if (i < fMap.fObjects.size() && objectCombo(fMap.fObjects[i]) == 1 &&
        kind == HitKind::kBasic) {
      fComboResult = ComboResult::kPerfect; // a new combo starts clean
    }

    const bool tick = kind == HitKind::kLargeTick;
    double increase = healthIncreaseFor(kind, j, hit, fDiff.fHp, tick);

    // The combo result only ever degrades within a combo.
    auto degrade = [this](ComboResult to) {
      fComboResult = static_cast<ComboResult>(
          std::min(static_cast<int>(fComboResult), static_cast<int>(to)));
    };
    if (kind == HitKind::kLargeTick && !hit) {
      degrade(ComboResult::kGood);
    } else if (kind == HitKind::kSliderTail && !hit) {
      degrade(ComboResult::kGood);
    } else if (kind == HitKind::kBasic) {
      std::visit(Overloaded{
                     [](judgement::Great) {},
                     [&degrade](judgement::Good) { degrade(ComboResult::kGood); },
                     [&degrade](judgement::Meh) { degrade(ComboResult::kNone); },
                     [&degrade](judgement::Miss) { degrade(ComboResult::kNone); },
                 },
                 j);
    }

    fScore.fHealth = std::min(1.0, fScore.fHealth + increase);

    // HealthProcessor.ApplyResultInternal checks the fail condition after the
    // increase has landed, and CheckDefaultFailCondition skips bonus results
    // and IgnoreHit outright. Nothing fails on a spinner's bonus spin.
    if (!isBonus(kind)) {
      this->checkFailure();
    }

    // Then the object's own judgement, which is where the end-of-combo bonus
    // lives. A slider judges itself with IgnoreHit -- DrawableSlider always
    // applies its maximum result -- so it counts as a hit however the tail
    // went, and a dropped tail does not cost the bonus. A circle or a spinner
    // has to have been hit. IgnoreHit is never a fail condition either.
    if (this->isFinalJudgement(i, kind)) {
      const bool slider = i < fMap.fObjects.size() &&
                          std::holds_alternative<Slider>(fMap.fObjects[i]);
      if (this->lastInCombo(i) && (slider || hit)) {
        fScore.fHealth =
            std::min(1.0, fScore.fHealth + comboBonusFor(fComboResult));
      }
      if (!slider) {
        this->checkFailure();
      }
    }
  }

  // NoFail keeps the bar from ending the play; everything else does not.
  void checkFailure() {
    if (fFailed || hasMod(fMods, mod::kNoFail)) {
      return;
    }
    if (fScore.fHealth <= 0.0) {
      fScore.fHealth = 0.0;
      fFailed = true;
    }
  }

  // The health this client used to give, kept for replays recorded under it.
  void legacyHealth(const Judgement &j) {
    const double hp = fDiff.fHp;
    const double inc = std::visit(
        Overloaded{
            [hp](judgement::Great) { return 0.01 * (10.2 - hp); },
            [hp](judgement::Good) { return 0.01 * (8.0 - hp); },
            [hp](judgement::Meh) { return 0.01 * (4.0 - hp); },
            [hp](judgement::Miss) { return -0.02 * hp; },
        },
        j);
    if (fScore.fHealth >= 0.0) {
      fScore.fHealth = std::min(1.0, fScore.fHealth + inc);
    }
  }

  // Health drains continuously between judgements, but not through breaks and
  // not past the last object.
  void drainTo(double time) {
    if (this->legacy() || fDrainRate <= 0.0 || time <= fLastDrainTime) {
      fLastDrainTime = std::max(fLastDrainTime, time);
      return;
    }
    const double from = std::clamp(fLastDrainTime, fDrainStart, fGameplayEnd);
    const double to = std::clamp(time, fDrainStart, fGameplayEnd);
    double elapsed = to - from;
    for (const auto &period : fMap.fBreaks) {
      const double overlap = std::min(to, period.fEnd) -
                             std::max(from, period.fStart);
      if (overlap > 0.0) {
        elapsed -= overlap;
      }
    }
    // DrainingHealthProcessor.Update only subtracts; the fail condition is
    // checked in ApplyResultInternal, so a bar that dips below zero between
    // judgements and is brought back by the next one does not fail.
    if (elapsed > 0.0 && !fFailed) {
      fScore.fHealth -= fDrainRate * elapsed;
    }
    fLastDrainTime = time;
  }

  [[nodiscard]] static int objectCombo(const HitObject &o) noexcept {
    return std::visit(Overloaded{
                          [](const Circle &c) { return c.fCombo; },
                          [](const Slider &s) { return s.fCombo; },
                          [](const Spinner &s) { return s.fCombo; },
                      },
                      o);
  }

  [[nodiscard]] bool lastInCombo(std::size_t i) const noexcept {
    if (i + 1 >= fMap.fObjects.size()) {
      return true;
    }
    return objectCombo(fMap.fObjects[i + 1]) == 1;
  }

  // Whether this is the judgement that finishes the object: a circle's own, or
  // a slider's tail.
  [[nodiscard]] bool isFinalJudgement(std::size_t i,
                                      HitKind kind) const noexcept {
    if (i >= fMap.fObjects.size()) {
      return kind == HitKind::kBasic;
    }
    if (std::holds_alternative<Slider>(fMap.fObjects[i])) {
      return kind == HitKind::kSliderTail;
    }
    return kind == HitKind::kBasic;
  }

  // A perfect play's health increases, which is what the drain rate is solved
  // against.
  void computeDrain() {
    if (fMap.fObjects.empty()) {
      return;
    }
    fDrainStart = startTime(fMap.fObjects.front());
    fGameplayEnd = this->objectEnd(fMap.fObjects.size() - 1);

    std::vector<std::pair<double, double>> increases;
    for (std::size_t i = 0; i < fMap.fObjects.size(); ++i) {
      const bool slider = std::holds_alternative<Slider>(fMap.fObjects[i]);
      double head =
          healthIncreaseFor(HitKind::kBasic, judgement::Great{}, true,
                            fDiff.fHp);
      increases.emplace_back(startTime(fMap.fObjects[i]), head);
      for (const auto &n : fStates[i].fNested) {
        if (isBonus(n.fKind)) {
          continue; // bonus results are left out of the drain rate solve
        }
        double amount = healthIncreaseFor(n.fKind, judgement::Great{}, true,
                                          fDiff.fHp,
                                          n.fKind == HitKind::kLargeTick);
        increases.emplace_back(n.fTime, amount);
      }
      if (slider) {
        // The slider judges itself once its nested objects are done. It gives
        // no health, but it is an entry in the list all the same, and on a map
        // with breaks that changes which entry swallows the break.
        increases.emplace_back(this->objectEnd(i), 0.0);
      }
    }
    // Left in simulation order, which is the order lazer builds them in: the
    // list is not sorted there.

    fDrainRate = computeDrainRate(increases, fMap.fBreaks, fDrainStart,
                                  targetMinimumHealth(fDiff.fHp));
  }

  void judge(std::size_t i, Judgement j, double delta) {
    fStates[i].fJudged = true;
    const bool hit = !std::holds_alternative<judgement::Miss>(j);
    this->emit(i, HitKind::kBasic, j, hit, delta);
  }

  // StartTimeOrderedHitPolicy. Only circles and slider heads block -- ticks,
  // tails and spinners do not -- and the rule is about the *last* one before
  // the object being hit: if it is still unjudged and the press came before
  // its start time, the press does nothing at all. lazer shakes the object to
  // say so.
  [[nodiscard]] bool blocksLaterHits(std::size_t i) const noexcept {
    return !std::holds_alternative<Spinner>(fMap.fObjects[i]);
  }

  [[nodiscard]] bool headJudged(std::size_t i) const noexcept {
    return std::holds_alternative<Slider>(fMap.fObjects[i])
               ? fStates[i].fHeadJudged
               : fStates[i].fJudged;
  }

  [[nodiscard]] std::optional<std::size_t>
  lastBlockingBefore(std::size_t i) const {
    const double target = startTime(fMap.fObjects[i]);
    std::optional<std::size_t> last;
    for (std::size_t n = fProcessedObjects; n < i; ++n) {
      if (startTime(fMap.fObjects[n]) >= target) {
        break;
      }
      if (this->blocksLaterHits(n)) {
        last = n;
      }
    }
    return last;
  }

  [[nodiscard]] bool allowedToHit(std::size_t i, double time) const {
    const auto blocking = this->lastBlockingBefore(i);
    if (!blocking) {
      return true;
    }
    // Hits at exactly the blocking object's time are allowed, for maps with
    // simultaneous objects.
    return this->headJudged(*blocking) ||
           time >= startTime(fMap.fObjects[*blocking]);
  }

  // Hitting an object misses everything before it that was still waiting.
  void missEarlierBlocking(std::size_t i, double time) {
    const double target = startTime(fMap.fObjects[i]);
    for (std::size_t n = fProcessedObjects; n < i; ++n) {
      if (startTime(fMap.fObjects[n]) >= target) {
        break;
      }
      if (!this->blocksLaterHits(n) || this->headJudged(n)) {
        continue;
      }
      if (std::holds_alternative<Slider>(fMap.fObjects[n])) {
        fStates[n].fHeadJudged = true;
        this->emit(n, HitKind::kBasic, judgement::Miss{}, false,
                   time - startTime(fMap.fObjects[n]));
      } else {
        this->judge(n, judgement::Miss{}, time - startTime(fMap.fObjects[n]));
      }
    }
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
            if (this->legacy()) {
              fStates[i].fLegacyPending = judgeDelta(delta, fDiff.fOd);
              return true;
            }
            this->emit(i, HitKind::kBasic, judgeDelta(delta, fDiff.fOd), true,
                       delta);
            return true;
          },
          [&](const Spinner &) -> bool {
            return false; // spinner is finalized by rotation, not by head hit
          },
      };
      // Whether this object could take the press at all, before letting it.
      const bool wouldHit = std::visit(
          Overloaded{
              [&](const Circle &) {
                return !fStates[i].fJudged &&
                       pos.distanceTo(this->circlePosition(i)) <=
                           circleRadius(fDiff.fCs) &&
                       std::abs(time - startTime(fMap.fObjects[i])) <=
                           windowMeh(fDiff.fOd);
              },
              [&](const Slider &) {
                return !fStates[i].fHeadJudged &&
                       pos.distanceTo(this->circlePosition(i)) <=
                           circleRadius(fDiff.fCs) &&
                       std::abs(time - startTime(fMap.fObjects[i])) <=
                           windowMeh(fDiff.fOd);
              },
              [](const Spinner &) { return false; },
          },
          fMap.fObjects[i]);
      if (!this->legacy()) {
        if (wouldHit && !this->allowedToHit(i, time)) {
          return; // note lock: the press is swallowed, nothing is judged
        }
        if (wouldHit) {
          this->missEarlierBlocking(i, time);
        }
      }
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
