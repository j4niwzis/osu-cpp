export module client.logo;

import std;
import skia;
import skiff.paint;
import client.spectrum;
import client.triangles;

// osu!lazer's OsuLogo: a circular container filled with a vertical pink
// gradient (#ff66ab -> #cc5289) with TrianglesV2 masked inside it, the mark on
// top, a ripple of the same shape that scales out and fades on each beat, a
// white impact ring while it is struck, and the visualiser's skirt of bars
// around it.
//
// Its own module because it is its own thing: it holds where it is, how big
// it is, what the music is doing to it and which of that is still moving, and
// it needs nothing from the client but a font, a pointer and a target.
export namespace client::logo {

namespace paint = skiff::paint;

class Logo {
public:
  struct Ctx {
    skia::SkFont *fFont = nullptr;
    float fMouseX = 0.0f;
    float fMouseY = 0.0f;
    double fDtMs = 16.0;
    bool fVisualiser = true; // the bars follow the music rather than standing
    bool fTriangles = true;  // the ones inside the disc drift
    float fBase = 0.0f;      // unscaled radius for this screen size
    float fTargetX = 0.0f;
    float fTargetY = 0.0f;
    float fTargetScale = 1.0f;
  };

  // Where it is going, and how fast it gets there. Separate from settle so
  // that a client which places the logo itself -- the menu, which lays it out
  // between the button rows -- can do so without the ease.
  void moveTowards(const Ctx &ctx, float tauMs = 140.0f) {
    if (!fPlaced) {
      fX = ctx.fTargetX;
      fY = ctx.fTargetY;
      fScale = ctx.fTargetScale;
      fPlaced = true;
      return;
    }
    fX = paint::approach(fX, ctx.fTargetX, tauMs, ctx.fDtMs);
    fY = paint::approach(fY, ctx.fTargetY, tauMs, ctx.fDtMs);
    fScale = paint::approach(fScale, ctx.fTargetScale, tauMs, ctx.fDtMs);
  }

  // Struck: by a click on it, or by whatever else wants the impact ring.
  void strike() { fPunch = 1.0f; }

  // Where it is now, which is not where it is going: the buttons are laid
  // out around this, in the same frame it was eased.
  [[nodiscard]] float x() const noexcept { return fX; }
  [[nodiscard]] float y() const noexcept { return fY; }
  [[nodiscard]] float scale() const noexcept { return fScale; }
  [[nodiscard]] const skia::SkRect &bounds() const noexcept { return fRect; }
  [[nodiscard]] float radius() const noexcept { return fRadius; }
  [[nodiscard]] float baseRadius() const noexcept { return fBaseRadius; }
  [[nodiscard]] float amplitude() const noexcept { return fAmp; }
  [[nodiscard]] client::Spectrum &spectrum() noexcept { return fSpectrum; }

  [[nodiscard]] bool contains(float x, float y) const {
    const float dx = x - fRect.centerX();
    const float dy = y - fRect.centerY();
    const float r = fRect.width() * 0.5f;
    return r > 0.0f && dx * dx + dy * dy <= r * r;
  }

  // How far the bars reach this frame, for whoever has to say what moved: a
  // flat guess covered 40% of the screen and saved nothing.
  [[nodiscard]] float reach(const Ctx &ctx) {
    float loudest = 0.0f;
    for (const float amp :
         ctx.fVisualiser ? fSpectrum.bars() : this->stillBars()) {
      loudest = std::max(loudest, amp);
    }
    return fRadius * 2.0f * (600.0f / 480.0f) * loudest;
  }

  // Anything here still on its way somewhere, which is what decides whether
  // the frame after this one has to happen.
  [[nodiscard]] bool moving(const Ctx &ctx) const {
    return !paint::settled(fX, ctx.fTargetX) ||
           !paint::settled(fY, ctx.fTargetY) ||
           !paint::settled(fScale, ctx.fTargetScale) ||
           !paint::settled(fPunch, 0.0f) ||
           !paint::settled(
               fHover,
               this->contains(ctx.fMouseX, ctx.fMouseY) ? 1.0f : 0.0f);
  }

