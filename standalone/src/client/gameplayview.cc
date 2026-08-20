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
    float fScale = 1.0f;
    float fOffsetX = 0.0f;
    float fOffsetY = 0.0f;
    int fScreenW = 0;
    int fScreenH = 0;
    osu::Vec2 fCursor{};
    float fCursorSize = 1.0f;
    float fDim = 0.7f;
    bool fNoGlow = false;
    bool fShowFps = false;
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
    std::size_t fComboIndex;
  };
  struct CursorTrailPoint {
    osu::Vec2 fPos;
    double fTime;
  };
  struct FadingObject {
    std::size_t fIndex;
    double fTime;
    osu::Judgement fResult;
  };
  struct ProfileFrame {
    double advUs, renderUs, flushUs, swapUs;
    double renderFollowUs, renderObjectsUs, renderRestUs, renderHudUs;
  };

  static constexpr double kPopupLifetime = 700.0;
  static constexpr double kHitBurstLifetime = 350.0;
  static constexpr double kCursorTrailLifetime = 140.0;
  static constexpr std::size_t kCursorTrailMax = 40;
  static constexpr double kFadeLifetime = 250.0;
  static constexpr std::size_t kFpsSampleCount = 120;
  static constexpr std::size_t kProfileCount = 60;


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
    fFrameTimeIdx = 0;
    fFrameTimeCount = 0;
    fLastFrameTime = 0.0;
    fFirstFrame = true;
    fCombo = 0;
  }

  void addJudgement(const osu::Judgement &result, std::size_t index,
                    osu::Vec2 pos, double now, std::size_t comboIndex,
                    bool countsCombo) {
    fPopups.push_back({result, now, pos});
    fFadingObjects.push_back({index, now, result});
    if (countsCombo) {
      fHitBursts.push_back({pos, now, comboIndex});
    }
  }

  void addTrailPoint(osu::Vec2 pos, double time) {
    fCursorTrail.push_back({pos, time});
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
  [[nodiscard]] std::span<const double> frameTimes() const noexcept {
    return {fFrameTimes.data(), fFrameTimeCount};
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
    const double preempt = osu::preemptTime(ar);
    const double fadeIn = osu::fadeInTime(ar);
    const double radius = osu::circleRadius(cs);
    const double spacing = radius * 0.7;
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
      const double distance = dir.length();
      if (distance < spacing * 3.0)
        continue;
      const double angle = std::atan2(dir.fY, dir.fX);
      const double dt = endTime - startTime;
      for (double d = spacing * 2.0; d < distance - 1.5 * spacing;
           d += spacing) {
        const double fraction = d / distance;
        const double pointTime = startTime + dt * fraction;
        const double fadeInTime = pointTime - preempt;
        const double fadeOutTime = pointTime;
        double rawAlpha = 0.0;
        if (now >= fadeInTime && now < fadeOutTime)
          rawAlpha = (now - fadeInTime) / fadeIn;
        else if (now >= fadeOutTime)
          rawAlpha = 1.0 - (now - fadeOutTime) / fadeIn;
        rawAlpha = std::clamp(rawAlpha, 0.0, 1.0);
        const double relpos = rawAlpha * (2.0 - rawAlpha);
        const double drawFraction = fraction - 0.1 * (1.0 - relpos);
        const osu::Vec2 p = startPos + dir * drawFraction;
        const float alpha = static_cast<float>(rawAlpha * 0.5);
        if (alpha > 0.0f) {
          c.fSkin->drawFollowPoint(canvas, p, angle, alpha, cs);
        }
      }
    }
  }

  void drawHitBursts(const Ctx &c, skia::SkCanvas *canvas, double now, double cs) {
    auto it = fHitBursts.begin();
    while (it != fHitBursts.end()) {
      const double age = now - it->fTime;
      if (age > kHitBurstLifetime) {
        it = fHitBursts.erase(it);
        continue;
      }
      c.fSkin->drawHitBurst(canvas, it->fPos, cs, age, it->fComboIndex);
      ++it;
    }
  }

  void render(const Ctx &c, double now) {
    using clock = std::chrono::steady_clock;
    auto rt0 = clock::now();

    if (fLastFrameTime > 0.0 && fLastFrameTime < now) {
      const double ft = now - fLastFrameTime;
      fFrameTimes[fFrameTimeIdx] = ft;
      fFrameTimeIdx = (fFrameTimeIdx + 1) % kFpsSampleCount;
      if (fFrameTimeCount < kFpsSampleCount)
        ++fFrameTimeCount;
    }
    fLastFrameTime = now;

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

    for (std::size_t i = 0; i < c.fMap->fObjects.size(); ++i) {
      this->drawObject(c, canvas, c.fMap->fObjects[i], i, now, ar, cs, od);
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

    if (c.fShowProfile || c.fShowFps) {
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
                           static_cast<float>(osu::kPlayfieldCenter.fY), 90.0f);
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

    for (const auto &hb : fHitBursts)
      addPlayfieldPt(static_cast<float>(hb.fPos.fX),
                     static_cast<float>(hb.fPos.fY), r * 2.5f);

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
                     c.fSkin->drawSlider(canvas, o, index,
                                      c.fMap->fSliderPaths[index],
                                      c.fMap->sliderSpanDuration(o),
                                      c.fMap->sliderTickDistance(o), now, cs, ar,
                                      od, o.fCombo, (*c.fCombo).fIndices[index],
                                      1.0f, c.fEngine->isTracking(index));
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
      if (age > kFadeLifetime) {
        it = fFadingObjects.erase(it);
        continue;
      }
      const float alpha = static_cast<float>(1.0 - age / kFadeLifetime);
      std::visit(
          osu::Overloaded{
              [&](const osu::Circle &o) {
                c.fSkin->drawHitCircle(canvas, o.fPos, o.fTime, now, cs, ar,
                                    o.fCombo, (*c.fCombo).fIndices[it->fIndex],
                                    alpha);
              },
              [&](const osu::Slider &o) {
                c.fSkin->drawSlider(
                    canvas, o, it->fIndex, c.fMap->fSliderPaths[it->fIndex],
                    c.fMap->sliderSpanDuration(o), c.fMap->sliderTickDistance(o),
                    now, cs, ar, od, o.fCombo, (*c.fCombo).fIndices[it->fIndex],
                    alpha, false);
              },
              [&](const osu::Spinner &) {},
          },
          c.fMap->fObjects[it->fIndex]);
      ++it;
    }
  }

  void drawSpinner(const Ctx &c, skia::SkCanvas *canvas, const osu::Spinner &s,
                   std::size_t index, double now, double cs, double od) {
    const float cx = static_cast<float>(osu::kPlayfieldCenter.fX);
    const float cy = static_cast<float>(osu::kPlayfieldCenter.fY);
    const float radius = 80.0f;

    const double progress =
        now < s.fTime
            ? 0.0
            : std::clamp(osu::spinnerProgress(c.fEngine->spinnerRotations(index),
                                              s.fEnd - s.fTime, od),
                         0.0, 1.0);
    c.fSkin->drawSpinner(canvas, cx, cy, radius, progress);

    skia::SkPaint textPaint;
    textPaint.setColor(skia::kWhite);
    textPaint.setStyle(skia::kFillStyle);
    textPaint.setAntiAlias(true);
    (*c.fFont).setSize(20.0f / c.fScale);
    const std::string label =
        std::format("{}/{}", std::max(0, c.fEngine->spinnerRotations(index)),
                    static_cast<int>(std::ceil(
                        osu::spinnerRequiredRotations(s.fEnd - s.fTime, od))));
    canvas->drawString(label.c_str(), cx, cy + 6.0f / c.fScale, (*c.fFont), textPaint);
  }

  void updateCursorTrail(const Ctx &c, double now) {
    if (!fCursorTrail.empty() && fCursorTrail.back().fPos.fX == c.fCursor.fX &&
        fCursorTrail.back().fPos.fY == c.fCursor.fY) {
      return;
    }
    fCursorTrail.push_back({c.fCursor, now});
    while (!fCursorTrail.empty() &&
           now - fCursorTrail.front().fTime > kCursorTrailLifetime) {
      fCursorTrail.pop_front();
    }
    if (fCursorTrail.size() > kCursorTrailMax) {
      fCursorTrail.pop_front();
    }
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

    // One round of Chaikin corner cutting rounds off sharp turns that would
    // otherwise fold the ribbon onto itself.
    std::vector<TrailPt> pts;
    pts.reserve(raw.size() * 2);
    pts.push_back(raw.front());
    for (std::size_t i = 0; i + 1 < raw.size(); ++i) {
      const auto &a = raw[i];
      const auto &b = raw[i + 1];
      pts.push_back({a.fX * 0.75f + b.fX * 0.25f, a.fY * 0.75f + b.fY * 0.25f,
                     a.fAlpha * 0.75f + b.fAlpha * 0.25f});
      pts.push_back({a.fX * 0.25f + b.fX * 0.75f, a.fY * 0.25f + b.fY * 0.75f,
                     a.fAlpha * 0.25f + b.fAlpha * 0.75f});
    }
    pts.push_back(raw.back());

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

  // Judgement text, ported from webosu-2's playback.js: bitmap text tinted per
  // result, no outline, fading over 500 ms (800 for a miss, which also drops
  // and rotates), and -- the characteristic part -- the letters spread apart
  // as it fades, letterSpacing = 70 * ((t/1800 - 1)^5 + 1).
  void drawPopups(const Ctx &c, skia::SkCanvas *canvas, double now, double cs) {
    const double hitSpriteScale = osu::circleRadius(cs) / 60.0;
    auto it = fPopups.begin();
    while (it != fPopups.end()) {
      const double age = now - it->fTime;
      const bool isMiss =
          std::holds_alternative<osu::judgement::Miss>(it->fResult);
      const double lifetime = isMiss ? 800.0 : 500.0;
      if (age > lifetime) {
        it = fPopups.erase(it);
        continue;
      }

      double alpha = 0.0;
      float yOffset = 0.0f;
      float rotation = 0.0f;
      float spacing = 0.0f;
      if (isMiss) {
        alpha = age < 100.0   ? age / 100.0
                : age < 600.0 ? 1.0
                              : 1.0 - (age - 600.0) / 200.0;
        const double k = std::pow(age / 800.0, 5.0);
        yOffset = static_cast<float>(100.0 * k * hitSpriteScale);
        rotation = static_cast<float>(0.7 * k);
      } else {
        alpha = age < 100.0 ? age / 100.0 : 1.0 - (age - 100.0) / 400.0;
        spacing = static_cast<float>(
            70.0 * (std::pow(age / 1800.0 - 1.0, 5.0) + 1.0));
      }
      alpha = std::clamp(alpha, 0.0, 1.0);

      const auto [text, color] = popupInfo(it->fResult);
      const float x = static_cast<float>(it->fPos.fX);
      const float y = static_cast<float>(it->fPos.fY) + yOffset;

      // webosu-2 scales the text 0.85 horizontally against 1.0 vertically.
      const float fontSize = static_cast<float>(20.0 * hitSpriteScale);
      c.fFont->setSize(fontSize);
      const float letterSpacing =
          spacing * static_cast<float>(hitSpriteScale) * 0.01f * fontSize;

      // Measure with the spacing applied so the string stays centred.
      const std::string str(text);
      float total = 0.0f;
      for (std::size_t k = 0; k < str.size(); ++k) {
        total += c.fFont->measureText(&str[k], 1, skia::SkTextEncoding::kUTF8) *
                 0.85f;
        if (k + 1 < str.size()) {
          total += letterSpacing;
        }
      }

      skia::SkPaint paint;
      paint.setAntiAlias(true);
      paint.setColor(color);
      paint.setAlphaf(static_cast<float>(alpha));
      // webosu-2 renders these in Venera, a heavy display face. The bundled
      // UI font is far lighter, so thicken the glyphs with a stroke of the
      // same colour rather than leaving them looking spindly.
      paint.setStyle(skia::kStrokeAndFillStyle);
      paint.setStrokeWidth(fontSize * 0.10f);
      paint.setStrokeJoin(skia::kRoundJoin);

      canvas->save();
      canvas->translate(x, y);
      if (rotation != 0.0f) {
        canvas->rotate(rotation * 180.0f / std::numbers::pi_v<float>);
      }
      canvas->scale(0.85f, 1.0f);
      float pen = -total / 0.85f * 0.5f;
      for (std::size_t k = 0; k < str.size(); ++k) {
        canvas->drawSimpleText(&str[k], 1, skia::SkTextEncoding::kUTF8, pen,
                               0.0f, *c.fFont, paint);
        pen += c.fFont->measureText(&str[k], 1, skia::SkTextEncoding::kUTF8) +
               letterSpacing / 0.85f;
      }
      canvas->restore();
      ++it;
    }
  }

  // Labels and tints from webosu-2: miss 0xed1121, meh 0xffcc22,
  // good 0x88b300, great 0x66ccff.
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
    fDisplayHealth += (score.fHealth - fDisplayHealth) * lagFactor;
    fDisplayScore +=
        (static_cast<double>(score.fScore) - fDisplayScore) * lagFactor;
    fDisplayCombo +=
        (static_cast<double>(score.fCombo) - fDisplayCombo) * lagFactor;
    fDisplayAccuracy += (score.accuracy() - fDisplayAccuracy) * lagFactor;

    fHudPaint.setColor(skia::kWhite);
    fHudPaint.setAlphaf(1.0f);

    // Combo counter (large, top-left).
    (*c.fFont).setSize(48.0f);
    const std::string comboText =
        std::format("{:.0f}x", std::max(0.0, fDisplayCombo));
    canvas->drawString(comboText.c_str(), 20.0f, 60.0f, (*c.fFont), fHudPaint);

    // Score, accuracy, grade (top-center).
    (*c.fFont).setSize(22.0f);
    const std::string statsText =
        std::format("{:.0f}  {:.2f}%  {}", fDisplayScore,
                    std::clamp(fDisplayAccuracy, 0.0, 1.0) * 100.0,
                    osu::gradeString(osu::computeGrade(score)));
    canvas->drawString(statsText.c_str(), 20.0f, 90.0f, (*c.fFont), fHudPaint);

    // Difficulty / mods (top-right).
    (*c.fFont).setSize(16.0f);
    const std::string diffText = std::format(
        "CS:{:.1f} AR:{:.1f} OD:{:.1f} HP:{:.1f} {}", c.fMap->fDiff.fCs,
        c.fMap->fDiff.fAr, c.fMap->fDiff.fOd, c.fMap->fDiff.fHp, c.fEngine->mods());
    canvas->drawString(diffText.c_str(), 20.0f, 115.0f, (*c.fFont), fHudPaint);

    // Health bar (top).
    this->drawHealthBar(c, canvas, 0.0f, 0.0f, sw, 14.0f, now);

    // Judgement counts.
    (*c.fFont).setSize(16.0f);
    const std::string countsText =
        std::format("Great {}  Good {}  Meh {}  Miss {}", score.fGreat,
                    score.fGood, score.fMeh, score.fMiss);
    canvas->drawString(countsText.c_str(), 20.0f, 140.0f, (*c.fFont), fHudPaint);

    // Progress time.
    (*c.fFont).setSize(14.0f);
    fHudPaint.setAlphaf(0.7f);
    const std::string timeText = std::format("{:.1f}s", now / 1000.0);
    canvas->drawString(timeText.c_str(), sw - 80.0f, sh - 20.0f, (*c.fFont),
                       fHudPaint);

    double avgFrameMs = 0.0;
    if (fFrameTimeCount > 0) {
      for (std::size_t i = 0; i < fFrameTimeCount; ++i)
        avgFrameMs += fFrameTimes[i];
      avgFrameMs /= static_cast<double>(fFrameTimeCount);
    }
    const double fps = avgFrameMs > 0.0 ? 1000.0 / avgFrameMs : 0.0;
    const std::string fpsText = std::format("{:.0f} fps", std::round(fps));
    canvas->drawString(fpsText.c_str(), sw - 80.0f, sh - 40.0f, (*c.fFont),
                       fHudPaint);

    if (c.fShowProfile || c.fShowFps) {
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
      (*c.fFont).setSize(11.0f);
      fHudPaint.setAlphaf(0.6f);
      const std::string profText =
          std::format("adv {:.0f}  rend {:.0f}  flush {:.0f}  swap {:.0f} us",
                      avgAdv, avgRender, avgFlush, avgSwap);
      canvas->drawString(profText.c_str(), sw - 240.0f, sh - 60.0f, (*c.fFont),
                         fHudPaint);
      const std::string subText =
          std::format("follow {:.0f}  objs {:.0f}  rest {:.0f}  hud {:.0f} us",
                      avgFollow, avgObjs, avgRest, avgHud);
      canvas->drawString(subText.c_str(), sw - 240.0f, sh - 75.0f, (*c.fFont),
                         fHudPaint);
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

    const float fill =
        w * static_cast<float>(std::clamp(fDisplayHealth, 0.0, 1.0));
    skia::SkPaint bg;
    bg.setColor(skia::kBlack);
    bg.setStyle(skia::kFillStyle);
    bg.setAlphaf(0.5f);
    canvas->drawRect(skia::SkRect::MakeXYWH(x, y, w, h), bg);

    skia::SkPaint fg;
    fg.setStyle(skia::kFillStyle);
    fg.setAntiAlias(true);
    if (fDisplayHealth > 0.5) {
      fg.setColor(skia::colorSetARGB(255, 50, 205, 50));
    } else if (fDisplayHealth > 0.25) {
      fg.setColor(skia::colorSetARGB(255, 255, 215, 0));
    } else {
      fg.setColor(skia::colorSetARGB(255, 255, 50, 50));
    }
    canvas->drawRect(skia::SkRect::MakeXYWH(x, y, fill, h), fg);

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
  double fDisplayScore = 0.0;
  double fDisplayCombo = 0.0;
  double fDisplayAccuracy = 1.0;
  double fLastHudTime = 0.0;
  int fCombo = 0;

  std::array<double, kFpsSampleCount> fFrameTimes{};
  std::size_t fFrameTimeIdx = 0;
  std::size_t fFrameTimeCount = 0;
  double fLastFrameTime = 0.0;

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
