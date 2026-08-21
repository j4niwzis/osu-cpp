export module skiff.widgets;

import std;
import skia;
import skiff.paint;
import skiff.scene;

// Widgets, as distinct from drawables: skiff.scene and skiff.nodes are the
// primitives -- a box, a string, a flow, a scroll -- and this is the layer of
// things a screen is actually made of, already knowing how they behave and
// what they look like. GTK and libadwaita, in that order.
//
// A widget draws itself from a Theme rather than from constants baked into
// it, so a screen restyles by handing over a different Theme and not by
// subclassing. The default Theme is a dark neutral one; the client overwrites
// theme() at startup with osu!'s.
export namespace skiff::widgets {

using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Spec;

// ---- theme ---------------------------------------------------------------

struct Theme {
  skia::SkColor fSurface = skia::colorSetARGB(255, 46, 53, 56);
  skia::SkColor fSurfaceHover = skia::colorSetARGB(255, 57, 66, 70);
  skia::SkColor fSurfaceActive = skia::colorSetARGB(255, 69, 79, 84);
  skia::SkColor fText = skia::colorSetARGB(255, 255, 255, 255);
  // Secondary -- icons and labels -- and fainter still, for placeholders.
  skia::SkColor fTextDim = skia::colorSetARGB(255, 178, 190, 196);
  skia::SkColor fTextFaint = skia::colorSetARGB(255, 143, 156, 163);
  skia::SkColor fAccent = skia::colorSetARGB(255, 102, 204, 255);
  skia::SkColor fOnAccent = skia::colorSetARGB(255, 23, 26, 28);

  float fCorner = 5.0f;
  float fFontSize = 16.0f;
  float fRowHeight = 40.0f;
  float fPaddingX = 12.0f;
};

// The one every widget starts from. Set it once, before any tree is built.
inline Theme &theme() {
  static Theme t;
  return t;
}

// ---- text box ------------------------------------------------------------

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

// ---- button --------------------------------------------------------------

// A rounded rectangle with a label in it that calls something when clicked,
// and lightens under the pointer. AdwButton, and the five hand-written ones
// this replaces.
class Button : public Drawable {
public:
  Button(std::string label, std::function<void()> action)
      : fLabel(std::move(label)), fAction(std::move(action)) {
    fHeight = fTheme.fRowHeight;
  }

  Theme fTheme = theme();
  bool fPrimary = false; // filled in the accent rather than the surface
  bool fEnabled = true;

  void setLabel(std::string label) {
    if (label == fLabel) {
      return;
    }
    fLabel = std::move(label);
    this->markDamaged();
  }

protected:
  bool acceptsInput() const override { return fEnabled; }
  bool hoverChangesAppearance() const override { return true; }

  bool onClick(float x, float y) override {
    if (!fEnabled || !fBounds.contains(x, y)) {
      return false;
    }
    if (fAction) {
      fAction();
    }
    return true;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    skia::SkColor fill = fPrimary ? fTheme.fAccent : fTheme.fSurface;
    if (fHovered && fEnabled) {
      fill =
          fPrimary ? skiff::paint::lighten(fill, 0.12f) : fTheme.fSurfaceHover;
    }
    p.fillRounded(fBounds, fTheme.fCorner, fill,
                  alpha * (fEnabled ? 1.0f : 0.5f));
    p.textCentered(fLabel, fBounds.centerX(),
                   fBounds.centerY() + fTheme.fFontSize * 0.375f,
                   fTheme.fFontSize, fPrimary ? fTheme.fOnAccent : fTheme.fText,
                   alpha * (fEnabled ? 1.0f : 0.5f), true);
  }

private:
  std::string fLabel;
  std::function<void()> fAction;
};

} // namespace skiff::widgets
