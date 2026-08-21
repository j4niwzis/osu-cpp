export module client.gameplayview;

import std;
import skia;
import osu;
import skin;
import client.ui;

export namespace client {

// Everything drawn during play: the playfield objects, the effects that
// outlive their hit (judgement popups, bursts, fading objects, the cursor
// trail) and the HUD. The presentation state lives here rather than in the
// app, which now only feeds it judgements and asks it to draw.
class GameplayView {
public:
  // What the renderer needs from the app for a frame. Passed in rather than
  // captured so the view holds no back-reference.
  struct Ctx {
    skia::SkCanvas *fCanvas = nullptr;
    const osu::Beatmap *fMap = nullptr;
    const osu::Engine *fEngine = nullptr;
    Skin *fSkin = nullptr;
    const osu::ComboInfo *fCombo = nullptr;
    skia::SkFont *fFont = nullptr;
    skia::SkFont *fDisplayFont = nullptr; // heavy face for judgements
    float fScale = 1.0f;
    float fOffsetX = 0.0f;
    float fOffsetY = 0.0f;
    int fScreenW = 0;
    int fScreenH = 0;
    osu::Vec2 fCursor{};
    float fCursorSize = 1.0f;
    // The HUD is written in pixels against a 1080-tall screen; this is how
    // much bigger the surface being drawn on is. One on a 1080p window, two
    // in a 4K render -- where the numbers in the corner were otherwise a
    // quarter of the size they have in the game.
    float fUiScale = 1.0f;
    float fDim = 0.7f;
    bool fNoGlow = false;
    // OsuSetting.HitLighting. With it off, DrawableOsuJudgement leaves the
    // lighting at zero alpha and never animates it.
    bool fHitLighting = true;
    bool fShowProfile = false;
  };

  struct Popup {
    osu::Judgement fResult;
    double fTime;
    osu::Vec2 fPos;
  };
  struct HitBurst {
    osu::Vec2 fPos;
    double fTime;
    skia::SkColor fTint;
  };
  struct CursorTrailPoint {
    osu::Vec2 fPos;
    double fTime;
  };
  // LegacyFollowCircle is driven by four moments: tracking starting, a tick
  // or repeat being hit, the tail being hit, and any of them being missed.
  // None of that is a property of the current frame, so it is remembered here.
  struct FollowState {
    double fPress = -1.0e18;   // tracking began
    double fTick = -1.0e18;    // a tick or repeat was hit
    double fEnd = -1.0e18;     // the tail was hit: the slider is over
    double fBreak = -1.0e18;   // a tick, repeat or tail was missed
    double fPressLimit = 180.0; // min(180, what is left of the slider)
    double fFadeLimit = 60.0;
    bool fTracking = false;
  };
  std::unordered_map<std::size_t, FollowState> fFollow;

  struct FadingObject {
    std::size_t fIndex;
    double fTime;
    osu::Judgement fResult;
  };
  struct ProfileFrame {
    double advUs, renderUs, flushUs, swapUs;
    double renderFollowUs, renderObjectsUs, renderRestUs, renderHudUs;
    // objs split by what was drawn, since one spinner and two hundred
    // circles cost nothing alike.
    double objCirclesUs, objSlidersUs, objSpinnersUs;
  };

  static constexpr double kPopupLifetime = 700.0;
  // Hit lighting outlives its judgement text: 200ms in, 200 held, 1000 out.
  static constexpr double kHitBurstLifetime = 1400.0;
  static constexpr double kCursorTrailLifetime = 140.0;
  // Bounded by how far the pointer travelled rather than by how many events
  // arrived: a trail's worth of path at this spacing is well under this.
  static constexpr std::size_t kCursorTrailMax = 256;
  static constexpr double kTrailSpacing = 2.0; // playfield units
  // LegacyMainCirclePiece's legacy_fade_duration, and DrawableHitCircle's
  // fade on a miss.
  static constexpr double kHitFade = 240.0;
  static constexpr double kMissFade = 100.0;
  static constexpr std::size_t kProfileCount = 60;
  // Anchored to the left edge: right-anchored text with numbers in it runs
  // off the window as soon as a number gets an extra digit, which is exactly
  // when it is worth reading.
  static constexpr float kProfileX = 12.0f;


  // Small geometry helpers the renderer needs; they only read the beatmap.
  [[nodiscard]] static osu::Vec2 objectPosition(const Ctx &c,
                                                std::size_t index) {
    if (index >= c.fMap->fObjects.size()) {
      return osu::kPlayfieldCenter;
    }
    return osu::objectPosition(c.fMap->fObjects[index]);
  }

  [[nodiscard]] static std::pair<osu::Vec2, double> objectEnd(const Ctx &c,
                                                              std::size_t i) {
    if (i >= c.fMap->fObjects.size()) {
      return {osu::kPlayfieldCenter, 0.0};
    }
    return osu::objectEnd(c.fMap->fObjects[i], *c.fMap);
  }

  // ---- Fed by the app ----------------------------------------------------
  void reset() {
    fPopups.clear();
    fHitBursts.clear();
    fCursorTrail.clear();
    fFadingObjects.clear();
    fDisplayHealth = 1.0;
    fDisplayScore = 0.0;
    fDisplayCombo = 0.0;
    fDisplayAccuracy = 1.0;
    fLastHudTime = 0.0;
    fFirstFrame = true;
    fCombo = 0;
  }

  void addJudgement(const osu::Judgement &result, std::size_t index,
                    osu::Vec2 pos, double now, std::size_t comboIndex,
                    bool countsCombo) {
    fPopups.push_back({result, now, pos});
    fFadingObjects.push_back({index, now, result});
    // Lighting is coloured by the judgement and is transparent for a miss or
    // a tick, so those simply do not produce one.
    if (countsCombo) {
      fHitBursts.push_back({pos, now, popupInfo(result).second});
    }
    // A miss that actually cost health starts the red display on the bar; the
    // glow hangs back at the old value for half a second before easing down.
    if (std::holds_alternative<osu::judgement::Miss>(result)) {
      fMissDisplayUntil = now + 500.0;
    } else {
      fFlashUntil = now + 330.0; // 30 ms to white, 300 ms back
    }
  }

  // One of a slider's nested objects was judged. `tail` is the slider's own
  // end, whose animation lazer plays at the slider's end time rather than at
  // the tail's, which sits 36ms earlier.
  void noteSliderNested(std::size_t index, bool tail, bool hit, double when) {
    auto &st = fFollow[index];
    if (!hit) {
      st.fBreak = when;
    } else if (tail) {
      st.fEnd = when;
    } else {
      st.fTick = when;
    }
  }

  // Tracking is a per-frame fact the engine knows; the moment it turns on is
  // not, so the edge is caught here. The grow and the fade are both cut short
  // by however little of the slider is left, which is what lazer does so that
  // a follow circle on a very short slider does not animate past its end.
  void updateFollowTracking(const Ctx &c, std::size_t index,
                            const osu::Slider &s, double now) {
    auto &st = fFollow[index];
    const bool tracking = c.fEngine->isTracking(index);
    if (tracking && !st.fTracking) {
      const double start = std::max(now, s.fTime);
      const double end = objectEnd(c, index).second;
      const double remaining = std::max(0.0, end - start);
      st.fPress = start;
      st.fPressLimit = std::max(1.0, std::min(180.0, remaining));
      st.fFadeLimit = std::max(1.0, std::min(60.0, remaining));
      st.fTick = -1.0e18;
      st.fEnd = -1.0e18;
      st.fBreak = -1.0e18;
    }
    st.fTracking = tracking;
  }

