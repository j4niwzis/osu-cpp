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

// ---- Text with fallback ---------------------------------------------------
//
// Skia draws a string with exactly one typeface: a codepoint the typeface
// does not have becomes a box. Beatmap metadata is full of Japanese, Korean
// and the odd bit of everything else, so text is split into runs by which of
// the loaded faces can render it, and each run is drawn with that face.
//
// The stack is filled once at startup from the fonts shipped beside the
// binary; nothing is taken from the system. Lookups happen on the render
// thread only, which is what lets the coverage cache go unlocked.
class FontStack {
public:
  void setPrimary(skia::Sp<skia::SkTypeface> face) {
    fPrimary = std::move(face);
    this->invalidateCaches();
  }
  void addFallback(skia::Sp<skia::SkTypeface> face) {
    if (face) {
      fFallbacks.push_back(std::move(face));
      this->invalidateCaches();
    }
  }
  void invalidateCaches() {
    fCoverage.clear();
    fAsciiCovered.clear();
    fWidths.clear();
  }
  [[nodiscard]] const skia::Sp<skia::SkTypeface> &primary() const noexcept {
    return fPrimary;
  }
  [[nodiscard]] std::size_t fallbackCount() const noexcept {
    return fFallbacks.size();
  }

  [[nodiscard]] float measure(const skia::SkFont &font,
                              std::string_view text) const {
    if (text.empty()) {
      return 0.0f;
    }
    // Measuring is the hot part of drawing a menu: the same labels are
    // measured every frame, at the same sizes, by every screen. The answer
    // only depends on the text, the size, the weight and the face.
    const std::uint64_t key = cacheKey(font, text);
    if (const auto it = fWidths.find(key); it != fWidths.end()) {
      return it->second;
    }
    float width = 0.0f;
    this->forEachRun(font, text,
                     [&](const skia::SkFont &runFont, std::string_view run) {
                       width += runFont.measureText(
                           run.data(), run.size(), skia::SkTextEncoding::kUTF8);
                     });
    if (fWidths.size() > kMaxCachedWidths) {
      fWidths.clear(); // a whole screen's worth of labels fits many times over
    }
    fWidths.emplace(key, width);
    return width;
  }

  void draw(skia::SkCanvas *canvas, const skia::SkFont &font,
            std::string_view text, float x, float y,
            const skia::SkPaint &paint) const {
    // Nothing to split when every byte is plain ASCII and the primary face
    // covers it, which is most of the text this client draws.
    if (isAscii(text) && this->asciiCovered(font.getTypeface())) {
      canvas->drawSimpleText(text.data(), text.size(),
                             skia::SkTextEncoding::kUTF8, x, y, font, paint);
      return;
    }
    this->forEachRun(font, text,
                     [&](const skia::SkFont &runFont, std::string_view run) {
                       canvas->drawSimpleText(run.data(), run.size(),
                                              skia::SkTextEncoding::kUTF8, x, y,
                                              runFont, paint);
                       x += runFont.measureText(run.data(), run.size(),
                                                skia::SkTextEncoding::kUTF8);
                     });
  }

private:
  static constexpr std::size_t kMaxCachedWidths = 8192;

  [[nodiscard]] static bool isAscii(std::string_view text) {
    for (const char c : text) {
      if (static_cast<unsigned char>(c) >= 0x80) {
        return false;
      }
    }
    return true;
  }

  // Whether a face can draw the printable ASCII range, asked once per face.
  [[nodiscard]] bool asciiCovered(const skia::SkTypeface *face) const {
    if (face == nullptr) {
      return false;
    }
    const auto it = fAsciiCovered.find(face);
    if (it != fAsciiCovered.end()) {
      return it->second;
    }
    bool covered = true;
    for (std::int32_t cp = 0x20; cp < 0x7f; ++cp) {
      if (face->unicharToGlyph(cp) == 0) {
        covered = false;
        break;
      }
    }
    fAsciiCovered.emplace(face, covered);
    return covered;
  }

  [[nodiscard]] static std::uint64_t cacheKey(const skia::SkFont &font,
                                              std::string_view text) {
    std::uint64_t hash = std::hash<std::string_view>{}(text);
    hash ^= std::hash<const void *>{}(font.getTypeface()) * 0x9e3779b97f4a7c15ull;
    hash ^= static_cast<std::uint64_t>(font.getSize() * 64.0f) << 17;
    hash ^= static_cast<std::uint64_t>(font.isEmbolden()) << 61;
    return hash;
  }

