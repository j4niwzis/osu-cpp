export module skiff.nodes:grid;

import std;
import skia;
import skiff.paint;
import skiff.scene;

namespace skiff::nodes {
using skiff::scene::Align;
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::nodes

export namespace skiff::nodes {

// Rows and columns of given sizes, with the children dealt into the cells in
// the order they were added. osu!framework's GridContainer, and CSS grid with
// one track list per axis.
//
// A track is a fixed size, the size of what is in it, or a share of what the
// fixed and automatic ones leave. That last one is `1fr`, and it is what a
// screen would otherwise compute: "half of what the buttons did not use" is
// two fractional rows either side of an automatic one.
//
// A cell does not move its child. The child is laid out against the cell as
// though it were its parent, so it centres itself or hangs off a corner with
// its own anchor and origin, which is how the framework this follows does it.
class Grid : public skiff::scene::TypedDrawable<Grid> {
public:
  struct Track {
    enum class Kind : std::uint8_t { kFixed, kAuto, kFraction };

    Kind fKind = Kind::kFraction;
    float fValue = 1.0f;

    [[nodiscard]] static Track fixed(float size) {
      return {Kind::kFixed, size};
    }
    [[nodiscard]] static Track automatic() { return {Kind::kAuto, 0.0f}; }
    [[nodiscard]] static Track fraction(float share = 1.0f) {
      return {Kind::kFraction, share};
    }
  };

  // Empty means one track taking everything, which is what a single row or a
  // single column of children is.
  std::vector<Track> fRows;
  std::vector<Track> fColumns;
  float fRowGap = 0.0f;
  float fColumnGap = 0.0f;

  void setRows(std::vector<Track> rows) {
    fRows = std::move(rows);
    this->invalidateLayout();
  }
  void setColumns(std::vector<Track> columns) {
    fColumns = std::move(columns);
    this->invalidateLayout();
  }

  // Where a cell ended up, for anything that has to be placed against one
  // without being in it.
  [[nodiscard]] skia::SkRect cellBox(std::size_t row,
                                     std::size_t column) const {
    if (row >= fRowSizes.size() || column >= fColumnSizes.size()) {
      return skia::SkRect::MakeEmpty();
    }
    const skia::SkRect box = this->contentBox();
    float x = box.fLeft;
    for (std::size_t c = 0; c < column; ++c) {
      x += fColumnSizes[c] + fColumnGap;
    }
    float y = box.fTop;
    for (std::size_t r = 0; r < row; ++r) {
      y += fRowSizes[r] + fRowGap;
    }
    return skia::SkRect::MakeXYWH(x, y, fColumnSizes[column], fRowSizes[row]);
  }

protected:
  void layoutChildren() override {
    const skia::SkRect box = this->contentBox();
    const std::size_t columns = std::max<std::size_t>(1, fColumns.size());
    const std::size_t rows =
        fRows.empty() ? std::max<std::size_t>(
                            1, (this->visibleCount() + columns - 1) / columns)
                      : fRows.size();

    // What each child would be at its own size, which is what an automatic
    // track is sized by.
    for (auto &child : fChildren) {
      if (child->fVisible) {
        child->layout(box);
      }
    }

    fColumnSizes = this->resolve(fColumns, columns, box.width(), fColumnGap,
                                 /*horizontal=*/true, columns);
    fRowSizes = this->resolve(fRows, rows, box.height(), fRowGap,
                              /*horizontal=*/false, columns);

    std::size_t index = 0;
    for (auto &child : fChildren) {
      if (!child->fVisible) {
        continue;
      }
      const std::size_t row = index / columns;
      const std::size_t column = index % columns;
      ++index;
      if (row >= rows) {
        break; // more children than cells: the rest are not placed
      }
      child->layout(this->cellBox(row, column));
    }
  }

private:
  [[nodiscard]] std::size_t visibleCount() const {
    std::size_t count = 0;
    for (const auto &child : fChildren) {
      count += child->fVisible ? 1 : 0;
    }
    return count;
  }

  // The natural size of whatever is in a track, taken from the children that
  // fall into it.
  [[nodiscard]] float naturalSize(std::size_t track, bool horizontal,
                                  std::size_t columns) const {
    float size = 0.0f;
    std::size_t index = 0;
    for (const auto &child : fChildren) {
      if (!child->fVisible) {
        continue;
      }
      const std::size_t at = horizontal ? index % columns : index / columns;
      ++index;
      if (at != track) {
        continue;
      }
      size = std::max(
          size, horizontal ? child->fBounds.width() + child->fMargin.totalX()
                           : child->fBounds.height() + child->fMargin.totalY());
    }
    return size;
  }

  [[nodiscard]] std::vector<float> resolve(const std::vector<Track> &tracks,
                                           std::size_t count, float room,
                                           float gap, bool horizontal,
                                           std::size_t columns) const {
    std::vector<float> sizes(count, 0.0f);
    float taken = gap * static_cast<float>(count > 0 ? count - 1 : 0);
    float shares = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
      const Track track = i < tracks.size() ? tracks[i] : Track::fraction(1.0f);
      switch (track.fKind) {
      case Track::Kind::kFixed:
        sizes[i] = track.fValue;
        taken += sizes[i];
        break;
      case Track::Kind::kAuto:
        sizes[i] = this->naturalSize(i, horizontal, columns);
        taken += sizes[i];
        break;
      case Track::Kind::kFraction:
        shares += track.fValue;
        break;
      }
    }
    if (shares > 0.0f) {
      const float left = std::max(0.0f, room - taken);
      for (std::size_t i = 0; i < count; ++i) {
        const Track track =
            i < tracks.size() ? tracks[i] : Track::fraction(1.0f);
        if (track.fKind == Track::Kind::kFraction) {
          sizes[i] = left * track.fValue / shares;
        }
      }
    }
    return sizes;
  }

  std::vector<float> fRowSizes;
  std::vector<float> fColumnSizes;
};

} // namespace skiff::nodes
