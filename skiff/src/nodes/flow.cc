export module skiff.nodes:flow;

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

// FillFlowContainer: children laid end to end, wrapping when they run out of
// room, which is how lazer builds every list and row of filters.
class FillFlow : public Drawable {
public:
  enum class Direction : std::uint8_t { kHorizontal, kVertical };

  explicit FillFlow(Direction direction = Direction::kVertical)
      : fDirection(direction) {}
  // Spacing is part of what a flow *is*, so it can be given at construction
  // rather than in a setter call on the line after every one of them.
  FillFlow(Direction direction, float spacingX, float spacingY)
      : fSpacingX(spacingX), fSpacingY(spacingY), fDirection(direction) {}

  float fSpacingX = 0.0f;
  float fSpacingY = 0.0f;
  bool fWrap = true;
  bool fCentreRows = false; // rows centred in the container, as the cards are
  // Where children sit across the flow's axis, and how the room left over
  // along it is handed out. A child can override the first for itself with
  // fAlignSelf.
  using Align = skiff::scene::Align;
  enum class Justify : std::uint8_t {
    kStart,
    kMiddle,
    kEnd,
    kSpaceBetween,
    kSpaceAround
  };
  Align fCrossAlign = Align::kStart;
  Justify fJustify = Justify::kStart;

  void setSpacing(float x, float y) {
    fSpacingX = x;
    fSpacingY = y;
  }

protected:
  void layoutChildren() override {
    const skia::SkRect box = this->contentBox();
    if (fDirection == Direction::kVertical) {
      this->grow(box, false);
      // Everything is laid out once to learn its height, so the room the
      // column does not use is known before anything is placed in it.
      float used = 0.0f;
      int count = 0;
      for (auto &child : fChildren) {
        if (!child->fVisible) {
          continue;
        }
        child->fX = 0.0f;
        child->fY = 0.0f;
        child->layout(box);
        used += child->fBounds.height() + child->fMargin.totalY();
        ++count;
      }
      const Spread spread = this->spread(
          box.height(),
          used + fSpacingY * static_cast<float>(std::max(0, count - 1)), count);
      float y = spread.fStart;
      for (auto &child : fChildren) {
        if (!child->fVisible) {
          continue;
        }
        child->fAnchor = Anchor::kTopLeft;
        child->fOrigin = Anchor::kTopLeft;
        child->fX = 0.0f;
        child->fY = y;
        child->layout(box);
        if (this->alignOf(*child) != Align::kStart) {
          // Its width is only known once it has been laid out, so the offset
          // that centres it is applied on a second pass over the same child.
          child->fX = this->crossOffset(*child, box.width(),
                                        child->fBounds.width() +
                                            child->fMargin.totalX());
          child->layout(box);
        }
        y += child->fBounds.height() + child->fMargin.totalY() + fSpacingY +
             spread.fBetween;
      }
      return;
    }

    // Horizontal: measure each child, break rows at the edge, then place them.
    this->grow(box, true);
    std::vector<Drawable *> row;
    float rowWidth = 0.0f;
    float y = 0.0f;
    const auto flushRow = [&] {
      if (row.empty()) {
        return;
      }
      // Every child in this row has been laid out already, so the row's
      // height is known before anything is placed in it.
      float rowHeight = 0.0f;
      for (Drawable *child : row) {
        rowHeight = std::max(rowHeight,
                             child->fBounds.height() + child->fMargin.totalY());
      }
      // fCentreRows predates fJustify and means the same thing for a
      // wrapped row, so it reads as kMiddle when it is set.
      const Spread spread = fCentreRows
                                ? Spread{(box.width() - rowWidth) * 0.5f, 0.0f}
                                : this->spread(box.width(), rowWidth,
                                               static_cast<int>(row.size()));
      float x = spread.fStart;
      for (Drawable *child : row) {
        child->fAnchor = Anchor::kTopLeft;
        child->fOrigin = Anchor::kTopLeft;
        child->fX = x;
        child->fY = y + this->crossOffset(*child, rowHeight,
                                          child->fBounds.height() +
                                              child->fMargin.totalY());
        child->layout(box);
        x += child->fBounds.width() + child->fMargin.totalX() + fSpacingX +
             spread.fBetween;
      }
      y += rowHeight + fSpacingY;
      row.clear();
      rowWidth = 0.0f;
    };

    for (auto &child : fChildren) {
      if (!child->fVisible) {
        continue;
      }
      // Lay it out once against the box to learn its size.
      child->fX = 0.0f;
      child->fY = 0.0f;
      child->layout(box);
      const float width = child->fBounds.width() + child->fMargin.totalX();
      // Half a pixel of slack: four children at a quarter of the width each
      // add up to the width, and whether that comes out a hair over depends
      // on the arithmetic rather than on the layout.
      if (fWrap && !row.empty() &&
          rowWidth + fSpacingX + width > box.width() + 0.5f) {
        flushRow();
      }
      rowWidth += row.empty() ? width : fSpacingX + width;
      row.push_back(child.get());
    }
    flushRow();
  }

