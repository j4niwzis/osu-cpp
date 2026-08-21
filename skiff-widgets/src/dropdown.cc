export module skiff.widgets.dropdown;

import std;
import skia;
import skiff.paint;
import skiff.scene;
export import skiff.widgets.theme;

namespace skiff::widgets {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::widgets

export namespace skiff::widgets {

// The open half of a dropdown: a plate with a row per option, the current one
// held at full strength and the one under the pointer lit.
//
// Only the open half, because that is the half that is the same everywhere.
// The closed control is a row in a settings panel one place and a labelled
// box with a chevron another, and both of those are the screen's own; what
// they share is what happens after the click. Where the list goes is the
// caller's too -- a list belongs to the control that opened it and has to be
// drawn over everything below it, which is a question about the tree.
//
// Geometry is one formula, exposed, because the control that opens the list
// has to size and place it before the list exists to ask.
class DropdownList : public Drawable {
public:
  Theme fTheme = theme();

  float fPitch = 26.0f;     // from the top of one row to the top of the next
  float fRowHeight = 24.0f; // the row itself, the rest being the gap
  float fInset = 2.0f;      // above the first row and below the last
  float fFontSize = 13.0f;
  float fPlateRadius = 6.0f;
  float fRowRadius = 6.0f;
  float fTextInset = 12.0f;
  float fBaseline = 4.0f; // below the row's middle, not derived from the size
  float fDimAlpha = 0.8f; // what an option that is not the current one is

  std::vector<std::string> fOptions;
  int fCurrent = -1;
  std::function<void(int)> fOnChoose;

  // How tall a list of that many options comes out, for the control that has
  // to reserve the space before there is a list to ask.
  [[nodiscard]] static float heightFor(std::size_t options, float pitch = 26.0f,
                                       float inset = 2.0f) {
    return static_cast<float>(options) * pitch + inset * 2.0f;
  }

  // Where an option sits inside a list occupying that rectangle.
  [[nodiscard]] static skia::SkRect
  optionBox(const skia::SkRect &list, std::size_t option, float pitch = 26.0f,
            float rowHeight = 24.0f, float inset = 2.0f) {
    return skia::SkRect::MakeXYWH(
        list.fLeft, list.fTop + inset + static_cast<float>(option) * pitch,
        list.width(), rowHeight);
  }

  [[nodiscard]] skia::SkRect optionBox(std::size_t option) const {
    return optionBox(fBounds, option, fPitch, fRowHeight, fInset);
  }

  void setOptions(std::vector<std::string> options) {
    if (options == fOptions) {
      return;
    }
    fOptions = std::move(options);
    this->markDamaged();
  }

  void setCurrent(int current) {
    if (current == fCurrent) {
      return;
    }
    fCurrent = current;
    this->markDamaged();
  }

protected:
  // The list lights the row under the pointer, which changes what it draws
  // while the list itself stays hovered. Nothing else in the tree notices.
  void update(double) override {
    const int hot = this->optionAt(this->hoverX(), this->hoverY());
    if (hot != fHotOption) {
      fHotOption = hot;
      this->markDamaged();
    }
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkFont *font = skiff::paint::defaultFont();
    if (font == nullptr || fOptions.empty()) {
      return;
    }
    const skiff::paint::Painter p(canvas, *font);
    // Its own backing plate, since what is underneath it stays where it is.
    p.fillRounded(fBounds, fPlateRadius, fTheme.fSurface, alpha);
    for (std::size_t o = 0; o < fOptions.size(); ++o) {
      const skia::SkRect row = this->optionBox(o);
      const bool hot = static_cast<int>(o) == fHotOption;
      p.fillRounded(row, fRowRadius,
                    hot ? fTheme.fSurfaceActive : fTheme.fSurfaceHover, alpha);
      p.textClipped(
          fOptions[o], row.fLeft + fTextInset, row.centerY() + fBaseline,
          row.width() - fTextInset * 2.0f, fFontSize, fTheme.fText,
          alpha * (static_cast<int>(o) == fCurrent ? 1.0f : fDimAlpha));
    }
  }

  bool acceptsInput() const override { return fVisible; }

  // A click between rows is still a click on the list, and is swallowed: an
  // open dropdown covers what is under it, and the thing under it must not
  // receive what was aimed at the gap.
  bool onClick(float x, float y) override {
    const int option = this->optionAt(x, y);
    if (option >= 0 && fOnChoose) {
      fOnChoose(option);
    }
    return true;
  }

  [[nodiscard]] int optionAt(float x, float y) const {
    for (std::size_t o = 0; o < fOptions.size(); ++o) {
      if (this->optionBox(o).contains(x, y)) {
        return static_cast<int>(o);
      }
    }
    return -1;
  }

private:
  int fHotOption = -1;
};

} // namespace skiff::widgets
