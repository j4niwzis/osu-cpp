export module skiff.widgets;

import std;
import skia;
import skiff.paint;
import skiff.scene;

// Widgets, as distinct from drawables: skiff.scene and skiff.nodes are the
// primitives -- a box, a string, a flow, a scroll -- and this is the layer of
// things a screen is actually made of, already knowing how they behave and
// what they look like.
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
  // A caption beside a control, as distinct from the control's own text.
  skia::SkColor fLabel = skia::colorSetARGB(255, 219, 233, 240);
  // Secondary -- icons, unselected tabs -- and fainter still, placeholders.
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

// ---- tab bar -------------------------------------------------------------

// A row of text tabs, one of them selected: lazer's OsuTabControl, and the
// two hand-written copies of it in the beatmap listing. It knows how to wrap
// when it runs out of width, which of its own tabs the pointer is on, and
// how to say that one of them was clicked. What a tab means is the caller's
// business, and so is anything drawn beside the selected one, which is what
// drawDecoration is for.
class TabBar : public Drawable {
public:
  struct Tab {
    std::string fLabel;
    int fValue = 0;
  };

  TabBar() {
    fRelativeSizeAxes = Axes::kX;
    fWidth = 1.0f;
  }

  Theme fTheme = theme();

  std::string fHeader;       // caption in the column to the left, may be empty
  float fHeaderWidth = 0.0f; // where the tabs start, header or no header
  float fFontSize = 13.0f;
  float fLineHeight = 16.0f;
  float fBaseline = -1.0f; // within a line box; below zero means fFontSize
  float fSpacing = 10.0f;
  float fSelectedExtra = 0.0f; // room after the selected tab for a decoration
  bool fWrap = true;
  std::function<void(int)> fOnSelect;
  // Which tabs read as selected. Left unset it is the one whose value is
  // selected(); a bar where several can be on at once -- a set of toggles
  // laid out as tabs -- answers for itself instead.
  std::function<bool(int)> fIsActive;

  void setTabs(std::vector<Tab> tabs) {
    if (tabs.size() == fTabs.size() &&
        std::equal(tabs.begin(), tabs.end(), fTabs.begin(),
                   [](const Tab &a, const Tab &b) {
                     return a.fValue == b.fValue && a.fLabel == b.fLabel;
                   })) {
      return;
    }
    fTabs = std::move(tabs);
    this->invalidateLayout();
  }

  void setSelected(int value) {
    if (value == fSelected) {
      return;
    }
    fSelected = value;
    // The selected tab is drawn bold and may carry a decoration, both of
    // which move the tabs after it along.
    this->invalidateLayout();
  }
  [[nodiscard]] int selected() const noexcept { return fSelected; }
  [[nodiscard]] std::span<const Tab> tabs() const noexcept { return fTabs; }

  // Where a tab ended up, in screen coordinates.
  [[nodiscard]] skia::SkRect tabBounds(std::size_t i) const {
    if (i >= fRects.size()) {
      return skia::SkRect::MakeEmpty();
    }
    const skia::SkRect &local = fRects[i];
    return skia::SkRect::MakeXYWH(fBounds.fLeft + local.fLeft,
                                  fBounds.fTop + local.fTop, local.width(),
                                  local.height());
  }

protected:
  // The height depends on how the tabs wrap, which depends on the width this
  // has been given, so it is worked out here where that is known and the
  // positions are kept for drawing and for hit testing.
  void measure(const skia::SkRect &parent) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(nullptr, *font);
    const float width =
        hasX(fRelativeSizeAxes) ? parent.width() * fWidth : fWidth;
    float x = fHeaderWidth;
    float y = 0.0f;
    fRects.clear();
    fRects.reserve(fTabs.size());
    for (const Tab &tab : fTabs) {
      // Measured bold either way, so selecting one does not shuffle the row.
      const float w = p.measure(tab.fLabel, fFontSize, true);
      if (fWrap && x > fHeaderWidth && x + w > width) {
        x = fHeaderWidth;
        y += fLineHeight;
      }
      fRects.push_back(skia::SkRect::MakeXYWH(x, y, w, fLineHeight));
      x += w + fSpacing + (this->activeTab(tab) ? fSelectedExtra : 0.0f);
    }
    fHeight = y + fLineHeight;
  }

  // The bar lights the tab under the pointer, so moving between two tabs of
  // the same bar changes what it draws while the bar itself stays hovered.
  // Nothing else in the tree would notice that.
  void update(double) override {
    const int hot = this->tabAt(this->hoverX(), this->hoverY());
    if (hot != fHotTab) {
      fHotTab = hot;
      this->markDamaged();
    }
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    const float baseline = fBaseline >= 0.0f ? fBaseline : fFontSize;
    if (!fHeader.empty()) {
      p.text(fHeader, fBounds.fLeft, fBounds.fTop + baseline, fFontSize,
             fTheme.fLabel, alpha);
    }
    for (std::size_t i = 0; i < fTabs.size(); ++i) {
      const bool active = this->activeTab(fTabs[i]);
      const skia::SkRect box = this->tabBounds(i);
      skia::SkColor colour = active ? fTheme.fText : fTheme.fTextDim;
      if (static_cast<int>(i) == fHotTab) {
        colour = skiff::paint::lighten(colour, 0.2f);
      }
      p.text(fTabs[i].fLabel, box.fLeft, box.fTop + baseline, fFontSize, colour,
             alpha, active);
      if (active) {
        this->drawDecoration(canvas, box, alpha);
      }
    }
  }

  bool acceptsInput() const override { return true; }

  bool onClick(float x, float y) override {
    const int hit = this->tabAt(x, y);
    if (hit < 0) {
      return false;
    }
    if (fOnSelect) {
      fOnSelect(fTabs[static_cast<std::size_t>(hit)].fValue);
    }
    return true;
  }

  // Drawn after the selected tab, given its box. A sort direction chevron, a
  // count, an underline -- whatever the caller puts there.
  virtual void drawDecoration(skia::SkCanvas *, const skia::SkRect &, float) {}

  [[nodiscard]] bool activeTab(const Tab &tab) const {
    return fIsActive ? fIsActive(tab.fValue) : tab.fValue == fSelected;
  }

  [[nodiscard]] int tabAt(float x, float y) const {
    for (std::size_t i = 0; i < fRects.size(); ++i) {
      if (this->tabBounds(i).contains(x, y)) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

private:
  std::vector<Tab> fTabs;
  std::vector<skia::SkRect> fRects; // relative to this bar
  int fSelected = -1;
  int fHotTab = -1;
};

} // namespace skiff::widgets
