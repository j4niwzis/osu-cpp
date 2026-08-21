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
class DropdownList : public skiff::nodes::FillFlow {
public:
  DropdownList() : FillFlow(Direction::kVertical, 0.0f, kRowGap) {
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
  float fBaseline = 4.0f; // below a row's middle, not derived from the size
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
      p.fillRounded(fBounds, fList->fRowRadius,
                    fHovered ? theme.fSurfaceActive : theme.fSurfaceHover,
                    alpha);
      const bool chosen = static_cast<int>(fIndex) == fList->fCurrent;
      p.textClipped(fList->fLabels[fIndex], fBounds.fLeft + fList->fTextInset,
                    fBounds.centerY() + fList->fBaseline,
                    fBounds.width() - fList->fTextInset * 2.0f,
                    fList->fFontSize, theme.fText,
                    alpha * (chosen ? 1.0f : fList->fDimAlpha));
    }

    bool acceptsInput() const override { return true; }

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