  // OsuLogo: a circular container filled with a vertical pink gradient
  // (#ff66ab -> #cc5289) with TrianglesV2 masked inside it, the logo mark on
  // top, a ripple of the same shape that scales out and fades on each beat,
  // and a white impact ring that only appears when the logo is struck. There
  // is no permanent halo -- the earlier glow ring was invented.
  // Where the logo is and how big, worked out without drawing anything.
  void settle(const Ctx &ctx) {
    fDtMs = ctx.fDtMs;
    const float logoBase = ctx.fBase;
    const bool hovered = (ctx.fMouseX - fX) * (ctx.fMouseX - fX) +
                             (ctx.fMouseY - fY) * (ctx.fMouseY - fY) <=
                         (logoBase * fScale) * (logoBase * fScale);
    fHover = this->ease(fHover, hovered ? 1.0f : 0.0f, 110.0f);
    fPunch = this->ease(fPunch, 0.0f, 180.0f);

    // Amplitude-driven beat: lazer drives these from timing points, which the
    // menu has not loaded, so bass energy stands in. Switched off with the
    // visualiser, or the logo would keep breathing on a screen that has
    // stopped drawing frames and would jump whenever one happened.
    fAmp = ctx.fVisualiser ? fSpectrum.bass() : 0.0f;
    const float beat = 1.0f - 0.02f * fAmp;
    // The radius the logo sits at with nothing happening to it, kept so the
    // mark can be typeset once against it and scaled rather than re-typeset.
    fBaseRadius = logoBase * fScale;
    fRadius = fBaseRadius * beat * (1.0f + 0.06f * fHover + 0.10f * fPunch);
    fRect = skia::SkRect::MakeXYWH(fX - fRadius, fY - fRadius, fRadius * 2,
                                   fRadius * 2);
  }

  void draw(skia::SkCanvas *canvas, const Ctx &ctx) {
    const float amp = fAmp;
    const float r = fRadius;

    this->drawVisualiser(canvas, ctx, r);

    // Ripple: same circle, scaled slightly out, alpha 0.15 * amplitude.
    if (amp > 0.01f) {
      skia::SkPaint ripple;
      ripple.setAntiAlias(true);
      ripple.setColor(skia::colorSetARGB(255, 0xff, 0x66, 0xab));
      ripple.setAlphaf(0.15f * std::min(1.0f, amp * 2.0f));
      canvas->drawCircle(fX, fY, r * (1.0f + 0.04f * amp), ripple);
    }

    // Body: vertical gradient disc, clipped triangles, then the mark.
    canvas->save();
    skia::SkPathBuilder disc;
    disc.addCircle(fX, fY, r);
    canvas->clipPath(disc.detach(), true);
    skiff::paint::verticalGradient(canvas, fRect,
                                   skia::colorSetARGB(255, 0xff, 0x66, 0xab),
                                   skia::colorSetARGB(255, 0xcc, 0x52, 0x89));
    this->drawTriangles(canvas, ctx, fRect);
    canvas->restore();

    // Impact ring: white border, only while punching.
    if (fPunch > 0.01f) {
      skia::SkPaint ring;
      ring.setAntiAlias(true);
      ring.setStyle(skia::kStrokeStyle);
      ring.setStrokeWidth(r * 0.08f);
      ring.setColor(skia::kWhite);
      ring.setAlphaf(fPunch * 0.8f);
      canvas->drawCircle(fX, fY, r * (1.0f + 0.12f * (1.0f - fPunch)), ring);
    }

    // Scaled with the logo rather than re-typeset at a new size every frame.
    // A font size that moves with the beat is measured afresh each frame, and
    // the width comes back a little different each time -- so the centred
    // text shifts sideways by a fraction of a pixel, unevenly, which is the
    // wobble. It also missed the width cache every frame: the cache filled
    // with one dead entry per frame until it was dropped whole, taking the
    // measurements of every other label on the screen with it.
    //
    // Scaling the canvas is not on its own enough: the device size the glyphs
    // are rasterised at still moves with the beat, and grid fitting snaps
    // each outline to the pixel grid at its own threshold as it passes
    // through one. That is the letter that twitches -- not the text sliding,
    // one glyph stepping while the rest hold still.
    const float base = fBaseRadius > 0.0f ? fBaseRadius : r;
    canvas->save();
    canvas->translate(fX, fY);
    canvas->scale(r / base, r / base);
    if (ctx.fFont != nullptr) {
      const skiff::paint::SmoothScaling smooth(*ctx.fFont);
      const paint::Painter p(canvas, *ctx.fFont);
      p.textCentered("osu!", 0.0f, base * 0.22f, base * 0.55f, skia::kWhite);
    }
    canvas->restore();
  }

