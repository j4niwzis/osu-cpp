export module client.ui;

import std;
import skia;

export namespace client::ui {

// osu!lazer's OverlayColourProvider palette (purple scheme) plus the accents
// used across song select and the menus. Kept in one place so screens do not
// each invent their own shade.
inline constexpr skia::SkColor kAccent = skia::colorSetARGB(255, 255, 102, 170);
inline constexpr skia::SkColor kAccent2 = skia::colorSetARGB(255, 102, 204, 255);
inline constexpr skia::SkColor kBackground3 = skia::colorSetARGB(255, 51, 41, 66);
inline constexpr skia::SkColor kBackground4 = skia::colorSetARGB(255, 40, 33, 48);
inline constexpr skia::SkColor kBackground5 = skia::colorSetARGB(255, 31, 25, 40);
inline constexpr skia::SkColor kBackground6 = skia::colorSetARGB(255, 23, 19, 30);
inline constexpr skia::SkColor kCardBg = kBackground4;
inline constexpr skia::SkColor kCardSel = skia::colorSetARGB(255, 64, 48, 70);
inline constexpr skia::SkColor kPanelBg = skia::colorSetARGB(215, 22, 18, 28);

// Judgement colours, matching lazer's OsuColour.
inline constexpr skia::SkColor kGreat = skia::colorSetARGB(255, 102, 204, 255);
inline constexpr skia::SkColor kGood = skia::colorSetARGB(255, 179, 217, 68);
inline constexpr skia::SkColor kMeh = skia::colorSetARGB(255, 255, 204, 34);
inline constexpr skia::SkColor kMiss = skia::colorSetARGB(255, 237, 17, 33);

// Star-rating colour ramp (OsuColour.ForStarDifficulty).
[[nodiscard]] inline skia::SkColor starColor(double stars) {
  if (stars < 2.0) {
    return skia::colorSetARGB(255, 102, 204, 102);
  }
  if (stars < 2.7) {
    return skia::colorSetARGB(255, 102, 204, 255);
  }
  if (stars < 4.0) {
    return skia::colorSetARGB(255, 255, 204, 102);
  }
  if (stars < 5.3) {
    return skia::colorSetARGB(255, 255, 102, 170);
  }
  if (stars < 6.5) {
    return skia::colorSetARGB(255, 170, 102, 255);
  }
  return skia::colorSetARGB(255, 90, 90, 110);
}

// Easing curves used by the framework's transforms.
[[nodiscard]] inline float outQuint(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const float u = 1.0f - t;
  return 1.0f - u * u * u * u * u;
}

[[nodiscard]] inline float outElasticHalf(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  constexpr float p = 0.5f;
  return std::pow(2.0f, -10.0f * t) *
             std::sin((t - p / 4.0f) * (2.0f * std::numbers::pi_v<float>) / p) +
         1.0f;
}

// Frame-rate independent approach toward a target (tau in milliseconds).
[[nodiscard]] inline float approach(float current, float target, float tauMs,
                                    double dtMs) {
  const float a = 1.0f - std::exp(-static_cast<float>(dtMs) / tauMs);
  return current + (target - current) * a;
}

// ---- Drawing helpers -----------------------------------------------------
//
// A thin wrapper around the canvas and the shared font, so screens do not
// repeat paint setup. Holds no state of its own.
class Painter {
public:
  Painter(skia::SkCanvas *canvas, skia::SkFont &font)
      : fCanvas(canvas), fFont(&font) {}

  [[nodiscard]] skia::SkCanvas *canvas() const noexcept { return fCanvas; }

  void fillRounded(const skia::SkRect &rect, float radius,
                   skia::SkColor color) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    fCanvas->drawRRect(skia::SkRRect::MakeRectXY(rect, radius, radius), p);
  }

  void strokeRounded(const skia::SkRect &rect, float radius,
                     skia::SkColor color, float width) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setStyle(skia::kStrokeStyle);
    p.setStrokeWidth(width);
    fCanvas->drawRRect(skia::SkRRect::MakeRectXY(rect, radius, radius), p);
  }

  void fillRect(const skia::SkRect &rect, skia::SkColor color) const {
    skia::SkPaint p;
    p.setColor(color);
    fCanvas->drawRect(rect, p);
  }

  void circle(float cx, float cy, float r, skia::SkColor color,
              float alpha = 1.0f) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(alpha);
    fCanvas->drawCircle(cx, cy, r, p);
  }

  [[nodiscard]] float measure(const std::string &text, float size) const {
    fFont->setSize(size);
    return fFont->measureText(text.c_str(), text.size(),
                              skia::SkTextEncoding::kUTF8);
  }

  void text(const std::string &str, float x, float y, float size,
            skia::SkColor color, float alpha = 1.0f) const {
    fFont->setSize(size);
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(alpha);
    fCanvas->drawString(str.c_str(), x, y, *fFont, p);
  }

  void textClipped(const std::string &str, float x, float y, float maxW,
                   float size, skia::SkColor color, float alpha = 1.0f) const {
    fCanvas->save();
    fCanvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(x), static_cast<int>(y - size * 1.2f),
        static_cast<int>(maxW), static_cast<int>(size * 1.8f)));
    this->text(str, x, y, size, color, alpha);
    fCanvas->restore();
  }

  // Centred, but clipped to a width so a long title cannot run past a panel.
  void textCenteredClipped(const std::string &str, float cx, float y,
                           float maxW, float size, skia::SkColor color,
                           float alpha = 1.0f) const {
    fCanvas->save();
    fCanvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(cx - maxW * 0.5f), static_cast<int>(y - size * 1.2f),
        static_cast<int>(maxW), static_cast<int>(size * 1.8f)));
    this->textCentered(str, cx, y, size, color, alpha);
    fCanvas->restore();
  }

  void textCentered(const std::string &str, float cx, float y, float size,
                    skia::SkColor color, float alpha = 1.0f) const {
    const float w = this->measure(str, size);
    this->text(str, cx - w * 0.5f, y, size, color, alpha);
  }

  // Text with a soft shadow, the way lazer draws judgements and HUD numbers.
  void textShadowed(const std::string &str, float cx, float y, float size,
                    skia::SkColor color, float alpha = 1.0f) const {
    const float w = this->measure(str, size);
    fFont->setSize(size);
    skia::SkPaint shadow;
    shadow.setAntiAlias(true);
    shadow.setColor(skia::colorSetARGB(255, 0, 0, 0));
    shadow.setAlphaf(alpha * 0.45f);
    fCanvas->drawString(str.c_str(), cx - w * 0.5f + size * 0.045f,
                        y + size * 0.05f, *fFont, shadow);
    this->text(str, cx - w * 0.5f, y, size, color, alpha);
  }

private:
  skia::SkCanvas *fCanvas;
  skia::SkFont *fFont;
};

} // namespace client::ui
