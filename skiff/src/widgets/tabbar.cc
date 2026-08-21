export module skiff.widgets:tabbar;

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