  // -1 is the font the caller handed in; anything else indexes fFallbacks.
  [[nodiscard]] int faceFor(std::int32_t codepoint,
                            const skia::SkTypeface *base) const {
    if (base != nullptr && base->unicharToGlyph(codepoint) != 0) {
      return -1;
    }
    const auto cached = fCoverage.find(codepoint);
    if (cached != fCoverage.end()) {
      return cached->second;
    }
    int found = -1;
    for (std::size_t i = 0; i < fFallbacks.size(); ++i) {
      if (fFallbacks[i]->unicharToGlyph(codepoint) != 0) {
        found = static_cast<int>(i);
        break;
      }
    }
    fCoverage.emplace(codepoint, found);
    return found;
  }

  // Splits the text where the face has to change and hands each piece over.
  template <typename Fn>
  void forEachRun(const skia::SkFont &font, std::string_view text,
                  Fn &&fn) const {
    if (text.empty()) {
      return;
    }
    const skia::SkTypeface *base = font.getTypeface();
    std::size_t runStart = 0;
    int runFace = 0;
    bool haveRun = false;
    std::size_t i = 0;
    while (i < text.size()) {
      const std::size_t start = i;
      const std::int32_t cp = decodeUtf8(text, i);
      const int face = this->faceFor(cp, base);
      if (!haveRun) {
        runStart = start;
        runFace = face;
        haveRun = true;
        continue;
      }
      if (face != runFace) {
        fn(this->fontFor(font, runFace),
           text.substr(runStart, start - runStart));
        runStart = start;
        runFace = face;
      }
    }
    if (haveRun) {
      fn(this->fontFor(font, runFace), text.substr(runStart));
    }
  }

  [[nodiscard]] skia::SkFont fontFor(const skia::SkFont &font,
                                     int face) const {
    if (face < 0 || face >= static_cast<int>(fFallbacks.size())) {
      return font;
    }
    skia::SkFont out = font;
    out.setTypeface(fFallbacks[static_cast<std::size_t>(face)]);
    return out;
  }

  // Returns the codepoint at `i` and advances past it. Malformed input is
  // consumed a byte at a time so this always terminates.
  [[nodiscard]] static std::int32_t decodeUtf8(std::string_view text,
                                               std::size_t &i) {
    const auto byte = static_cast<unsigned char>(text[i]);
    int extra = 0;
    std::int32_t cp = byte;
    if (byte >= 0xf0) {
      extra = 3;
      cp = byte & 0x07;
    } else if (byte >= 0xe0) {
      extra = 2;
      cp = byte & 0x0f;
    } else if (byte >= 0xc0) {
      extra = 1;
      cp = byte & 0x1f;
    }
    if (i + static_cast<std::size_t>(extra) >= text.size()) {
      ++i;
      return byte;
    }
    for (int n = 0; n < extra; ++n) {
      const auto cont = static_cast<unsigned char>(text[i + 1 + static_cast<std::size_t>(n)]);
      if ((cont & 0xc0) != 0x80) {
        ++i;
        return byte;
      }
      cp = (cp << 6) | (cont & 0x3f);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
  }

  skia::Sp<skia::SkTypeface> fPrimary;
  std::vector<skia::Sp<skia::SkTypeface>> fFallbacks;
  mutable std::unordered_map<std::int32_t, int> fCoverage;
  mutable std::unordered_map<const skia::SkTypeface *, bool> fAsciiCovered;
  mutable std::unordered_map<std::uint64_t, float> fWidths;
};

inline FontStack &fonts() {
  static FontStack stack;
  return stack;
}

// The alpha a colour already carries, times the one the caller asked for.
// SkPaint::setAlphaf replaces rather than multiplies, so passing both without
// combining them turns a translucent colour opaque.
[[nodiscard]] inline float combinedAlpha(skia::SkColor color, float alpha) {
  return static_cast<float>((color >> 24) & 0xffu) / 255.0f * alpha;
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
                   skia::SkColor color, float alpha = 1.0f) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
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

  void fillRect(const skia::SkRect &rect, skia::SkColor color,
                float alpha = 1.0f) const {
    skia::SkPaint p;
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    fCanvas->drawRect(rect, p);
  }

  void circle(float cx, float cy, float r, skia::SkColor color,
              float alpha = 1.0f) const {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    fCanvas->drawCircle(cx, cy, r, p);
  }

  [[nodiscard]] float measure(const std::string &text, float size) const {
    fFont->setSize(size);
    return fonts().measure(*fFont, text);
  }

  void text(const std::string &str, float x, float y, float size,
            skia::SkColor color, float alpha = 1.0f) const {
    fFont->setSize(size);
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    fonts().draw(fCanvas, *fFont, str, x, y, p);
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
    fonts().draw(fCanvas, *fFont, str, cx - w * 0.5f + size * 0.045f,
                 y + size * 0.05f, shadow);
    this->text(str, cx - w * 0.5f, y, size, color, alpha);
  }

private:
  skia::SkCanvas *fCanvas;
  skia::SkFont *fFont;
};

} // namespace client::ui
