export module skiff.paint;

import std;
import skia;

// Text and paint: the font stack that puts fallbacks behind a primary face,
// and a thin wrapper over the canvas so callers do not repeat paint setup.
// Nothing in here knows what it is drawing.
export namespace skiff::paint {

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
//
// This used to set a global flag whenever anything moved, which the frame
// loop read once a frame to decide whether to keep drawing. It worked, and it
// was a side channel: every eased value in the client wrote to one bool, and
// nothing could be asked who had moved. Every screen now settles in a pass of
// its own and marks what changed, and marked damage is what asks for the next
// frame -- so the flag has nothing left to say.
[[nodiscard]] inline float approach(float current, float target, float tauMs,
                                    double dtMs) {
  const float a = 1.0f - std::exp(-static_cast<float>(dtMs) / tauMs);
  const float next = current + (target - current) * a;
  // Exponential easing never arrives. Left alone, a value that has visually
  // settled keeps changing in the fifth decimal for ever, and anything
  // comparing it against its previous value -- which is how this client
  // decides what to repaint -- concludes that it is still moving. Below a
  // thousandth of a unit, which is under a pixel and under 1/255 of an
  // alpha, it is there.
  return std::abs(target - next) < 0.001f ? target : next;
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
    // A weight instance of the same file, so bold text is a different face
    // rather than the same one dilated at rasterisation time. Faking bold
    // costs more per glyph than drawing one, and on a software rasteriser
    // that is the difference between a text-heavy screen at 150 frames a
    // second and at 60.
    fPrimaryBold.reset();
    if (fPrimary) {
      skia::SkFontArguments::VariationPosition::Coordinate coordinate{
          kWeightAxis, 600.0f};
      skia::SkFontArguments::VariationPosition position{&coordinate, 1};
      skia::SkFontArguments arguments;
      arguments.setVariationDesignPosition(position);
      fPrimaryBold = fPrimary->makeClone(arguments);
    }
    this->invalidateCaches();
  }

