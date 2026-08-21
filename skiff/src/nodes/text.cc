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
} // namespace skiff::nodes

export namespace skiff::nodes {

// A line of text. Auto-sizes to what it draws, so a flow can lay it out
// without anyone measuring by hand.
class Text : public Drawable {
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
  void setColour(skia::SkColor colour) { fColour = colour; }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }

  // Set to clip instead of auto-sizing: the text is cut to the given width.
  void setMaxWidth(float width) {
    fMaxWidth = width;
    fMeasuredSize = -1.0f;
  }

  static void setFont(skia::SkFont *font) {
    skiff::paint::defaultFont() = font;
  }

protected:
  // Text sizes itself: a flow then reads the size off like any other child.
  void measure(const skia::SkRect &) override {
    if (fMeasuredSize == fSize) {
      return; // already measured at this size, and the text has not changed
    }
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    font->setSize(fSize);
    skiff::paint::fonts().applyWeight(*font, fBold);
    const float measured = skiff::paint::fonts().measure(*font, fText);
    skiff::paint::fonts().applyWeight(*font, false);
    fWidth = fMaxWidth > 0.0f ? std::min(fMaxWidth, measured) : measured;
    fHeight = fSize * 1.25f;
    fMeasuredSize = fSize;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr || fText.empty()) {
      return;
    }
    font->setSize(fSize);
    skiff::paint::fonts().applyWeight(*font, fBold);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fColour);
    paint.setAlphaf(alpha);
    const int saved = canvas->save();
    if (fMaxWidth > 0.0f) {
      canvas->clipRect(fBounds, true);
    }
    // The baseline sits at the top plus the ascent share of the line box.
    skiff::paint::fonts().draw(canvas, *font, fText, fBounds.fLeft,
                               fBounds.fTop + fSize, paint);
    canvas->restoreToCount(saved);
    skiff::paint::fonts().applyWeight(*font, false);
  }

private:
  std::string fText;
  float fSize;
  skia::SkColor fColour;
  bool fBold;
  float fMaxWidth = 0.0f;
  float fMeasuredSize = -1.0f; // the size the cached width was measured at
};

} // namespace skiff::nodes
