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

  void setSpacing(float x, float y) {
    fSpacingX = x;
    fSpacingY = y;
  }

protected:
  void layoutChildren() override {
    const skia::SkRect box = this->contentBox();
    if (fDirection == Direction::kVertical) {
      this->grow(box, false);
      float y = 0.0f;
      for (auto &child : fChildren) {
        if (!child->fVisible) {
          continue;
        }
        child->fAnchor = Anchor::kTopLeft;
        child->fOrigin = Anchor::kTopLeft;
        child->fY = y;
        child->layout(box);
        y += child->fBounds.height() + child->fMargin.totalY() + fSpacingY;
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
      float x = fCentreRows ? (box.width() - rowWidth) * 0.5f : 0.0f;
      float rowHeight = 0.0f;
      for (Drawable *child : row) {
        child->fAnchor = Anchor::kTopLeft;
        child->fOrigin = Anchor::kTopLeft;
        child->fX = x;
        child->fY = y;
        child->layout(box);
        x += child->fBounds.width() + child->fMargin.totalX() + fSpacingX;
        rowHeight = std::max(rowHeight,
                             child->fBounds.height() + child->fMargin.totalY());
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