  // TrianglesV2 inside the logo: Thickness 0.009, ScaleAdjust 3, SpawnRatio
  // 1.4, tinted #ff66ab at the top to #b6346f at the bottom.
  void drawTriangles(skia::SkCanvas *canvas, const Ctx &ctx,
                     const skia::SkRect &rect) {
    fTriangles.setScaleAdjust(1.05f);
    fTriangles.setSpawnRatio(1.4f);
    fTriangles.setThickness(0.009f);
    fTriangles.setAlphaRange(0.85f, 0.85f);
    fTriangles.setColours(skia::colorSetARGB(255, 0xff, 0x66, 0xab),
                          skia::colorSetARGB(255, 0xb6, 0x34, 0x6f));
    fTriangles.draw(canvas, rect, ctx.fTriangles ? ctx.fDtMs : 0.0, 1.0f);
  }

  // LogoVisualisation.VisualisationDrawNode, transcribed. Each bar is a quad
  // sitting on the logo's circumference, `bar_length * amplitude` long, with
  // width equal to the chord subtended by one bar; the whole ring is drawn
  // `visualiser_rounds` times, rotated, additively at 20% white.
  // What the bars stand at when the visualiser is switched off: a fixed
  // shape, so the logo keeps its skirt instead of sitting on the background
  // as a bare circle. Deterministic on purpose -- it is drawn once and never
  // asks to be drawn again.
  [[nodiscard]] std::span<const float> stillBars() const {
    static const std::vector<float> kBars = [] {
      constexpr int kCount = 200;
      std::vector<float> bars(static_cast<std::size_t>(kCount));
      // A spectrum does not undulate; it spikes. Neighbouring bins differ by
      // a lot, the whole thing slopes down as the frequency rises, and a
      // handful of partials stand well above the rest. So: a falling
      // envelope, a hash per bin for the jumps, and a few peaks on top.
      const auto hash01 = [](std::uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return static_cast<float>(x & 0xffffffU) /
               static_cast<float>(0x1000000U);
      };
      for (int i = 0; i < kCount; ++i) {
        const auto index = static_cast<std::uint32_t>(i);
        const float t = static_cast<float>(i) / static_cast<float>(kCount);
        // Loud at the bass end, thin at the top, as music is.
        const float envelope = 0.06f + 0.30f * std::pow(1.0f - t, 1.6f);
        // Squared, so most bins sit low and the occasional one jumps.
        const float jump = hash01(index * 2654435761U);
        float amp = envelope * (0.25f + 1.35f * jump * jump);
        if (hash01(index * 40503U + 17U) > 0.94f) {
          amp *= 2.1f; // a partial standing out of the noise
        }
        // Shorter than a loud moment of music: this one is on screen for as
        // long as the menu is, and a skirt that reaches out as far as a drop
        // does looks wrong standing still.
        bars[static_cast<std::size_t>(i)] =
            std::clamp(amp * 0.62f, 0.01f, 0.55f);
      }
      return bars;
    }();
    return kBars;
  }