  // Where the follow circle is in its animation, in LegacyFollowCircle's own
  // units: 1 is the size it appears at, 2 the size it settles at while
  // tracking. Zero alpha means it is not drawn at all.
  [[nodiscard]] std::pair<float, float>
  followCircleState(const Ctx &c, std::size_t index, double now) {
    const auto it = fFollow.find(index);
    if (it == fFollow.end()) {
      return {0.0f, 0.0f};
    }
    const FollowState &st = it->second;
    const auto outQuad = [](double t) { return t * (2.0 - t); };
    const auto inQuad = [](double t) { return t * t; };

    if (st.fPress <= -1.0e17) {
      return {0.0f, 0.0f}; // never tracked, never shown
    }
    // OnSliderPress: 1 -> 2 eased out, fading in over a shorter window.
    const double sincePress = now - st.fPress;
    double scale =
        1.0 + outQuad(std::clamp(sincePress / st.fPressLimit, 0.0, 1.0));
    double alpha = std::clamp(sincePress / st.fFadeLimit, 0.0, 1.0);

    // OnSliderTick: a pop from 2.2 back down to 2, but only once the circle
    // has actually reached its tracking size.
    if (st.fTick > st.fPress && now >= st.fTick && scale >= 2.0) {
      const double t = std::clamp((now - st.fTick) / 200.0, 0.0, 1.0);
      scale = 2.2 - 0.2 * t;
    }
    // OnSliderBreak wins over everything: out to 4 and gone in 100ms.
    if (st.fBreak > -1.0e17 && now >= st.fBreak) {
      const double t = std::clamp((now - st.fBreak) / 100.0, 0.0, 1.0);
      scale = scale + (4.0 - scale) * t;
      alpha *= 1.0 - t;
    } else if (st.fEnd > -1.0e17 && now >= st.fEnd) {
      // OnSliderEnd: in to 1.6 eased out, fading out eased in.
      const double t = std::clamp((now - st.fEnd) / 200.0, 0.0, 1.0);
      const double from = scale;
      scale = from + (1.6 - from) * outQuad(t);
      alpha *= 1.0 - inQuad(t);
    }
    static_cast<void>(c);
    return {static_cast<float>(scale), static_cast<float>(alpha)};
  }

  // Points arrive at whatever rate the pointer reports, which is a thousand a
  // second on a gaming mouse and three a frame on a slow one. Neither is a
  // shape: what matters is that the samples cover the path, so they are kept
  // a couple of units apart and the rest are dropped. The curve is worked out
  // from them at draw time.
  void addTrailPoint(osu::Vec2 pos, double time) {
    if (!fCursorTrail.empty()) {
      const auto &last = fCursorTrail.back();
      const double dx = pos.fX - last.fPos.fX;
      const double dy = pos.fY - last.fPos.fY;
      if (dx * dx + dy * dy < kTrailSpacing * kTrailSpacing) {
        return;
      }
    }
    fCursorTrail.push_back({pos, time});
    while (!fCursorTrail.empty() &&
           time - fCursorTrail.front().fTime > kCursorTrailLifetime) {
      fCursorTrail.pop_front();
    }
    while (fCursorTrail.size() > kCursorTrailMax) {
      fCursorTrail.pop_front();
    }
  }

  void setCombo(int combo) noexcept { fCombo = combo; }
  void invalidate() noexcept { fFirstFrame = true; }
  void setBackground(skia::Sp<skia::SkImage> image) {
    fBackground = std::move(image);
  }
  [[nodiscard]] bool hasBackground() const noexcept {
    return static_cast<bool>(fBackgroundScaled);
  }
  [[nodiscard]] const skia::Sp<skia::SkImage> &background() const noexcept {
    return fBackgroundScaled;
  }
  [[nodiscard]] ProfileFrame &profileSlot() noexcept {
    return fProfile[fProfileIdx];
  }
  void advanceProfile() noexcept {
    fProfileIdx = (fProfileIdx + 1) % kProfileCount;
    if (fProfileNum < kProfileCount) {
      ++fProfileNum;
    }
  }


  void drawFollowPoints(const Ctx &c, skia::SkCanvas *canvas, double now, double ar,
                        double cs) {
    if (c.fMap->fObjects.size() < 2)
      return;
    // FollowPointConnection. The spacing is a constant in osu! units and is
    // not scaled by circle size; the distance it is stepped over is truncated
    // to a whole number of them before the fractions are taken.
    const double fadeIn = osu::fadeInTime(ar);
    const double preempt = osu::followPointPreempt(ar);
    const double spacing = osu::kFollowPointSpacing;
    for (std::size_t i = 0; i + 1 < c.fMap->fObjects.size(); ++i) {
      if ((*c.fCombo).fGroups[i] != (*c.fCombo).fGroups[i + 1])
        continue;
      if (std::holds_alternative<osu::Spinner>(c.fMap->fObjects[i]) ||
          std::holds_alternative<osu::Spinner>(c.fMap->fObjects[i + 1])) {
        continue;
      }
      const auto [startPos, startTime] = objectEnd(c, i);
      const osu::Vec2 endPos = objectPosition(c, i + 1);
      const double endTime = osu::startTime(c.fMap->fObjects[i + 1]);
      const osu::Vec2 dir = endPos - startPos;
      const double distance = std::floor(dir.length());
      if (distance <= 0.0)
        continue;
      const double angle = std::atan2(dir.fY, dir.fX);
      const double dt = endTime - startTime;
      for (double d = std::floor(spacing * 1.5); d < distance - spacing;
           d += spacing) {
        const double fraction = d / distance;
        const double fadeOutTime = startTime + dt * fraction;
        const double fadeInTime = fadeOutTime - preempt;
        if (now < fadeInTime)
          continue;
        // Position, scale and alpha all run over the fade-in from the moment
        // the point appears; the move and the scale are eased out, the fade
        // is not. The fade out starts where the fade times say and takes the
        // same time again.
        const double t = std::clamp((now - fadeInTime) / fadeIn, 0.0, 1.0);
        const double eased = t * (2.0 - t); // Easing.Out
        double rawAlpha = t;
        if (now >= fadeOutTime) {
          rawAlpha = std::clamp(1.0 - (now - fadeOutTime) / fadeIn, 0.0, 1.0);
        }
        if (rawAlpha <= 0.0)
          continue;
        const double drawFraction = fraction - 0.1 * (1.0 - eased);
        const osu::Vec2 p = startPos + dir * drawFraction;
        c.fSkin->drawFollowPoint(canvas, p, angle,
                                 static_cast<float>(rawAlpha), cs,
                                 static_cast<float>(1.5 - 0.5 * eased));
      }
    }
  }

  void drawHitBursts(const Ctx &c, skia::SkCanvas *canvas, double now, double cs) {
    // Nothing to draw either when the setting is off or when the skin has no
    // lighting element at all.
    if (!c.fHitLighting || !c.fSkin->hasHitLighting()) {
      fHitBursts.clear();
      return;
    }
    auto it = fHitBursts.begin();
    while (it != fHitBursts.end()) {
      const double age = now - it->fTime;
      if (age > kHitBurstLifetime) {
        it = fHitBursts.erase(it);
        continue;
      }
      c.fSkin->drawHitBurst(canvas, it->fPos, cs, age, it->fTint);
      ++it;
    }
  }