  // Picks the face for the weight instead of asking the rasteriser to
  // thicken one, falling back to that only when there is no bold instance.
  void applyWeight(skia::SkFont &font, bool bold) const {
    if (bold && fPrimaryBold) {
      font.setTypeface(fPrimaryBold);
      font.setEmbolden(false);
      return;
    }
    if (fPrimary) {
      font.setTypeface(fPrimary);
    }
    font.setEmbolden(bold);
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
    hash ^=
        std::hash<const void *>{}(font.getTypeface()) * 0x9e3779b97f4a7c15ull;
    hash ^= static_cast<std::uint64_t>(font.getSize() * 64.0f) << 17;
    hash ^= static_cast<std::uint64_t>(font.isEmbolden()) << 61;
    // Linear metrics measure wider than hinted ones by a fraction of a pixel
    // per glyph, so a width cached under one is wrong under the other.
    hash ^= static_cast<std::uint64_t>(font.isLinearMetrics()) << 60;
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

  [[nodiscard]] skia::SkFont fontFor(const skia::SkFont &font, int face) const {
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
      const auto cont =
          static_cast<unsigned char>(text[i + 1 + static_cast<std::size_t>(n)]);
      if ((cont & 0xc0) != 0x80) {
        ++i;
        return byte;
      }
      cp = (cp << 6) | (cont & 0x3f);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
  }

  // 'wght', the OpenType weight axis.
  static constexpr std::uint32_t kWeightAxis =
      (static_cast<std::uint32_t>('w') << 24) |
      (static_cast<std::uint32_t>('g') << 16) |
      (static_cast<std::uint32_t>('h') << 8) | static_cast<std::uint32_t>('t');

  skia::Sp<skia::SkTypeface> fPrimary;
  skia::Sp<skia::SkTypeface> fPrimaryBold;
  std::vector<skia::Sp<skia::SkTypeface>> fFallbacks;
  mutable std::unordered_map<std::int32_t, int> fCoverage;
  mutable std::unordered_map<const skia::SkTypeface *, bool> fAsciiCovered;
  mutable std::unordered_map<std::uint64_t, float> fWidths;
};

inline FontStack &fonts() {
  // One per thread rather than one per process. It carries caches -- measured
  // widths, which typeface covers which codepoint -- and it mutates the SkFont
  // it is handed, so two threads drawing text through one of these would be
  // writing to the same caches at the same time. Only the render thread draws
  // today, so this costs nothing today; it is what lets a second thread draw
  // at all, which is what rendering a video export off the render thread
  // needs. Typefaces underneath are refcounted and shared, so the second
  // stack is a set of caches rather than a second copy of the fonts.
  static thread_local FontStack stack;
  return stack;
}

// One font for everything drawn through here, handed over by the app at
// startup. It lived in nodes::Text, which was fine until anything other than
// a Text node wanted to measure a string.
inline skia::SkFont *&defaultFont() {
  static skia::SkFont *font = nullptr;
  return font;
}

// The alpha a colour already carries, times the one the caller asked for.
// SkPaint::setAlphaf replaces rather than multiplies, so passing both without
// combining them turns a translucent colour opaque.
[[nodiscard]] inline float combinedAlpha(skia::SkColor color, float alpha) {
  return static_cast<float>((color >> 24) & 0xffu) / 255.0f * alpha;
}

// Colour4.Lighten: each channel scaled towards white, alpha left alone. What
// a hovered tab or row is drawn in.
[[nodiscard]] inline skia::SkColor lighten(skia::SkColor colour, float amount) {
  const auto channel = [amount](std::uint32_t v) {
    return static_cast<std::uint8_t>(
        std::min(255.0f, static_cast<float>(v) * (1.0f + amount)));
  };
  return skia::colorSetARGB(
      (colour >> 24) & 0xffu, channel((colour >> 16) & 0xffu),
      channel((colour >> 8) & 0xffu), channel(colour & 0xffu));
}

// FillMode.Fill: the image is cropped to the destination's aspect ratio
// rather than squashed into it, which is what a cover or a thumbnail wants
// and what drawImageRect will not do on its own.
inline void imageFilled(skia::SkCanvas *canvas, const skia::SkImage *image,
                        const skia::SkRect &dst, float alpha = 1.0f) {
  if (canvas == nullptr || image == nullptr) {
    return;
  }
  const float iw = static_cast<float>(image->width());
  const float ih = static_cast<float>(image->height());
  if (iw <= 0.0f || ih <= 0.0f) {
    return;
  }
  const float scale = std::max(dst.width() / iw, dst.height() / ih);
  const float srcW = dst.width() / scale;
  const float srcH = dst.height() / scale;
  const skia::SkRect src = skia::SkRect::MakeXYWH(
      (iw - srcW) * 0.5f, (ih - srcH) * 0.5f, srcW, srcH);
  skia::SkPaint p;
  p.setAlphaf(alpha);
  canvas->drawImageRect(
      image, src, dst, skia::SkSamplingOptions(skia::SkFilterMode::kLinear),
      alpha < 1.0f ? &p : nullptr, skia::SkCanvas::kStrict_SrcRectConstraint);
}

// Text whose size is animating -- a logo on the beat, a judgement popping --
// has to be drawn without grid fitting and without rounded advances. Every
// outline snaps to the pixel grid at its own threshold as the size passes
// through it, and every advance rounds to a whole pixel at its own, so the
// letters stop moving together: one jumps while its neighbours stay, which
// reads as a single twitching letter rather than as text being resampled.
//
// Held for the duration of the draw and put back afterwards, because static
// text wants exactly the opposite -- grid fitting is what makes a label at
// 11 points legible on a screen with no pixels to spare.
class SmoothScaling {
public:
  explicit SmoothScaling(skia::SkFont &font)
      : fFont(&font), fSubpixel(font.isSubpixel()),
        fLinearMetrics(font.isLinearMetrics()), fHinting(font.getHinting()) {
    font.setSubpixel(true);
    font.setLinearMetrics(true);
    font.setHinting(skia::kNoHinting);
  }
  ~SmoothScaling() {
    fFont->setSubpixel(fSubpixel);
    fFont->setLinearMetrics(fLinearMetrics);
    fFont->setHinting(fHinting);
  }
  SmoothScaling(const SmoothScaling &) = delete;
  SmoothScaling &operator=(const SmoothScaling &) = delete;

private:
  skia::SkFont *fFont;
  bool fSubpixel;
  bool fLinearMetrics;
  skia::SkFontHinting fHinting;
};

// A two-stop gradient, drawn as a stack of bands. Skia's gradient shader is
// the obvious way and its header is not in this build's include set, so this
// is the way that needs nothing but a rectangle and a colour. One
// implementation, here, rather than one per screen: the beatmap cover and the
// logo each had their own, with their own step counts.
inline void bandedGradient(skia::SkCanvas *canvas, const skia::SkRect &rect,
                           skia::SkColor start, skia::SkColor end,
                           bool vertical, float alpha) {
  if (canvas == nullptr || rect.isEmpty()) {
    return;
  }
  // Enough that the seams are under a pixel on anything the client draws
  // gradients across, which is a logo and a header strip.
  constexpr int kBands = 48;
  const auto lerp = [](std::uint32_t a, std::uint32_t b, float t) {
    return static_cast<std::uint8_t>(
        static_cast<float>(a) +
        (static_cast<float>(b) - static_cast<float>(a)) * t);
  };
  skia::SkPaint p;
  p.setAntiAlias(false);
  for (int i = 0; i < kBands; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kBands - 1);
    const skia::SkColor colour =
        skia::colorSetARGB(lerp((start >> 24) & 0xffu, (end >> 24) & 0xffu, t),
                           lerp((start >> 16) & 0xffu, (end >> 16) & 0xffu, t),
                           lerp((start >> 8) & 0xffu, (end >> 8) & 0xffu, t),
                           lerp(start & 0xffu, end & 0xffu, t));
    p.setColor(colour);
    p.setAlphaf(combinedAlpha(colour, alpha));
    const float from = static_cast<float>(i) / static_cast<float>(kBands);
    const float to = static_cast<float>(i + 1) / static_cast<float>(kBands);
    canvas->drawRect(
        vertical ? skia::SkRect::MakeLTRB(
                       rect.fLeft, rect.fTop + rect.height() * from,
                       rect.fRight, rect.fTop + rect.height() * to + 1.0f)
                 : skia::SkRect::MakeLTRB(
                       rect.fLeft + rect.width() * from, rect.fTop,
                       rect.fLeft + rect.width() * to + 1.0f, rect.fBottom),
        p);
  }
}

