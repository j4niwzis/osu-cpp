export module skiff.widgets.dropdown;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
export import skiff.widgets.theme;

namespace skiff::widgets {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::widgets

export namespace skiff::widgets {

// The closed half of a dropdown. The label, current value and open state are
// data; hover, hit testing and the chevron belong to the widget. Keeping this
// beside DropdownList gives screens both reusable pieces without forcing a
// particular placement or state model on them.
class DropdownButton : public skiff::scene::TypedDrawable<DropdownButton> {
public:
  DropdownButton(std::string label = {}, std::string value = {})
      : fLabel(std::move(label)), fValue(std::move(value)) {
    fHeight = 30.0f;
  }

  Theme fTheme = theme();
  float fLabelWidth = 52.0f;
  float fChevronWidth = 22.0f;
  float fStrokeWidth = 1.0f;
  float fOpenStrokeWidth = 2.0f;
  float fLabelAlpha = 0.5f;
  float fValueAlpha = 0.95f;
  std::function<void()> fOnOpen;

  void setLabel(std::string label) {
    if (label == fLabel) {
      return;
    }
    fLabel = std::move(label);
    this->markDamaged();
  }

  void setValue(std::string value) {
    if (value == fValue) {
      return;
    }
    fValue = std::move(value);
    this->markDamaged();
  }

  void setOpen(bool open) {
    if (open == fOpen) {
      return;
    }
    fOpen = open;
    this->markDamaged();
  }
  [[nodiscard]] bool open() const noexcept { return fOpen; }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(fBounds, fTheme.fCorner,
                  fHovered || fOpen ? fTheme.fSurfaceHover : fTheme.fSurface,
                  alpha);
    p.strokeRounded(fBounds, fTheme.fCorner,
                    fOpen ? fTheme.fAccent : fTheme.fSurfaceActive,
                    fOpen ? fOpenStrokeWidth : fStrokeWidth, alpha);
    const float baseline = p.middleBaseline(fBounds, fTheme.fFontSize);
    p.textClipped(fLabel, fBounds.fLeft + fTheme.fPaddingX, baseline,
                  fLabelWidth, fTheme.fFontSize, fTheme.fLabel,
                  alpha * fLabelAlpha);
    const float valueLeft =
        fBounds.fLeft + fTheme.fPaddingX + fLabelWidth;
    p.textClipped(fValue, valueLeft, baseline,
                  std::max(0.0f, fBounds.fRight - valueLeft - fChevronWidth),
                  fTheme.fFontSize, fTheme.fText, alpha * fValueAlpha);

    skia::SkPaint triangle;
    triangle.setAntiAlias(true);
    triangle.setColor(fTheme.fText);
    triangle.setAlphaf(alpha * 0.7f);
    skia::SkPathBuilder path;
    const float cx = fBounds.fRight - fChevronWidth * 0.5f;
    const float cy = fBounds.centerY();
    if (fOpen) {
      path.moveTo(cx - 5.0f, cy + 2.5f);
      path.lineTo(cx + 5.0f, cy + 2.5f);
      path.lineTo(cx, cy - 3.5f);
    } else {
      path.moveTo(cx - 5.0f, cy - 2.5f);
      path.lineTo(cx + 5.0f, cy - 2.5f);
      path.lineTo(cx, cy + 3.5f);
    }
    path.close();
    canvas->drawPath(path.detach(), triangle);
  }

  bool acceptsInput() const override { return static_cast<bool>(fOnOpen); }
  bool hoverChangesAppearance() const override { return true; }