  void drawVisualiser(skia::SkCanvas *canvas, const Ctx &ctx,
                      float logoRadius) {
    const auto bars = ctx.fVisualiser ? fSpectrum.bars() : this->stillBars();
    if (bars.empty()) {
      return;
    }
    constexpr int kRounds = 5; // visualiser_rounds
    constexpr float kAmplitudeDeadZone = 1.0f / 600.0f;
    const auto count = static_cast<int>(bars.size());

    // Nothing above the dead zone is the usual case between tracks.
    bool anyAudible = false;
    for (const float amp : bars) {
      if (amp >= kAmplitudeDeadZone) {
        anyAudible = true;
        break;
      }
    }
    if (!anyAudible) {
      return;
    }

    // A bar at a time, as a small convex path with antialiasing.
    //
    // Two attempts at making this cheaper both cost more, and both for
    // reasons worth keeping written down. Collecting the bars of a round into
    // one path takes Skia off the analytic route it has for convex shapes and
    // onto a coverage mask over the whole circle. Sending them as vertices
    // removes the per-draw overhead but also the antialiasing, and adding it
    // back as a ring of transparent geometry means blending five times the
    // triangles -- which on a software rasteriser is paid per pixel, and cost
    // more than the draws it saved. What is here is what measured best.
    const float barLength = logoRadius * 2.0f * (600.0f / 480.0f);
    // barSize.X = size * sqrt(2 * (1 - cos(360/bars))) / 2  -- the chord.
    const float chord =
        logoRadius * 2.0f *
        std::sqrt(2.0f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> /
                                          static_cast<float>(count)))) /
        2.0f;

    this->ensureVisualiserAngles(count, kRounds);

    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(skia::kWhite);
    paint.setAlphaf(0.2f); // transparent_white
    paint.setBlendMode(skia::SkBlendMode::kPlus);

    for (int round = 0; round < kRounds; ++round) {
      for (int i = 0; i < count; ++i) {
        const float amp = bars[static_cast<std::size_t>(i)];
        if (amp < kAmplitudeDeadZone) {
          continue;
        }
        const auto &angle =
            fVisualiserAngles[static_cast<std::size_t>(round * count + i)];
        const float bx = fX + angle.fCos * logoRadius;
        const float by = fY + angle.fSin * logoRadius;
        // bottomOffset is perpendicular; amplitudeOffset is radial.
        const float ox = -angle.fSin * chord * 0.5f;
        const float oy = angle.fCos * chord * 0.5f;
        const float ax = angle.fCos * barLength * amp;
        const float ay = angle.fSin * barLength * amp;

        skia::SkPathBuilder bar;
        bar.moveTo(bx - ox, by - oy);
        bar.lineTo(bx - ox + ax, by - oy + ay);
        bar.lineTo(bx + ox + ax, by + oy + ay);
        bar.lineTo(bx + ox, by + oy);
        bar.close();
        canvas->drawPath(bar.detach(), paint);
      }
    }
  }

  // The bars sit at fixed angles; only their lengths change. Computing two
  // trigonometric functions per bar per frame -- two thousand of them at five
  // rounds of two hundred -- was work repeated for an answer that never
  // changes.
  void ensureVisualiserAngles(int count, int rounds) {
    const auto needed =
        static_cast<std::size_t>(count) * static_cast<std::size_t>(rounds);
    if (fVisualiserAngles.size() == needed && fVisualiserCount == count) {
      return;
    }
    fVisualiserAngles.resize(needed);
    fVisualiserCount = count;
    for (int round = 0; round < rounds; ++round) {
      for (int i = 0; i < count; ++i) {
        const float rotation =
            2.0f * std::numbers::pi_v<float> *
            (static_cast<float>(i) / static_cast<float>(count) +
             static_cast<float>(round) / static_cast<float>(rounds));
        fVisualiserAngles[static_cast<std::size_t>(round * count + i)] = {
            std::cos(rotation), std::sin(rotation)};
      }
    }
  }

private:
  [[nodiscard]] float ease(float current, float target, float tauMs) const {
    return paint::approach(current, target, tauMs, fDtMs);
  }

  struct Angle {
    float fCos = 0.0f, fSin = 0.0f;
  };

  float fX = 0.0f;
  float fY = 0.0f;
  float fScale = 1.0f;
  bool fPlaced = false;
  float fHover = 0.0f;
  float fPunch = 0.0f;      // click and beat impact, decays
  float fBaseRadius = 0.0f; // before beat, hover and punch
  float fAmp = 0.0f;        // beat amplitude it settled at
  float fRadius = 0.0f;
  skia::SkRect fRect = skia::SkRect::MakeEmpty();
  client::triangles::Field fTriangles;
  client::Spectrum fSpectrum;
  std::vector<Angle> fVisualiserAngles; // fixed per bar count, not per frame
  int fVisualiserCount = 0;
  double fDtMs = 16.0;
};

} // namespace client::logo
