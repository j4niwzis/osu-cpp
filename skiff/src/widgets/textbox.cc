export module skiff.widgets:textbox;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import :theme;

namespace skiff::widgets {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::widgets

export namespace skiff::widgets {

// A single line of editable text: OsuTextBox, AdwEntryRow. It owns the string
// and reports changes; the keyboard belongs to whoever is routing input, so
// the screen still decides what a keystroke means and pushes the result back
// through setText.
class TextBox : public Drawable {
public:
  explicit TextBox(std::string placeholder = {})
      : fPlaceholder(std::move(placeholder)) {
    fRelativeSizeAxes = Axes::kX;
    fWidth = 1.0f;
    fHeight = fTheme.fRowHeight;
  }

  Theme fTheme = theme();
  std::string fPlaceholder;
  bool fSearchIcon = false; // the magnifier lazer puts in its search boxes

  void setText(std::string text) {
    if (text == fText) {
      return;
    }
    fText = std::move(text);
    this->markDamaged();
  }
  [[nodiscard]] const std::string &text() const noexcept { return fText; }

  // The caret is usually the only thing on a screen that changes without
  // being touched, so it marks the box when it flips and nothing else: the
  // frame after a flip repaints one rectangle, and the frames between are
  // not drawn at all. Off screen it does not blink, because a caret nobody
  // can see is not worth a frame.
  void tickCaret(double nowMs, bool visible) {
    const bool shown = visible && std::fmod(nowMs, 1000.0) < 600.0;
    if (shown != fCaretShown) {
      fCaretShown = shown;
      this->markDamaged();
    }
  }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(fBounds, fTheme.fCorner, fTheme.fSurface, alpha);

    float textLeft = fBounds.fLeft + fTheme.fPaddingX;
    if (fSearchIcon) {
      skia::SkPaint icon;
      icon.setAntiAlias(true);
      icon.setStyle(skia::kStrokeStyle);
      icon.setStrokeWidth(1.8f);
      icon.setColor(fTheme.fTextDim);
      icon.setAlphaf(alpha);
      const float ix = fBounds.fLeft + fTheme.fPaddingX + 6.0f;
      const float iy = fBounds.centerY();
      canvas->drawCircle(ix, iy - 1.0f, 5.5f, icon);
      canvas->drawLine(ix + 4.0f, iy + 3.0f, ix + 8.0f, iy + 7.0f, icon);
      textLeft = ix + 14.0f;
    }

    const float baseline = fBounds.centerY() + fTheme.fFontSize * 0.375f;
    const float room = fBounds.fRight - textLeft - fTheme.fPaddingX;
    if (fText.empty()) {
      p.text(fPlaceholder, textLeft, baseline, fTheme.fFontSize,
             fTheme.fTextFaint, alpha * 0.6f);
    } else {
      p.textClipped(fText, textLeft, baseline, room, fTheme.fFontSize,
                    fTheme.fText, alpha);
    }
    if (fCaretShown) {
      const float cx =
          textLeft + std::min(room, p.measure(fText, fTheme.fFontSize)) + 2.0f;
      p.fillRect(skia::SkRect::MakeXYWH(cx, fBounds.centerY() - 9.0f, 1.5f,
                                        fTheme.fFontSize + 2.0f),
                 fTheme.fText, alpha * 0.8f);
    }
  }

private:
  std::string fText;
  bool fCaretShown = false;
};

} // namespace skiff::widgets