  bool onClick(float, float) override {
    if (!fOnOpen) {
      return false;
    }
    fOnOpen();
    return true;
  }

private:
  std::string fLabel;
  std::string fValue;
  bool fOpen = false;
};

// The open half of a dropdown: a plate with a row per option, the current one
// held at full strength and the one under the pointer lit.
//
// Only the open half, because that is the half that is the same everywhere.
// The closed control is a row in a settings panel one place and a labelled box
// with a chevron another, and both of those belong to their screen; what they
// share is what happens after the click.
//
// It is a flow of real rows rather than a rectangle that draws several. That
// is the difference between a widget that has to be told how tall it is and
// one that knows: the height comes from the children, each row is placed by
// the flow, each row is asked whether the click was in it, and each row
// notices the pointer arriving on its own. None of those is arithmetic anybody
// has to write down, and none of them can drift from the arithmetic somewhere
// else that used to have to agree with it.
class DropdownList
    : public skiff::scene::TypedDrawable<DropdownList,
                                         skiff::nodes::FillFlow> {
public:
  DropdownList()
      : TypedDrawable(Direction::kVertical, 0.0f, kRowGap) {
    fAutoSizeAxes = Axes::kY;
    // Two above the first row, four below the last, which is where the plate
    // ends. Asymmetric because that is how it has always looked.
    fPadding = {kTopInset, 0.0f, kBottomInset, 0.0f};
    fWrap = false;
  }

  Theme fTheme = theme();
  float fRowHeight = 24.0f;
  float fFontSize = 13.0f;
  float fPlateRadius = 6.0f;
  float fRowRadius = 6.0f;
  float fTextInset = 12.0f;
  float fDimAlpha = 0.8f; // an option that is not the current one
  std::function<void(int)> fOnChoose;

  void setOptions(std::vector<std::string> options) {
    if (options == fLabels) {
      return;
    }
    fLabels = std::move(options);
    this->clear();
    for (std::size_t i = 0; i < fLabels.size(); ++i) {
      this->add<Row>({.fillX = true, .height = fRowHeight}, this, i);
    }
    this->invalidateLayout();
  }

  void setCurrent(int current) {
    if (current == fCurrent) {
      return;
    }
    fCurrent = current;
    this->markDamaged();
  }
  [[nodiscard]] int current() const noexcept { return fCurrent; }

  void setExpanded(bool expanded) {
    if (expanded == fVisible) {
      return;
    }
    // Hiding must repaint the old plate before visibility makes it stop
    // contributing damage. Showing needs layout because hidden flow children
    // were deliberately skipped.
    this->markDamaged();
    fVisible = expanded;
    this->invalidateLayout();
  }
  [[nodiscard]] bool expanded() const noexcept { return fVisible; }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr || fLabels.empty()) {
      return;
    }
    // Its own backing plate, since what is underneath it stays where it is.
    // The rows are children and draw themselves over this.
    const skiff::paint::Painter p(canvas, *font);
    p.fillRounded(fBounds, fPlateRadius, fTheme.fSurface, alpha);
    p.strokeRounded(fBounds, fPlateRadius, fTheme.fAccent, 1.5f, alpha);
  }

  // A click that lands between two rows is still a click on the list, and is
  // swallowed: an open dropdown covers what is under it, and the thing under
  // it must not receive what was aimed at the gap. Rows are children, so they
  // are asked first and this is only reached by the gaps.
  bool acceptsInput() const override { return fVisible; }
  bool hoverChangesAppearance() const override { return false; }
  bool onClick(float, float) override { return true; }

private:
  static constexpr float kRowGap = 2.0f;
  static constexpr float kTopInset = 2.0f;
  static constexpr float kBottomInset = 4.0f;

  // One option. It lights when the pointer is on it, which the framework
  // tells it, and reports its own click, which the framework routes to it.
  class Row : public Drawable {
  public:
    Row(DropdownList *list, std::size_t index) : fList(list), fIndex(index) {}

  protected:
    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      skia::SkFont *font = skiff::paint::defaultFont();
      if (font == nullptr || fIndex >= fList->fLabels.size()) {
        return;
      }
      const Theme &theme = fList->fTheme;
      const skiff::paint::Painter p(canvas, *font);
      const bool chosen = static_cast<int>(fIndex) == fList->fCurrent;
      if (chosen || fHovered) {
        p.fillRounded(fBounds, fList->fRowRadius,
                      chosen ? theme.fAccent : theme.fSurfaceHover, alpha);
      }
      p.textIn(fBounds, fList->fLabels[fIndex], fList->fFontSize,
               chosen ? theme.fOnAccent : theme.fText,
               alpha * (chosen ? 1.0f : fList->fDimAlpha), false,
               fList->fTextInset);
    }

    bool acceptsInput() const override { return true; }
    bool hoverChangesAppearance() const override { return true; }

    bool onClick(float, float) override {
      if (fList->fOnChoose) {
        fList->fOnChoose(static_cast<int>(fIndex));
      }
      return true;
    }

  private:
    DropdownList *fList;
    std::size_t fIndex;
  };

  std::vector<std::string> fLabels;
  int fCurrent = -1;
};

} // namespace skiff::widgets
