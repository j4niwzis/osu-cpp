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
      if (fWrap && !row.empty() && rowWidth + fSpacingX + width > box.width()) {
        flushRow();
      }
      rowWidth += row.empty() ? width : fSpacingX + width;
      row.push_back(child.get());
    }
    flushRow();
  }

private:
  Direction fDirection;
};

} // namespace skiff::nodes
