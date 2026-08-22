export module skiff.nodes:text;

import std;
import skia;
import skiff.paint;
import skiff.scene;

namespace skiff::nodes {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
using skiff::scene::Style;
} // namespace skiff::nodes

export namespace skiff::nodes {

// A line of text. Auto-sizes to what it draws, so a flow can lay it out
// without anyone measuring by hand.
class Text : public skiff::scene::TypedDrawable<Text> {
public:
  Text(std::string text, float size, skia::SkColor colour, bool bold = false)
      : fText(std::move(text)), fSize(size), fColour(colour), fBold(bold) {}

  void setText(std::string text) {
    if (text == fText) {
      return;
    }
    fText = std::move(text);
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  void setColour(skia::SkColor colour) {
    if (colour == fColour) {
      return;
    }
    fColour = colour;
    this->markDamaged();
  }
  void setFontSize(float size) {
    if (size == fSize) {
      return;
    }
    fSize = size;
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  void setBold(bool bold) {
    if (bold == fBold) {
      return;
    }
    fBold = bold;
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }
  [[nodiscard]] float fontSize() const noexcept { return fSize; }
  [[nodiscard]] skia::SkColor colour() const noexcept { return fColour; }
  [[nodiscard]] bool bold() const noexcept { return fBold; }

  // Set to clip instead of auto-sizing: the text is cut to the given width.
  // fMaxWidth is the drawable's own, so a Spec can set it as well.
  void setMaxWidth(float width) {
    if (width == fMaxWidth) {
      return;
    }
    fMaxWidth = width;
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }

  // Broken across lines at spaces instead of running past the width. The
  // width comes from wherever the drawable's does -- a maximum, a relative
  // size, or a flow that grew it.
  void setWrapped(bool wrapped) {
    if (wrapped == fWrapped) {
      return;
    }
    fWrapped = wrapped;
    fMeasuredSize = -1.0f;
    this->invalidateLayout();
  }

  // What it says when it does not say the whole thing, rather than stopping
  // mid-glyph. Ignored when wrapped, since nothing is dropped then.
  void setElided(bool elided) {
    if (elided == fElided) {
      return;
    }
    fElided = elided;
    this->markDamaged();
  }

  static void setFont(skia::SkFont *font) {
    skiff::paint::defaultFont() = font;
  }

protected:
  void applyNodeStyle(const Style &style, bool active) override {
    if (!active && !fNodeStyleActive) {
      return;
    }
    if (active && !fNodeStyleActive) {
      fBaseSize = fSize;
      fBaseColour = fColour;
      fBaseBold = fBold;
    }
    const float size = active ? style.fontSize.value_or(fBaseSize) : fBaseSize;
    const skia::SkColor colour =
        active ? style.colour.value_or(fBaseColour) : fBaseColour;
    const bool bold = active ? style.fontBold.value_or(fBaseBold) : fBaseBold;
    if (size != fSize || bold != fBold) {
      fSize = size;
      fBold = bold;
      fMeasuredSize = -1.0f;
      this->invalidateLayout();
    }
    if (colour != fColour) {
      fColour = colour;
      this->markDamaged();
    }
    fNodeStyleActive = active;
  }

  // Text sizes itself: a flow then reads the size off like any other child.
  void measure(const skia::SkRect &parent) override {
    if (fMeasuredSize == fSize && !fWrapped) {
      return; // already measured at this size, and the text has not changed
    }
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(nullptr, *font);
    if (fWrapped) {
      const float room = this->roomFor(parent);
      fLines = p.wrap(fText, room, fSize, fBold);
      fHeight = static_cast<float>(std::max<std::size_t>(1, fLines.size())) *
                fSize * 1.25f;
      if (!hasX(fGrowAxes)) {
        fWidth = room;
      }
      fMeasuredSize = fSize;
      return;
    }
    const float measured = p.measure(fText, fSize, fBold);
    // A text sized by its flow or parent clips to the width it was given
    // rather than replacing that width with the measured glyphs.
    if (!hasX(fGrowAxes) && !hasX(fRelativeSizeAxes)) {
      fWidth = fMaxWidth > 0.0f ? std::min(fMaxWidth, measured) : measured;
    }
    fHeight = fSize * 1.25f;
    fMeasuredSize = fSize;
  }

  // The width a wrapped line has to fit into: whatever the drawable has been
  // told, in the order the layout would resolve it.
  [[nodiscard]] float roomFor(const skia::SkRect &parent) const {
    if (fMaxWidth > 0.0f) {
      return fMaxWidth;
    }
    if (hasX(fRelativeSizeAxes)) {
      return parent.width() * fWidth - fMargin.totalX();
    }
    return fWidth > 0.0f ? fWidth : parent.width() - fMargin.totalX();
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr || fText.empty()) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    const int saved = canvas->save();
    if (fWrapped) {
      float y = fBounds.fTop + fSize;
      for (const std::string &line : fLines) {
        p.text(line, fBounds.fLeft, y, fSize, fColour, alpha, fBold);
        y += fSize * 1.25f;
      }
      canvas->restoreToCount(saved);
      return;
    }
    if (fMaxWidth > 0.0f || hasX(fGrowAxes) || hasX(fRelativeSizeAxes)) {
      canvas->clipRect(fBounds, true);
    }
    // The baseline sits at the top plus the ascent share of the line box.
    if (fElided) {
      p.textElided(fText, fBounds.fLeft, fBounds.fTop + fSize, fBounds.width(),
                   fSize, fColour, alpha, fBold);
    } else {
      p.text(fText, fBounds.fLeft, fBounds.fTop + fSize, fSize, fColour, alpha,
             fBold);
    }
    canvas->restoreToCount(saved);
  }

private:
  std::string fText;
  float fSize;
  skia::SkColor fColour;
  bool fBold;
  bool fWrapped = false;
  bool fElided = false;
  std::vector<std::string> fLines;
  float fMeasuredSize = -1.0f; // the size the cached width was measured at
  float fBaseSize = 0.0f;
  skia::SkColor fBaseColour = 0;
  bool fBaseBold = false;
  bool fNodeStyleActive = false;
};

} // namespace skiff::nodes