inline void verticalGradient(skia::SkCanvas *canvas, const skia::SkRect &rect,
                             skia::SkColor top, skia::SkColor bottom,
                             float alpha = 1.0f) {
  bandedGradient(canvas, rect, top, bottom, true, alpha);
}

inline void horizontalGradient(skia::SkCanvas *canvas, const skia::SkRect &rect,
                               skia::SkColor left, skia::SkColor right,
                               float alpha = 1.0f) {
  bandedGradient(canvas, rect, left, right, false, alpha);
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

  void fillRounded(const skia::SkRect &rect, float radius, skia::SkColor color,
                   float alpha = 1.0f) const {
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
    // Antialiased like everything else here. It was not, which only showed
    // once the listing's own rect() -- which was -- started coming through
    // this one: a bar at a fractional scroll offset picked up a hard edge.
    p.setAntiAlias(true);
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

  // bold sits after alpha rather than beside size, where it would read
  // better, because these had callers before they could embolden anything
  // and moving it would have turned every alpha silently into a weight.
  [[nodiscard]] float measure(const std::string &text, float size,
                              bool bold = false) const {
    fFont->setSize(size);
    fonts().applyWeight(*fFont, bold);
    const float width = fonts().measure(*fFont, text);
    fonts().applyWeight(*fFont, false);
    return width;
  }

  void text(const std::string &str, float x, float y, float size,
            skia::SkColor color, float alpha = 1.0f, bool bold = false) const {
    fFont->setSize(size);
    fonts().applyWeight(*fFont, bold);
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(combinedAlpha(color, alpha));
    fonts().draw(fCanvas, *fFont, str, x, y, p);
    fonts().applyWeight(*fFont, false);
  }

  void textClipped(const std::string &str, float x, float y, float maxW,
                   float size, skia::SkColor color, float alpha = 1.0f,
                   bool bold = false) const {
    fCanvas->save();
    fCanvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(x), static_cast<int>(y - size * 1.2f),
        static_cast<int>(maxW), static_cast<int>(size * 1.8f)));
    this->text(str, x, y, size, color, alpha, bold);
    fCanvas->restore();
  }

  // Centred, but clipped to a width so a long title cannot run past a panel.
  void textCenteredClipped(const std::string &str, float cx, float y,
                           float maxW, float size, skia::SkColor color,
                           float alpha = 1.0f, bool bold = false) const {
    fCanvas->save();
    fCanvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(cx - maxW * 0.5f), static_cast<int>(y - size * 1.2f),
        static_cast<int>(maxW), static_cast<int>(size * 1.8f)));
    this->textCentered(str, cx, y, size, color, alpha, bold);
    fCanvas->restore();
  }

  void textCentered(const std::string &str, float cx, float y, float size,
                    skia::SkColor color, float alpha = 1.0f,
                    bool bold = false) const {
    const float w = this->measure(str, size, bold);
    this->text(str, cx - w * 0.5f, y, size, color, alpha, bold);
  }

  void imageFilled(const skia::SkImage *image, const skia::SkRect &dst,
                   float alpha = 1.0f) const {
    skiff::paint::imageFilled(fCanvas, image, dst, alpha);
  }

  void verticalGradient(const skia::SkRect &rect, skia::SkColor top,
                        skia::SkColor bottom, float alpha = 1.0f) const {
    skiff::paint::verticalGradient(fCanvas, rect, top, bottom, alpha);
  }

  // The longest prefix of `str` that fits in `width`, with a single-character
  // ellipsis where something was dropped. Cut on UTF-8 boundaries, so a
  // multi-byte character is never halved.
  [[nodiscard]] std::string elide(const std::string &str, float width,
                                  float size, bool bold = false) const {
    if (width <= 0.0f) {
      return {};
    }
    if (this->measure(str, size, bold) <= width) {
      return str;
    }
    static constexpr std::string_view kEllipsis = "\u2026";
    const float room =
        width - this->measure(std::string(kEllipsis), size, bold);
    if (room <= 0.0f) {
      return std::string(kEllipsis);
    }
    // Binary search on the cut, snapped outwards to a character boundary.
    const auto boundary = [&str](std::size_t at) {
      while (at > 0 && (static_cast<unsigned char>(str[at]) & 0xc0u) == 0x80u) {
        --at;
      }
      return at;
    };
    std::size_t low = 0;
    std::size_t high = str.size();
    while (low < high) {
      const std::size_t mid = boundary(low + (high - low + 1) / 2);
      if (mid == low) {
        break;
      }
      if (this->measure(str.substr(0, mid), size, bold) <= room) {
        low = mid;
      } else {
        high = mid - 1;
      }
    }
    return str.substr(0, boundary(low)) + std::string(kEllipsis);
  }

  // The text broken into lines that each fit in `width`, split at spaces. A
  // word longer than the width gets a line of its own and overhangs, which is
  // what a browser does with an unbreakable string.
  [[nodiscard]] std::vector<std::string> wrap(const std::string &str,
                                              float width, float size,
                                              bool bold = false) const {
    std::vector<std::string> lines;
    if (str.empty()) {
      return lines;
    }
    if (width <= 0.0f) {
      lines.push_back(str);
      return lines;
    }
    std::string line;
    std::size_t at = 0;
    while (at < str.size()) {
      const std::size_t space = str.find(' ', at);
      const std::string word =
          str.substr(at, space == std::string::npos ? space : space - at);
      const std::string candidate = line.empty() ? word : line + " " + word;
      if (!line.empty() && this->measure(candidate, size, bold) > width) {
        lines.push_back(line);
        line = word;
      } else {
        line = candidate;
      }
      if (space == std::string::npos) {
        break;
      }
      at = space + 1;
    }
    if (!line.empty()) {
      lines.push_back(line);
    }
    return lines;
  }

  // Text that says it was cut rather than stopping mid-glyph.
  void textElided(const std::string &str, float x, float y, float maxW,
                  float size, skia::SkColor color, float alpha = 1.0f,
                  bool bold = false) const {
    this->text(this->elide(str, maxW, size, bold), x, y, size, color, alpha,
               bold);
  }

  void textElidedIn(const skia::SkRect &box, const std::string &str, float size,
                    skia::SkColor color, float alpha = 1.0f, bool bold = false,
                    float inset = 0.0f) const {
    this->textElided(str, box.fLeft + inset, this->middleBaseline(box, size),
                     box.width() - inset * 2.0f, size, color, alpha, bold);
  }

  // The baseline that puts a line of this size in the middle of a box, from
  // the font's ascent and descent. The alternative is a constant added to the
  // middle, which has to be picked per size and per face by eye.
  [[nodiscard]] float middleBaseline(const skia::SkRect &box,
                                     float size) const {
    fFont->setSize(size);
    skia::SkFontMetrics metrics;
    fFont->getMetrics(&metrics);
    return box.centerY() - (metrics.fAscent + metrics.fDescent) * 0.5f;
  }

  // Text in a box: down the middle vertically, and clipped to the box so a
  // long string cannot run out of it. `inset` is taken off both ends.
  void textIn(const skia::SkRect &box, const std::string &str, float size,
              skia::SkColor color, float alpha = 1.0f, bool bold = false,
              float inset = 0.0f) const {
    this->textClipped(str, box.fLeft + inset, this->middleBaseline(box, size),
                      box.width() - inset * 2.0f, size, color, alpha, bold);
  }

  // The same, centred across the box as well.
  void textCentredIn(const skia::SkRect &box, const std::string &str,
                     float size, skia::SkColor color, float alpha = 1.0f,
                     bool bold = false, float inset = 0.0f) const {
    this->textCenteredClipped(
        str, box.centerX(), this->middleBaseline(box, size),
        box.width() - inset * 2.0f, size, color, alpha, bold);
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

} // namespace skiff::paint