  void render(const Ctx &c, double now) {
    using clock = std::chrono::steady_clock;
    auto rt0 = clock::now();


    const double ar = c.fEngine->clockRate() > 0.0
                          ? c.fMap->fDiff.fAr * c.fEngine->clockRate()
                          : c.fMap->fDiff.fAr;
    const double cs = c.fMap->fDiff.fCs;
    const double od = c.fMap->fDiff.fOd;

    this->updateCursorTrail(c, now);

    auto dirty = this->computeDirtyBounds(c, now, ar, cs, od);
    if (!fFirstFrame)
      dirty.join(fDirtyBounds);
    else
      dirty = skia::SkIRect::MakeXYWH(0, 0, c.fScreenW, c.fScreenH);

    if (dirty.isEmpty())
      return;

    fDirtyBounds = dirty;
    fFirstFrame = false;

    auto *canvas = c.fCanvas;

    canvas->save();
    canvas->clipIRect(dirty);
    if (fBackgroundScaled) {
      this->drawBackground(c, canvas);
    } else {
      canvas->clear(skia::kBlack);
    }

    canvas->save();
    canvas->translate(c.fOffsetX, c.fOffsetY);
    canvas->scale(c.fScale, c.fScale);

    this->drawPlayfield(c, canvas);

    auto rta = clock::now();
    this->drawFollowPoints(c, canvas, now, ar, cs);
    auto rtb = clock::now();

    double circlesUs = 0.0, slidersUs = 0.0, spinnersUs = 0.0;
    for (std::size_t i = 0; i < c.fMap->fObjects.size(); ++i) {
      if (!c.fShowProfile) {
        this->drawObject(c, canvas, c.fMap->fObjects[i], i, now, ar, cs, od);
        continue;
      }
      const auto o0 = clock::now();
      this->drawObject(c, canvas, c.fMap->fObjects[i], i, now, ar, cs, od);
      const double spent = static_cast<double>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() -
                                                               o0)
              .count()) /
          1000.0;
      std::visit(osu::Overloaded{
                     [&](const osu::Circle &) { circlesUs += spent; },
                     [&](const osu::Slider &) { slidersUs += spent; },
                     [&](const osu::Spinner &) { spinnersUs += spent; },
                 },
                 c.fMap->fObjects[i]);
    }
    auto rtc = clock::now();

    this->drawFadingObjects(c, canvas, now, ar, cs, od);
    this->drawHitBursts(c, canvas, now, cs);
    this->drawPopups(c, canvas, now, cs);
    this->drawCursorTrail(c, canvas, now);
    this->drawCursor(c, canvas);
    canvas->restore();

    auto rtd = clock::now();
    this->drawHud(c, canvas, now);
    auto rte = clock::now();

    canvas->restore();