  // Children that grow take an equal share of what the rest leave along the
  // flow's axis. Their size is written here, before anything is placed, so
  // the placing pass sees a settled size like any other child's.
  //
  // A wrapping row is left alone: what is left over is only known once the
  // rows are decided, and the rows are decided by the widths this would be
  // choosing.
  // How far across the line a child sits: how much room the line has, less
  // how much the child takes.
  [[nodiscard]] Align alignOf(const Drawable &child) const {
    return child.fAlignSelf.value_or(fCrossAlign);
  }

  [[nodiscard]] float crossOffset(const Drawable &child, float line,
                                  float own) const {
    switch (this->alignOf(child)) {
    case Align::kMiddle:
      return (line - own) * 0.5f;
    case Align::kEnd:
      return line - own;
    case Align::kStart:
      break;
    }
    return 0.0f;
  }

  // Where a line starts and how much extra goes between its children, given
  // how much room it did not use.
  struct Spread {
    float fStart = 0.0f;
    float fBetween = 0.0f;
  };

  [[nodiscard]] Spread spread(float room, float used, int count) const {
    const float slack = std::max(0.0f, room - used);
    const auto gaps = static_cast<float>(std::max(0, count - 1));
    switch (fJustify) {
    case Justify::kMiddle:
      return {slack * 0.5f, 0.0f};
    case Justify::kEnd:
      return {slack, 0.0f};
    case Justify::kSpaceBetween:
      return {0.0f, gaps > 0.0f ? slack / gaps : 0.0f};
    case Justify::kSpaceAround:
      return {count > 0 ? slack / static_cast<float>(count) * 0.5f : 0.0f,
              count > 0 ? slack / static_cast<float>(count) : 0.0f};
    case Justify::kStart:
      break;
    }
    return {};
  }

  void grow(const skia::SkRect &box, bool horizontal) {
    if (horizontal && fWrap) {
      return;
    }
    const auto grows = [horizontal](const Drawable &child) {
      return horizontal ? hasX(child.fGrowAxes) : hasY(child.fGrowAxes);
    };
    int growers = 0;
    for (const auto &child : fChildren) {
      if (child->fVisible && grows(*child)) {
        ++growers;
      }
    }
    if (growers == 0) {
      return;
    }

    float taken = 0.0f;
    int visible = 0;
    for (auto &child : fChildren) {
      if (!child->fVisible) {
        continue;
      }
      ++visible;
      if (grows(*child)) {
        continue;
      }
      child->fX = 0.0f;
      child->fY = 0.0f;
      child->layout(box);
      taken += horizontal ? child->fBounds.width() + child->fMargin.totalX()
                          : child->fBounds.height() + child->fMargin.totalY();
    }
    const float gaps = (horizontal ? fSpacingX : fSpacingY) *
                       static_cast<float>(std::max(0, visible - 1));
    const float room = horizontal ? box.width() : box.height();
    const float share =
        std::max(0.0f, (room - taken - gaps) / static_cast<float>(growers));
    for (auto &child : fChildren) {
      if (!child->fVisible || !grows(*child)) {
        continue;
      }
      (horizontal ? child->fWidth : child->fHeight) = share;
    }
  }

private:
  Direction fDirection;
};

} // namespace skiff::nodes