    if (c.fShowProfile) {
      auto &p = fProfile[fProfileIdx];
      p.renderFollowUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(rtb - rta)
              .count());
      p.renderObjectsUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(rtc - rtb)
              .count());
      p.renderRestUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(rtd - rtc)
              .count());
      p.renderHudUs = static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(rte - rtd)
              .count());
      p.objCirclesUs = circlesUs;
      p.objSlidersUs = slidersUs;
      p.objSpinnersUs = spinnersUs;
    }
  }

  [[nodiscard]] skia::SkIRect computeDirtyBounds(const Ctx &c, double now, double ar,
                                                 double cs, double od) const {
    skia::SkIRect dirty = skia::SkIRect::MakeEmpty();
    const float r = static_cast<float>(osu::circleRadius(cs)) * 1.05f;

    auto addPlayfieldPt = [&](float px, float py, float radius) {
      float sx = px * c.fScale + c.fOffsetX;
      float sy = py * c.fScale + c.fOffsetY;
      float sr = radius * c.fScale + 2.0f;
      dirty.join(skia::SkIRect::MakeLTRB(
          std::max(0, static_cast<int>(sx - sr)),
          std::max(0, static_cast<int>(sy - sr)),
          std::min(c.fScreenW, static_cast<int>(sx + sr + 1.0f)),
          std::min(c.fScreenH, static_cast<int>(sy + sr + 1.0f))));
    };

    for (std::size_t i = 0; i < c.fMap->fObjects.size(); ++i) {
      const auto &obj = c.fMap->fObjects[i];
      const double time = osu::startTime(obj);
      const double preempt = osu::preemptTime(ar);
      if (now < time - preempt)
        continue;
      if (c.fEngine->isJudged(i))
        continue;

      std::visit(osu::Overloaded{
                     [&](const osu::Circle &o) {
                       addPlayfieldPt(static_cast<float>(o.fPos.fX),
                                      static_cast<float>(o.fPos.fY), r * 5.0f);
                     },
                     [&](const osu::Slider &o) {
                       const auto &path = c.fMap->fSliderPaths[i];
                       for (const auto &pt : path.points()) {
                         addPlayfieldPt(static_cast<float>(pt.fX),
                                        static_cast<float>(pt.fY), r);
                       }
                       addPlayfieldPt(static_cast<float>(o.fPos.fX),
                                      static_cast<float>(o.fPos.fY), r * 5.0f);
                     },
                     [&](const osu::Spinner &) {
                       addPlayfieldPt(
                           static_cast<float>(osu::kPlayfieldCenter.fX),
                           static_cast<float>(osu::kPlayfieldCenter.fY),
                           static_cast<float>(osu::kPlayfieldHeight / 2.0 *
                                              1.3 * 1.2));
                     },
                 },
                 obj);
    }

    for (const auto &fo : fFadingObjects) {
      const auto &obj = c.fMap->fObjects[fo.fIndex];
      std::visit(osu::Overloaded{
                     [&](const osu::Circle &o) {
                       addPlayfieldPt(static_cast<float>(o.fPos.fX),
                                      static_cast<float>(o.fPos.fY), r * 4.0f);
                     },
                     [&](const osu::Slider &o) {
                       const auto &path = c.fMap->fSliderPaths[fo.fIndex];
                       for (const auto &pt : path.points()) {
                         addPlayfieldPt(static_cast<float>(pt.fX),
                                        static_cast<float>(pt.fY), r);
                       }
                       addPlayfieldPt(static_cast<float>(o.fPos.fX),
                                      static_cast<float>(o.fPos.fY), r * 4.0f);
                     },
                     [&](const osu::Spinner &) {},
                 },
                 obj);
    }

    // Lighting lives for 1400ms, so a map at speed keeps a dozen of these
    // alive at once and their union is most of the playfield -- which throws
    // away partial redraws and repaints the background every frame. Not worth
    // paying for when there is nothing to draw.
    if (c.fHitLighting && c.fSkin->hasHitLighting()) {
      for (const auto &hb : fHitBursts)
        addPlayfieldPt(static_cast<float>(hb.fPos.fX),
                       static_cast<float>(hb.fPos.fY), r * 2.5f);
    }

    for (const auto &pp : fPopups)
      addPlayfieldPt(static_cast<float>(pp.fPos.fX),
                     static_cast<float>(pp.fPos.fY), 60.0f);

    for (const auto &pt : fCursorTrail)
      addPlayfieldPt(static_cast<float>(pt.fPos.fX),
                     static_cast<float>(pt.fPos.fY), 24.0f);

    addPlayfieldPt(static_cast<float>(c.fCursor.fX),
                   static_cast<float>(c.fCursor.fY), 24.0f);

    for (std::size_t i = 0; i + 1 < c.fMap->fObjects.size(); ++i) {
      const double endTime = osu::startTime(c.fMap->fObjects[i + 1]);
      const double preempt = osu::preemptTime(ar);
      if (now < endTime - preempt)
        continue;
      if (now > endTime)
        continue;
      const auto [startPos, startTime] = objectEnd(c, i);
      const osu::Vec2 endPos = objectPosition(c, i + 1);
      addPlayfieldPt(static_cast<float>(startPos.fX),
                     static_cast<float>(startPos.fY), 10.0f);
      addPlayfieldPt(static_cast<float>(endPos.fX),
                     static_cast<float>(endPos.fY), 10.0f);
    }

    dirty.join(skia::SkIRect::MakeXYWH(0, 0, c.fScreenW, 160));

    // The frame breakdown sits in the opposite corner from the HUD strip
    // above, and changes every frame. Without saying so it would be clipped
    // away and never appear -- which it was not, only because the path that
    // drew it was not applying the clip properly either.
    if (c.fShowProfile) {
      dirty.join(skia::SkIRect::MakeLTRB(0, c.fScreenH - 140, c.fScreenW,
                                         c.fScreenH));
    }

    return dirty;
  }

  void drawBackground(const Ctx &c, skia::SkCanvas *canvas) {
    if (!fBackgroundScaled)
      return;
    skia::SkPaint paint;
    paint.setAntiAlias(false);
    paint.setBlendMode(skia::SkBlendMode::kSrc);
    canvas->drawImageRect(
        fBackgroundScaled.get(),
        skia::SkRect::MakeXYWH(0, 0, static_cast<float>(c.fScreenW),
                               static_cast<float>(c.fScreenH)),
        skia::SkSamplingOptions(skia::SkFilterMode::kNearest), &paint);
  }

  void preScaleBackground(const Ctx &c) {
    fBackgroundScaled.reset();
    if (!fBackground)
      return;
    const int sw = c.fScreenW;
    const int sh = c.fScreenH;
    if (sw <= 0 || sh <= 0)
      return;
    const float iw = static_cast<float>(fBackground->width());
    const float ih = static_cast<float>(fBackground->height());
    if (iw <= 0.0f || ih <= 0.0f)
      return;

    const float scale =
        std::max(static_cast<float>(sw) / iw, static_cast<float>(sh) / ih);
    const float dw = iw * scale;
    const float dh = ih * scale;
    const float dx = (static_cast<float>(sw) - dw) * 0.5f;
    const float dy = (static_cast<float>(sh) - dh) * 0.5f;

    skia::SkBitmap bmp;
    if (!bmp.tryAllocPixels(skia::SkImageInfo::Make(
            sw, sh, skia::kRGBA_8888_SkColorType, skia::kPremul_SkAlphaType)))
      return;
    bmp.eraseColor(skia::kBlack);
    skia::SkCanvas offscreen(bmp);
    skia::SkPaint paint;
    paint.setAntiAlias(false);
    paint.setAlphaf(1.0f - c.fDim);
    offscreen.drawImageRect(
        fBackground.get(), skia::SkRect::MakeXYWH(dx, dy, dw, dh),
        skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
    fBackgroundScaled = skia::RasterFromBitmap(bmp);
  }

  void drawPlayfield(const Ctx &c, skia::SkCanvas *) {
    // No visible playfield border.
  }

  void drawObject(const Ctx &c, skia::SkCanvas *canvas, const osu::HitObject &obj,
                  std::size_t index, double now, double ar, double cs,
                  double od) {
    const double time = osu::startTime(obj);
    const double preempt = osu::preemptTime(ar);
    if (now < time - preempt) {
      return;
    }
    if (c.fEngine->isJudged(index)) {
      return;
    }

    std::visit(osu::Overloaded{
                   [&](const osu::Circle &o) {
                     c.fSkin->drawHitCircle(canvas, o.fPos, time, now, cs, ar,
                                         o.fCombo, (*c.fCombo).fIndices[index]);
                   },
                   [&](const osu::Slider &o) {
                     this->updateFollowTracking(c, index, o, now);
                     const auto [fs, fa] =
                         this->followCircleState(c, index, now);
                     c.fSkin->drawSlider(canvas, o, index,
                                      c.fMap->fSliderPaths[index],
                                      c.fMap->sliderSpanDuration(o),
                                      c.fMap->sliderTickDistance(o), now, cs, ar,
                                      od, o.fCombo, (*c.fCombo).fIndices[index],
                                      1.0f, fs, fa);
                   },
                   [&](const osu::Spinner &o) {
                     this->drawSpinner(c, canvas, o, index, now, cs, od);
                   },
               },
               obj);
  }

  void drawFadingObjects(const Ctx &c, skia::SkCanvas *canvas, double now, double ar,
                         double cs, double od) {
    auto it = fFadingObjects.begin();
    while (it != fFadingObjects.end()) {
      const double age = now - it->fTime;
      // LegacyMainCirclePiece fades a hit object out over 240ms while growing
      // it to 1.4; DrawableHitCircle fades a missed one out over 100ms and
      // leaves it the size it was.
      const bool missed =
          std::holds_alternative<osu::judgement::Miss>(it->fResult);
      const double lifetime = missed ? kMissFade : kHitFade;
      if (age > lifetime) {
        it = fFadingObjects.erase(it);
        continue;
      }
      const double t = age / lifetime;
      const float alpha = static_cast<float>(1.0 - t);
      const float sizeScale =
          missed ? 1.0f : static_cast<float>(1.0 + 0.4 * t * (2.0 - t));
      std::visit(
          osu::Overloaded{
              [&](const osu::Circle &o) {
                c.fSkin->drawHitCircle(canvas, o.fPos, o.fTime, now, cs, ar,
                                    o.fCombo, (*c.fCombo).fIndices[it->fIndex],
                                    alpha, sizeScale, /*withApproach=*/false);
              },
              [&](const osu::Slider &o) {
                const auto [fs, fa] =
                    this->followCircleState(c, it->fIndex, now);
                c.fSkin->drawSlider(
                    canvas, o, it->fIndex, c.fMap->fSliderPaths[it->fIndex],
                    c.fMap->sliderSpanDuration(o), c.fMap->sliderTickDistance(o),
                    now, cs, ar, od, o.fCombo, (*c.fCombo).fIndices[it->fIndex],
                    alpha, fs, fa);
              },
              [&](const osu::Spinner &) {},
          },
          c.fMap->fObjects[it->fIndex]);
      ++it;
    }
  }

  // DefaultSpinnerDisc. The disc is the height of the playfield, a third
  // again as large so that its top and bottom clip, and it arrives in two
  // steps over the preempt rather than simply appearing.
  void drawSpinner(const Ctx &c, skia::SkCanvas *canvas, const osu::Spinner &s,
                   std::size_t index, double now, double cs, double od) {
    const float cx = static_cast<float>(osu::kPlayfieldCenter.fX);
    const float cy = static_cast<float>(osu::kPlayfieldCenter.fY);
    constexpr double kInitialScale = 1.3;
    constexpr double kInitialFillScale = 0.2;
    constexpr double kIdleAlpha = 0.2;
    constexpr double kTrackingAlpha = 0.4;
    const double half = osu::kPlayfieldHeight / 2.0;

    const double preempt = osu::preemptTime(c.fMap->fDiff.fAr);
    const double appear = s.fTime - preempt;
    const double duration = std::max(0.0, s.fEnd - s.fTime);
    const auto outQuint = [](double t) {
      const double inv = 1.0 - t;
      return 1.0 - inv * inv * inv * inv * inv;
    };
    const auto phase = [now](double from, double length) {
      if (length <= 0.0)
        return 1.0;
      return std::clamp((now - from) / length, 0.0, 1.0);
    };

    // The main container goes 0 -> 0.2 over a quarter of the preempt starting
    // halfway through it, then 0.2 -> 1 over half the preempt from the start
    // time; the centre goes 0 -> 0.3 -> 0.5 on the same clock.
    double main = 0.0;
    double centre = 0.0;
    if (now >= appear + preempt * 0.5) {
      main = 0.2 * outQuint(phase(appear + preempt * 0.5, preempt * 0.25));
      centre = 0.3 * outQuint(phase(appear + preempt * 0.5, preempt * 0.25));
    }
    if (now >= s.fTime) {
      const double t = outQuint(phase(s.fTime, preempt * 0.5));
      main = 0.2 + 0.8 * t;
      centre = 0.3 + 0.2 * t;
    }

    // Cleared or missed, the whole disc takes 320ms to grow or shrink.
    double discScale = kInitialScale;
    if (now > s.fEnd) {
      const double t = phase(s.fEnd, 320.0);
      const bool cleared = c.fEngine->isJudged(index)
                               ? c.fEngine->spinnerRotations(index) >=
                                     osu::spinsRequired(duration, od)
                               : false;
      const double target =
          cleared ? kInitialScale * 1.2 : kInitialScale * 0.8;
      const double eased = cleared ? t * (2.0 - t) : t * t;
      discScale = kInitialScale + (target - kInitialScale) * eased;
    }

    // A constant ambient rotation, so the disc reads as spinning even when it
    // is not being turned: 25 degrees per two seconds of spinner.
    double rotation = 0.0;
    if (now > appear + preempt * 0.5) {
      const double total = 25.0 * duration / 2000.0;
      rotation = total * phase(appear + preempt * 0.5, preempt + duration);
    }

    const double radius = half * discScale * main;

    const double progress =
        now < s.fTime
            ? 0.0
            : std::clamp(osu::spinnerProgress(c.fEngine->spinnerRotations(index),
                                              duration, od),
                         0.0, 1.0);
    const double fill =
        kInitialFillScale + (1.0 - kInitialFillScale) * progress;
    const float fillAlpha = static_cast<float>(
        c.fEngine->isTracking(index) ? kTrackingAlpha : kIdleAlpha);
    // DefaultSpinnerDisc carries Scale = 1.3 on itself, so it multiplies the
    // centre layer as well as the disc -- and so does the grow or shrink at
    // the end. Applying it only to the disc, which is what this did, left the
    // centre a third smaller than it should be.
    c.fSkin->drawSpinner(canvas, cx, cy, radius, progress, fill, fillAlpha,
                         rotation, centre * discScale,
                         c.fEngine->spinnerAngle(index));

    // No counter in the middle: that was mine, the game has nothing like it,
    // and it sits exactly where the centre piece is.
  }

  // The frame's own contribution, which matters only when nothing is feeding
  // the trail from events -- a replay whose frames are sparse, or a pointer
  // sitting still. Routed through the same door so the spacing rule and the
  // ageing live in one place.
  void updateCursorTrail(const Ctx &c, double now) {
    this->addTrailPoint(c.fCursor, now);
    while (!fCursorTrail.empty() &&
           now - fCursorTrail.front().fTime > kCursorTrailLifetime) {
      fCursorTrail.pop_front();
    }
  }

  // Centripetal Catmull-Rom through the samples, evaluated every `step`
  // units. Alpha rides along the same parameter, so the fade stays attached
  // to the sample it came from.
  template <typename Pt>
  [[nodiscard]] static std::vector<Pt> resampleTrail(const std::vector<Pt> &in,
                                                     float step) {
    if (in.size() < 2) {
      return in;
    }
    const auto knotGap = [](const Pt &a, const Pt &b) {
      const float dx = b.fX - a.fX;
      const float dy = b.fY - a.fY;
      // alpha = 0.5: the centripetal parameterisation.
      return std::max(1e-4f, std::sqrt(std::sqrt(dx * dx + dy * dy)));
    };
    std::vector<Pt> out;
    out.reserve(in.size() * 8);
    for (std::size_t i = 0; i + 1 < in.size(); ++i) {
      // The two neighbours the span is shaped by; the ends stand in for
      // themselves so the curve starts and finishes where the samples do.
      const Pt &p0 = in[i > 0 ? i - 1 : 0];
      const Pt &p1 = in[i];
      const Pt &p2 = in[i + 1];
      const Pt &p3 = in[i + 2 < in.size() ? i + 2 : in.size() - 1];

      const float t0 = 0.0f;
      const float t1 = t0 + knotGap(p0, p1);
      const float t2 = t1 + knotGap(p1, p2);
      const float t3 = t2 + knotGap(p2, p3);

      const float dx = p2.fX - p1.fX;
      const float dy = p2.fY - p1.fY;
      const float chord = std::sqrt(dx * dx + dy * dy);
      const int steps =
          std::clamp(static_cast<int>(std::ceil(chord / std::max(0.25f, step))),
                     1, 64);
      for (int k = 0; k < steps; ++k) {
        const float u = static_cast<float>(k) / static_cast<float>(steps);
        const float t = t1 + u * (t2 - t1);
        // De Boor's triangle, which is the readable way to say Catmull-Rom
        // for arbitrary knots.
        const auto lerpPt = [](const Pt &a, const Pt &b, float w) {
          Pt r = a;
          r.fX = a.fX + (b.fX - a.fX) * w;
          r.fY = a.fY + (b.fY - a.fY) * w;
          r.fAlpha = a.fAlpha + (b.fAlpha - a.fAlpha) * w;
          return r;
        };
        const Pt a1 = lerpPt(p0, p1, (t - t0) / std::max(1e-4f, t1 - t0));
        const Pt a2 = lerpPt(p1, p2, (t - t1) / std::max(1e-4f, t2 - t1));
        const Pt a3 = lerpPt(p2, p3, (t - t2) / std::max(1e-4f, t3 - t2));
        const Pt b1 = lerpPt(a1, a2, (t - t0) / std::max(1e-4f, t2 - t0));
        const Pt b2 = lerpPt(a2, a3, (t - t1) / std::max(1e-4f, t3 - t1));
        out.push_back(lerpPt(b1, b2, (t - t1) / std::max(1e-4f, t2 - t1)));
      }
    }
    out.push_back(in.back());
    return out;
  }

  void drawCursorTrail(const Ctx &c, skia::SkCanvas *canvas, double now) {
    const float scale = 1.0f / c.fScale;
    const bool hasImg = static_cast<bool>(c.fSkin->cursorTrail());

    struct TrailPt {
      float fX, fY;
      float fAlpha;
    };
    std::vector<TrailPt> raw;
    raw.reserve(fCursorTrail.size());
    for (const auto &p : fCursorTrail) {
      const double age = now - p.fTime;
      if (age > kCursorTrailLifetime)
        continue;
      const float alpha =
          static_cast<float>((1.0 - age / kCursorTrailLifetime) * 0.6);
      const float x = static_cast<float>(p.fPos.fX);
      const float y = static_cast<float>(p.fPos.fY);
      if (!raw.empty()) {
        const float dx = x - raw.back().fX;
        const float dy = y - raw.back().fY;
        if (dx * dx + dy * dy < 1e-6f)
          continue; // duplicate point => degenerate tangent
      }
      raw.push_back({x, y, alpha});
    }
    if (raw.size() < 2)
      return;

    // The samples are knots on a curve, not the curve. Cutting corners off the
    // polygon they form -- which is what a round of Chaikin did here -- gets
    // smoother the more of them there are, and at twenty frames a second
    // there are three. So the curve is worked out instead and then walked at
    // a fixed step in device pixels: how smooth it looks stops depending on
    // how many points happened to arrive.
    //
    // Centripetal Catmull-Rom: it passes through every sample, and unlike the
    // uniform kind it cannot loop or cusp when the samples are unevenly
    // spaced, which is exactly what a pointer that speeds up and slows down
    // produces.
    const std::vector<TrailPt> pts = this->resampleTrail(raw, 2.0f * scale);
    if (pts.size() < 2)
      return;

    const float baseW = hasImg ? 6.0f * scale : 12.0f * scale;
    const float feather = 1.5f * scale; // ~1.5 device px of edge fade
    const std::size_t n = pts.size();

    // Four vertices per point: outer-left (alpha 0), inner-left, inner-right
    // (full alpha), outer-right (alpha 0). The solid core keeps the visual
    // width of the old stroked trail; only a narrow margin feathers to zero,
    // which is what provides the antialiasing.
    std::vector<skia::SkPoint> pos(n * 4);
    std::vector<skia::SkColor> col(n * 4);
    for (std::size_t i = 0; i < n; ++i) {
      const auto &prev = pts[i == 0 ? 0 : i - 1];
      const auto &next = pts[i + 1 < n ? i + 1 : n - 1];
      float tx = next.fX - prev.fX;
      float ty = next.fY - prev.fY;
      const float len = std::sqrt(tx * tx + ty * ty);
      if (len > 1e-6f) {
        tx /= len;
        ty /= len;
      } else {
        tx = 1.0f;
        ty = 0.0f;
      }
      // Solid half-width tapers with the same profile as the old stroke
      // width (which was the *total* width => halve it here).
      const float t =
          static_cast<float>(i + 1) / static_cast<float>(n);
      const float w = 0.5f * baseW * (0.12f + 0.88f * t * (2.0f - t));
      const float nxCore = -ty * w;
      const float nyCore = tx * w;
      const float nxOut = -ty * (w + feather);
      const float nyOut = tx * (w + feather);

      const auto a8 = static_cast<std::uint8_t>(
          std::clamp(pts[i].fAlpha, 0.0f, 1.0f) * 255.0f + 0.5f);
      pos[i * 4 + 0] = {pts[i].fX + nxOut, pts[i].fY + nyOut};
      pos[i * 4 + 1] = {pts[i].fX + nxCore, pts[i].fY + nyCore};
      pos[i * 4 + 2] = {pts[i].fX - nxCore, pts[i].fY - nyCore};
      pos[i * 4 + 3] = {pts[i].fX - nxOut, pts[i].fY - nyOut};
      col[i * 4 + 0] = skia::colorSetARGB(0, 255, 255, 255);
      col[i * 4 + 1] = skia::colorSetARGB(a8, 255, 255, 255);
      col[i * 4 + 2] = skia::colorSetARGB(a8, 255, 255, 255);
      col[i * 4 + 3] = skia::colorSetARGB(0, 255, 255, 255);
    }

    std::vector<std::uint16_t> idx;
    idx.reserve((n - 1) * 18);
    for (std::size_t i = 0; i + 1 < n; ++i) {
      const auto base0 = static_cast<std::uint16_t>(i * 4);
      const auto base1 = static_cast<std::uint16_t>((i + 1) * 4);
      for (std::uint16_t band = 0; band < 3; ++band) {
        const auto a0 = static_cast<std::uint16_t>(base0 + band);
        const auto b0 = static_cast<std::uint16_t>(base0 + band + 1);
        const auto a1 = static_cast<std::uint16_t>(base1 + band);
        const auto b1 = static_cast<std::uint16_t>(base1 + band + 1);
        idx.insert(idx.end(), {a0, b0, a1, a1, b0, b1});
      }
    }

    auto verts = skia::SkVertices::MakeCopy(
        skia::SkVertices::kTriangles_VertexMode, static_cast<int>(pos.size()),
        pos.data(), nullptr, col.data(), static_cast<int>(idx.size()),
        idx.data());

    skia::SkPaint paint;
    if (hasImg && !c.fNoGlow)
      paint.setBlendMode(skia::SkBlendMode::kPlus);
    // No shader on the paint: kDst keeps the interpolated vertex colors.
    canvas->drawVertices(verts, skia::SkBlendMode::kDst, paint);

    if (hasImg && !c.fNoGlow) {
      c.fSkin->drawCursorTrail(canvas, fCursorTrail.back().fPos, scale, 0.6f);
    }
  }

  void drawCursor(const Ctx &c, skia::SkCanvas *canvas) {
    c.fSkin->drawCursor(canvas, c.fCursor,
                     c.fCursorSize / c.fScale);
  }

  // Judgement text. DefaultJudgementPiece and, for anything that is not a
  // miss, OsuJudgementPiece over it. This was ported from webosu-2 before,
  // which faked the sideways stretch with letter spacing and gave both
  // results a fade in and a hold that lazer does not have.
  void drawPopups(const Ctx &c, skia::SkCanvas *canvas, double now,
                  double cs) {
    // DrawableOsuJudgement carries Scale = HitObject.Scale, so the whole
    // judgement is drawn at the size of the circles on this map. Dropping
    // that, which is what porting these to lazer's numbers did, left the text
    // at nearly twice the size it should be.
    const double objectScale = osu::circleRadius(cs) / 60.0;
    auto it = fPopups.begin();
    while (it != fPopups.end()) {
      const double age = now - it->fTime;
      const bool isMiss =
          std::holds_alternative<osu::judgement::Miss>(it->fResult);
      // DefaultJudgementPiece: both live for 800ms and fade from full
      // opacity over the whole of it, with no fade in and no hold.
      constexpr double kLifetime = 800.0;
      if (age > kLifetime) {
        it = fPopups.erase(it);
        continue;
      }

      constexpr float kBaseSize = 20.0f;
      double alpha = std::clamp(1.0 - age / kLifetime, 0.0, 1.0);
      float yOffset = 0.0f;
      float rotation = 0.0f;
      float stretch = 1.0f;
      float uniform = 1.0f;
      if (isMiss) {
        // Snaps down from 1.6 over 100ms eased in, then drops away and turns
        // over the rest of its life, both eased in hard.
        const double snap = std::clamp(age / 100.0, 0.0, 1.0);
        uniform = static_cast<float>(1.6 - 0.6 * (snap * snap));
        const double k = std::pow(age / kLifetime, 5.0); // Easing.InQuint
        yOffset = static_cast<float>(100.0 * k);
        rotation = static_cast<float>(40.0 * k * std::numbers::pi / 180.0);
      } else {
        // OsuJudgementPiece stretches the text sideways from 0.8 to 1.2 over
        // 1800ms eased out -- far longer than the 800ms it is visible for, so
        // only the first part of that curve is ever seen.
        const double t = std::clamp(age / 1800.0, 0.0, 1.0);
        const double eased = 1.0 - std::pow(1.0 - t, 5.0); // Easing.OutQuint
        stretch = static_cast<float>(0.8 + 0.4 * eased);
      }

      const auto [label, color] = popupInfo(it->fResult);
      // Venera is an all-caps display face; the same words in a normal face
      // have to be set in capitals to read the same way.
      std::string str(label);
      std::ranges::transform(str, str.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
      });

      const float x = static_cast<float>(it->fPos.fX);
      const float y = static_cast<float>(it->fPos.fY) + yOffset;

      skia::SkFont &font = c.fDisplayFont ? *c.fDisplayFont : *c.fFont;
      font.setSize(kBaseSize);

      const float total =
          font.measureText(str.data(), str.size(), skia::SkTextEncoding::kUTF8);

      // anchor 0.5 centres vertically too, so shift by half the cap height
      // rather than sitting on the baseline.
      skia::SkFontMetrics metrics;
      font.getMetrics(&metrics);
      const float centreOffset = -(metrics.fAscent + metrics.fDescent) * 0.5f;

      skia::SkPaint paint;
      paint.setAntiAlias(true);
      paint.setColor(color);
      paint.setAlphaf(static_cast<float>(alpha));
      if (!c.fDisplayFont) {
        // Without the heavy face, thicken the light UI font a little.
        paint.setStyle(skia::kStrokeAndFillStyle);
        paint.setStrokeWidth(kBaseSize * 0.06f);
        paint.setStrokeJoin(skia::kRoundJoin);
      }

      canvas->save();
      canvas->translate(x, y);
      if (rotation != 0.0f) {
        canvas->rotate(rotation * 180.0f / std::numbers::pi_v<float>);
      }
      canvas->scale(static_cast<float>(objectScale) * uniform * stretch,
                    static_cast<float>(objectScale) * uniform);
      canvas->drawSimpleText(str.data(), str.size(),
                             skia::SkTextEncoding::kUTF8, -total * 0.5f,
                             centreOffset, font, paint);
      canvas->restore();
      ++it;
    }
  }

  // OsuColour.ForHitResult: great is Blue 66ccff, ok Green 88b300, meh
  // Yellow ffcc22, miss Red ed1121. These reached here through webosu-2,
  // which took them from the same place.
  [[nodiscard]] static std::pair<const char *, skia::SkColor>
  popupInfo(const osu::Judgement &j) {
    return std::visit(
        osu::Overloaded{
            [](osu::judgement::Great)
                -> std::pair<const char *, skia::SkColor> {
              return {"great", skia::colorSetARGB(255, 0x66, 0xcc, 0xff)};
            },
            [](osu::judgement::Good)
                -> std::pair<const char *, skia::SkColor> {
              return {"good", skia::colorSetARGB(255, 0x88, 0xb3, 0x00)};
            },
            [](osu::judgement::Meh) -> std::pair<const char *, skia::SkColor> {
              return {"meh", skia::colorSetARGB(255, 0xff, 0xcc, 0x22)};
            },
            [](osu::judgement::Miss)
                -> std::pair<const char *, skia::SkColor> {
              return {"miss", skia::colorSetARGB(255, 0xed, 0x11, 0x21)};
            },
        },
        j);
  }

  void drawHud(const Ctx &c, skia::SkCanvas *canvas, double now) {
    const auto &score = c.fEngine->score();
    const float sw = static_cast<float>(c.fScreenW);
    const float sh = static_cast<float>(c.fScreenH);

    if (fLastHudTime == 0.0)
      fLastHudTime = now;
    const double dt = now - fLastHudTime;
    fLastHudTime = now;
    constexpr double kLazyLag = 200.0;
    const double lagFactor = 1.0 - std::exp(-dt / kLazyLag);

    // ArgonHealthDisplay. The bar does not show the health, it chases it:
    // Interpolation.DampContinuously with a half life of 50 ms. The glow bar
    // chases it too, except while a miss is being shown -- then it hangs back
    // at where the health used to be, and the gap between the two is the red
    // that tells you what you just lost.
    const auto damp = [dt](double current, double target, double halfLife) {
      return current +
             (target - current) * (1.0 - std::pow(0.5, dt / halfLife));
    };
    fHealthBarValue = damp(fHealthBarValue, score.fHealth, 50.0);
    if (now >= fMissDisplayUntil) {
      if (fMissDisplayUntil > 0.0) {
        // The 300 ms ease back to the current health once the miss has been
        // shown for half a second.
        fGlowBarValue = damp(fGlowBarValue, score.fHealth, 100.0);
        if (std::abs(fGlowBarValue - score.fHealth) < 1e-4) {
          fMissDisplayUntil = 0.0;
          fGlowBarValue = score.fHealth;
        }
      } else {
        fGlowBarValue = damp(fGlowBarValue, score.fHealth, 50.0);
      }
    }
    fFlashUntil = std::max(fFlashUntil, 0.0);
    fDisplayHealth += (score.fHealth - fDisplayHealth) * lagFactor;
    fDisplayScore +=
        (static_cast<double>(score.fScore) - fDisplayScore) * lagFactor;
    fDisplayCombo +=
        (static_cast<double>(score.fCombo) - fDisplayCombo) * lagFactor;
    fDisplayAccuracy += (score.accuracy() - fDisplayAccuracy) * lagFactor;

    fHudPaint.setColor(skia::kWhite);
    fHudPaint.setAlphaf(1.0f);

    // Everything in the corner is written against a 1080-tall screen and
    // scaled from there, so a render at another size gets the same interface
    // rather than the same number of pixels.
    const float ui = c.fUiScale;

    // Combo counter (large, top-left).
    (*c.fFont).setSize(48.0f * ui);
    const std::string comboText =
        std::format("{:.0f}x", std::max(0.0, fDisplayCombo));
    client::ui::fonts().draw(canvas, *c.fFont, comboText, 20.0f * ui,
                             60.0f * ui, fHudPaint);

    // Score, accuracy, grade (top-center).
    (*c.fFont).setSize(22.0f * ui);
    const std::string statsText =
        std::format("{:.0f}  {:.2f}%  {}", fDisplayScore,
                    std::clamp(fDisplayAccuracy, 0.0, 1.0) * 100.0,
                    osu::gradeString(osu::computeGrade(score)));
    client::ui::fonts().draw(canvas, *c.fFont, statsText, 20.0f * ui,
                             90.0f * ui, fHudPaint);

    // Difficulty / mods (top-right).
    (*c.fFont).setSize(16.0f * ui);
    const std::string diffText = std::format(
        "CS:{:.1f} AR:{:.1f} OD:{:.1f} HP:{:.1f} {}", c.fMap->fDiff.fCs,
        c.fMap->fDiff.fAr, c.fMap->fDiff.fOd, c.fMap->fDiff.fHp, c.fEngine->mods());
    client::ui::fonts().draw(canvas, *c.fFont, diffText, 20.0f * ui,
                             115.0f * ui, fHudPaint);

    // Health bar (top).
    this->drawHealthBar(c, canvas, 0.0f, 0.0f, sw, 14.0f * ui, now);

    // Judgement counts.
    (*c.fFont).setSize(16.0f * ui);
    const std::string countsText =
        std::format("Great {}  Good {}  Meh {}  Miss {}", score.fGreat,
                    score.fGood, score.fMeh, score.fMiss);
    client::ui::fonts().draw(canvas, *c.fFont, countsText, 20.0f * ui,
                             140.0f * ui, fHudPaint);

    // Time since the map started, top right. It used to sit in the bottom
    // right, which is where the frame counter is, and the two drew over each
    // other. Right-aligned, so the number growing a digit does not shift it.
    (*c.fFont).setSize(14.0f * ui);
    fHudPaint.setAlphaf(0.7f);
    const std::string timeText = std::format("{:.1f}s", now / 1000.0);
    const float timeWidth = client::ui::fonts().measure(*c.fFont, timeText);
    client::ui::fonts().draw(canvas, *c.fFont, timeText,
                             sw - timeWidth - 20.0f * ui, 34.0f * ui,
                             fHudPaint);

    if (c.fShowProfile) {
      double avgAdv = 0.0, avgRender = 0.0, avgFlush = 0.0, avgSwap = 0.0;
      double avgFollow = 0.0, avgObjs = 0.0, avgRest = 0.0, avgHud = 0.0;
      if (fProfileNum > 0) {
        for (std::size_t i = 0; i < fProfileNum; ++i) {
          avgAdv += fProfile[i].advUs;
          avgRender += fProfile[i].renderUs;
          avgFlush += fProfile[i].flushUs;
          avgSwap += fProfile[i].swapUs;
          avgFollow += fProfile[i].renderFollowUs;
          avgObjs += fProfile[i].renderObjectsUs;
          avgRest += fProfile[i].renderRestUs;
          avgHud += fProfile[i].renderHudUs;
        }
        avgAdv /= static_cast<double>(fProfileNum);
        avgRender /= static_cast<double>(fProfileNum);
        avgFlush /= static_cast<double>(fProfileNum);
        avgSwap /= static_cast<double>(fProfileNum);
        avgFollow /= static_cast<double>(fProfileNum);
        avgObjs /= static_cast<double>(fProfileNum);
        avgRest /= static_cast<double>(fProfileNum);
        avgHud /= static_cast<double>(fProfileNum);
      }
      // Averages hide the thing that is actually felt. A single 40ms frame in
      // sixty adds half a millisecond to the mean and is invisible there,
      // while on screen it is a visible hitch and, with vsync, three missed
      // deadlines. So the worst frame in the window is reported beside the
      // mean, per stage, and the stage that owns it is named.
      double maxAdv = 0.0, maxRender = 0.0, maxFlush = 0.0, maxSwap = 0.0;
      double maxFollow = 0.0, maxObjs = 0.0, maxRest = 0.0, maxHud = 0.0;
      double maxFrame = 0.0;
      for (std::size_t i = 0; i < fProfileNum; ++i) {
        const auto &f = fProfile[i];
        maxAdv = std::max(maxAdv, f.advUs);
        maxRender = std::max(maxRender, f.renderUs);
        maxFlush = std::max(maxFlush, f.flushUs);
        maxSwap = std::max(maxSwap, f.swapUs);
        maxFollow = std::max(maxFollow, f.renderFollowUs);
        maxObjs = std::max(maxObjs, f.renderObjectsUs);
        maxRest = std::max(maxRest, f.renderRestUs);
        maxHud = std::max(maxHud, f.renderHudUs);
        maxFrame = std::max(maxFrame,
                            f.advUs + f.renderUs + f.flushUs + f.swapUs);
      }

      (*c.fFont).setSize(11.0f);
      fHudPaint.setAlphaf(0.6f);
      const std::string profText =
          std::format("adv {:.0f}  rend {:.0f}  flush {:.0f}  swap {:.0f} us",
                      avgAdv, avgRender, avgFlush, avgSwap);
      client::ui::fonts().draw(canvas, *c.fFont, profText, kProfileX,
                               sh - 60.0f, fHudPaint);
      const std::string subText =
          std::format("follow {:.0f}  objs {:.0f}  rest {:.0f}  hud {:.0f} us",
                      avgFollow, avgObjs, avgRest, avgHud);
      client::ui::fonts().draw(canvas, *c.fFont, subText, kProfileX,
                               sh - 75.0f, fHudPaint);
      const std::string maxText = std::format(
          "worst {:.0f} us = adv {:.0f} rend {:.0f} flush {:.0f} swap {:.0f}",
          maxFrame, maxAdv, maxRender, maxFlush, maxSwap);
      client::ui::fonts().draw(canvas, *c.fFont, maxText, kProfileX,
                               sh - 90.0f, fHudPaint);
      const std::string maxSub = std::format(
          "worst rend: follow {:.0f} objs {:.0f} rest {:.0f} hud {:.0f}",
          maxFollow, maxObjs, maxRest, maxHud);
      client::ui::fonts().draw(canvas, *c.fFont, maxSub, kProfileX,
                               sh - 105.0f, fHudPaint);
      double maxCircles = 0.0, maxSliders = 0.0, maxSpinners = 0.0;
      for (std::size_t i = 0; i < fProfileNum; ++i) {
        maxCircles = std::max(maxCircles, fProfile[i].objCirclesUs);
        maxSliders = std::max(maxSliders, fProfile[i].objSlidersUs);
        maxSpinners = std::max(maxSpinners, fProfile[i].objSpinnersUs);
      }
      const std::string objSub =
          std::format("worst objs: circles {:.0f} sliders {:.0f} spinners {:.0f}",
                      maxCircles, maxSliders, maxSpinners);
      client::ui::fonts().draw(canvas, *c.fFont, objSub, kProfileX,
                               sh - 120.0f, fHudPaint);
    }
  }

  void drawHealthBar(const Ctx &c, skia::SkCanvas *canvas, float x, float y, float w, float h,
                     double now) {
    auto left = c.fSkin->hpBarLeft();
    auto right = c.fSkin->hpBarRight();
    auto mid = c.fSkin->hpBarMid();
    const float hpX =
        x + w * static_cast<float>(std::clamp(fDisplayHealth, 0.0, 1.0));

    if (left && right && mid) {
      skia::SkPaint paint;
      paint.setAntiAlias(false);

      // Background/empty portion: right sprite stretches from hpX to the end.
      const float rightW = w;
      canvas->drawImageRect(
          right.get(), skia::SkRect::MakeXYWH(hpX, y, rightW, h),
          skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);

      // Filled portion: left sprite stretches from the start to hpX.
      const float leftW = hpX - x;
      canvas->drawImageRect(
          left.get(), skia::SkRect::MakeXYWH(x, y, leftW, h),
          skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);

      // Mid marker centered on the HP boundary.
      const float midScale = h / static_cast<float>(mid->height());
      const float midW = static_cast<float>(mid->width()) * midScale;
      canvas->drawImageRect(
          mid.get(), skia::SkRect::MakeXYWH(hpX - midW * 0.5f, y, midW, h),
          skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
      return;
    }

    // ArgonHealthDisplay: a white bar with a soft blue glow at its end, and a
    // red tail left behind when health is lost. The colours are lazer's --
    // #7ED7FD at half alpha for the glow, and the miss red easing from
    // (255, 147, 147) to (255, 93, 93) over its first 800 ms.
    const float barValue =
        static_cast<float>(std::clamp(fHealthBarValue, 0.0, 1.0));
    const float glowValue =
        static_cast<float>(std::clamp(fGlowBarValue, 0.0, 1.0));
    const float fill = w * barValue;

    skia::SkPaint bg;
    bg.setColor(skia::kBlack);
    bg.setStyle(skia::kFillStyle);
    bg.setAlphaf(0.5f);
    canvas->drawRect(skia::SkRect::MakeXYWH(x, y, w, h), bg);

    // The portion between the health and where it was: what the last miss
    // took away.
    if (glowValue > barValue) {
      const double sinceMiss = std::max(0.0, now - (fMissDisplayUntil - 500.0));
      const double t = std::clamp(sinceMiss / 800.0, 0.0, 1.0);
      const auto mix = [t](double from, double to) {
        return static_cast<int>(std::lround(from + (to - from) * t));
      };
      skia::SkPaint lost;
      lost.setStyle(skia::kFillStyle);
      lost.setAntiAlias(true);
      lost.setColor(skia::colorSetARGB(255, mix(255, 255), mix(147, 93),
                                       mix(147, 93)));
      canvas->drawRect(
          skia::SkRect::MakeXYWH(x + fill, y, w * (glowValue - barValue), h),
          lost);
    }

    skia::SkPaint fg;
    fg.setStyle(skia::kFillStyle);
    fg.setAntiAlias(true);
    fg.setColor(skia::kWhite);
    canvas->drawRect(skia::SkRect::MakeXYWH(x, y, fill, h), fg);

    // The glow at the end of the bar, flashing white for a moment on a hit.
    if (fill > 0.0f) {
      const bool flashing = now < fFlashUntil;
      skia::SkPaint glow;
      glow.setStyle(skia::kFillStyle);
      glow.setAntiAlias(true);
      glow.setColor(flashing ? skia::colorSetARGB(200, 255, 255, 255)
                             : skia::colorSetARGB(128, 0x7E, 0xD7, 0xFD));
      const float glowW = std::min(fill, h * 2.0f);
      canvas->drawRect(
          skia::SkRect::MakeXYWH(x + fill - glowW, y, glowW, h), glow);
    }

    skia::SkPaint border;
    border.setColor(skia::kWhite);
    border.setStyle(skia::kStrokeStyle);
    border.setStrokeWidth(2.0f);
    border.setAntiAlias(true);
    canvas->drawRect(skia::SkRect::MakeXYWH(x, y, w, h), border);
  }
private:
  std::vector<Popup> fPopups;
  std::vector<HitBurst> fHitBursts;
  std::deque<CursorTrailPoint> fCursorTrail;
  std::vector<FadingObject> fFadingObjects;

  skia::Sp<skia::SkImage> fBackground;
  skia::Sp<skia::SkImage> fBackgroundScaled;

  double fDisplayHealth = 1.0;
  double fHealthBarValue = 1.0;
  double fGlowBarValue = 1.0;
  double fMissDisplayUntil = 0.0;
  double fFlashUntil = 0.0;
  double fDisplayScore = 0.0;
  double fDisplayCombo = 0.0;
  double fDisplayAccuracy = 1.0;
  double fLastHudTime = 0.0;
  int fCombo = 0;


  std::array<ProfileFrame, kProfileCount> fProfile{};
  std::size_t fProfileIdx = 0;
  std::size_t fProfileNum = 0;

  skia::SkIRect fDirtyBounds = skia::SkIRect::MakeEmpty();
  bool fFirstFrame = true;

  skia::SkPaint fHudPaint{[] {
    skia::SkPaint p;
    p.setAntiAlias(true);
    return p;
  }()};
};

} // namespace client
