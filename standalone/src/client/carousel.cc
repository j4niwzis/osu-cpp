export module client.carousel;

import std;
import skia;
import skiff.paint;
import skiff.scene;

// osu!lazer's song select carousel, as a scene tree.
//
// Sources: osu.Game/Screens/Select/Carousel.cs and its panels --
// CarouselItem.DEFAULT_HEIGHT = 45 for a difficulty, PanelBeatmapSet.HEIGHT =
// 45 * 1.6 for a set, Panel.CORNER_RADIUS = 10, active_x_offset = 25 (doubled
// for an unselected difficulty, quadrupled for an unselected set), transitions
// 400 ms OutQuint, and the horizontal curve offsetX = (3 - sqrt(9 - dist^2)) *
// halfHeight.
//
// What a panel looks like -- its artwork, its veil, the spread of difficulty
// dots -- stays with the client, which has the library and the painters for
// it. What this owns is where each panel is, which of them exist at all, and
// what has to be repainted. That seam is deliberate: the drawing is already
// 1:1 with lazer and did not need moving to gain a scene tree.
// The framework lives in skiff:: now; these keep the screens below
// writing scene:: and nodes:: as they did when it sat in client::.
namespace scene = skiff::scene;

export namespace client::carousel {

// One row of the list: a set panel, or one difficulty of the expanded set.
struct Row {
  int fSet = 0;
  int fDiff = -1; // -1 for the set panel
};

using PaintPanel =
    std::function<void(skia::SkCanvas *, const skia::SkRect &, const Row &,
                       bool selected, bool hovered, float corner)>;

class Carousel {
public:
  struct Ctx {
    float fWidth = 0.0f;
    float fHeight = 0.0f;
    float fTop = 0.0f;    // below the filter control
    float fBottom = 0.0f; // above the footer
    float fMouseX = 0.0f;
    float fMouseY = 0.0f;
    double fNowMs = 0.0;
    double fDtMs = 16.0;
    int fSelectedSet = -1;
    int fSelectedDiff = 0;
  };

  struct Hit {
    bool fHit = false;
    int fSet = 0;
    int fDiff = -1;
  };

  void setPainter(PaintPanel painter) { fPaint = std::move(painter); }

  // The carousel owns its presentational rows. The library supplies only the
  // visible set indices and a way to ask how many difficulties the expanded
  // set has; an unchanged library revision and expansion make this a no-op.
  template <class DifficultyCount>
  void setRows(std::uint64_t sourceRevision, std::span<const int> visible,
               int expandedSet, DifficultyCount &&difficultyCount) {
    if (sourceRevision == fSourceRevision && expandedSet == fExpandedSet) {
      return;
    }
    fSourceRevision = sourceRevision;
    fExpandedSet = expandedSet;
    fRows.clear();
    fRows.reserve(visible.size());
    for (const int set : visible) {
      fRows.push_back({set, -1});
      if (set != expandedSet) {
        continue;
      }
      const auto count = std::invoke(difficultyCount, set);
      fRows.reserve(visible.size() + count);
      for (std::size_t diff = 0; diff < count; ++diff) {
        fRows.push_back({set, static_cast<int>(diff)});
      }
    }
    ++fRowsRevision;
  }

  // Everything that decides what the carousel looks like, with nothing drawn:
  // the client runs this before it knows whether the frame is worth drawing.
  void update(const Ctx &ctx) {
    fCtx = ctx;
    this->measure(ctx);
    this->follow(ctx);
    this->ease(ctx);
    if (!fScene) {
      fScene = std::make_unique<RootNode>(this);
    }
    fScene->place(skia::SkRect::MakeLTRB(0.0f, ctx.fTop, ctx.fWidth,
                                         ctx.fBottom));
    this->placePanels();
    fScene->setHover(ctx.fMouseX, ctx.fMouseY);
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] Hit click(float x, float y) {
    fPending = {};
    if (fScene) {
      fScene->click(x, y);
    }
    return fPending;
  }

  // A wheel tick moves the view without moving the selection, as lazer's
  // carousel does.
  void scroll(float ticks) {
    fTarget = std::clamp(fTarget - ticks * 90.0f, this->minScroll(),
                         this->maxScroll());
  }

  [[nodiscard]] skiff::scene::FrameResult finishFrame() {
    auto result = fScene ? fScene->finishFrame() : skiff::scene::FrameResult{};
    result.fWantsAnotherFrame =
        result.fWantsAnotherFrame || fScrollAnim != fTarget || fPop < 1.0f;
    return result;
  }

  [[nodiscard]] float scrollOffset() const noexcept { return fScrollAnim; }

private:
  // lazer's Carousel.offsetX: panels bow away from the centre of the screen.
  [[nodiscard]] static float offsetX(float dist, float halfHeight) {
    constexpr float kCircleRadius = 3.0f;
    const float discriminant =
        std::max(0.0f, kCircleRadius * kCircleRadius - dist * dist);
    return (kCircleRadius - std::sqrt(discriminant)) * halfHeight;
  }

  // One panel. It holds nothing but which row it is showing: the pool below
  // hands it a row and a rectangle, and it draws through the client's painter.
  class PanelNode : public scene::Drawable {
  public:
    explicit PanelNode(Carousel *owner) : fOwner(owner) {}

    void place(const skia::SkRect &rect) {
      if (rect != fBounds) {
        this->markDamaged(); // where it was
        fBounds = rect;
        this->markDamaged(); // and where it is now
      }
    }

    void show(const Row &row, bool selected, float corner) {
      if (!fVisible || row.fSet != fRow.fSet || row.fDiff != fRow.fDiff ||
          selected != fSelected) {
        this->markDamaged();
      }
      fRow = row;
      fSelected = selected;
      fCorner = corner;
      fVisible = true;
    }

    void hide() {
      if (fVisible) {
        this->markDamaged(); // whatever it was covering has to come back
        fVisible = false;
      }
    }

    [[nodiscard]] const Row &row() const noexcept { return fRow; }

  protected:
    void drawSelf(skia::SkCanvas *canvas, float) override {
      if (fOwner->fPaint) {
        fOwner->fPaint(canvas, fBounds, fRow, fSelected, fHovered, fCorner);
      }
    }

    bool acceptsInput() const override { return true; }
    bool hoverChangesAppearance() const override { return true; }

    bool onClick(float, float) override {
      fOwner->fPending = {true, fRow.fSet, fRow.fDiff};
      return true;
    }

  private:
    Carousel *fOwner;
    Row fRow;
    bool fSelected = false;
    float fCorner = 10.0f;
  };

  // The viewport. Masking, so a panel that has scrolled out of it damages
  // nothing: the clip on the way up to the root ends here.
  class RootNode : public scene::Drawable {
  public:
    explicit RootNode(Carousel *owner) : fOwner(owner) { fMasking = true; }

    void place(const skia::SkRect &rect) {
      if (rect != fBounds) {
        this->markDamaged();
        fBounds = rect;
        this->markDamaged();
      }
    }

    PanelNode *panel(std::size_t index) {
      while (fChildren.size() <= index) {
        this->add(std::make_unique<PanelNode>(fOwner));
      }
      return static_cast<PanelNode *>(fChildren[index].get());
    }

    [[nodiscard]] std::size_t panels() const noexcept {
      return fChildren.size();
    }

  protected:
    // The panels are placed by the carousel's own geometry rather than by the
    // layout system: their x comes from a curve through their y.
    void layoutChildren() override {}

    bool acceptsInput() const override { return false; }

  private:
    Carousel *fOwner;
  };

  // Row heights and the offsets they add up to. The offsets are retained: a
  // stable library does not repeat thousands of additions every update.
  void measure(const Ctx &ctx) {
    const float scale = std::clamp(ctx.fHeight / 900.0f, 0.8f, 1.6f);
    fSetHeight = 45.0f * 1.6f * scale;
    fDiffHeight = 45.0f * scale;
    fGap = 5.0f * scale;
    fCorner = 10.0f * scale;
    fActiveX = 25.0f * scale;
    fPanelWidth = std::min(680.0f * scale, ctx.fWidth * 0.52f);
    fLeft = ctx.fWidth - fPanelWidth - 20.0f * scale;

    if (scale == fMeasuredScale && fRowsRevision == fMeasuredRowsRevision) {
      return;
    }
    fMeasuredScale = scale;
    fMeasuredRowsRevision = fRowsRevision;
    fOffsets.clear();
    fOffsets.reserve(fRows.size());
    float y = 0.0f;
    for (const Row &row : fRows) {
      fOffsets.push_back(y);
      y += (row.fDiff < 0 ? fSetHeight : fDiffHeight) + fGap;
    }
    fTotal = y;
  }

  [[nodiscard]] float rowHeight(const Row &row) const {
    return row.fDiff < 0 ? fSetHeight : fDiffHeight;
  }

  // Measured against half the screen rather than half the visible band, which
  // is what the immediate version did: the list is allowed to overshoot into
  // the strips at either end, and matching it keeps the panels where they were.
  [[nodiscard]] float minScroll() const { return -fCtx.fHeight * 0.25f; }

  [[nodiscard]] float maxScroll() const {
    return std::max(this->minScroll(), fTotal - fCtx.fHeight * 0.25f);
  }

  // A selection the client made elsewhere -- a click, a key, a track ending --
  // brings the list to it.
  void follow(const Ctx &ctx) {
    if (ctx.fSelectedSet == fFollowedSet &&
        ctx.fSelectedDiff == fFollowedDiff &&
        fRowsRevision == fFollowedRowsRevision) {
      return;
    }
    fFollowedSet = ctx.fSelectedSet;
    fFollowedDiff = ctx.fSelectedDiff;
    fFollowedRowsRevision = fRowsRevision;
    fPop = 0.0f;
    // The selected difficulty is what the list centres on; a set with no
    // difficulties to show -- one still loading -- centres on its own panel.
    float centre = -1.0f;
    for (std::size_t i = 0; i < fRows.size(); ++i) {
      const Row &row = fRows[i];
      if (row.fSet != ctx.fSelectedSet) {
        continue;
      }
      if (row.fDiff < 0) {
        if (centre < 0.0f) {
          centre = fOffsets[i] + this->rowHeight(row) * 0.5f;
        }
        continue;
      }
      if (row.fDiff == ctx.fSelectedDiff) {
        centre = fOffsets[i] + this->rowHeight(row) * 0.5f;
        break;
      }
    }
    if (centre >= 0.0f) {
      fTarget = std::clamp(centre - fCtx.fHeight * 0.5f, this->minScroll(),
                           this->maxScroll());
    }
  }

  void ease(const Ctx &ctx) {
    fTarget = std::clamp(fTarget, this->minScroll(), this->maxScroll());
    fScrollAnim = skiff::paint::approach(fScrollAnim, fTarget, 120.0f, ctx.fDtMs);
    fPop = std::min(1.0f, fPop + static_cast<float>(ctx.fDtMs) / 400.0f);
  }

  // Only the rows within the viewport get a panel, and the panels are reused:
  // scrolling a list of two thousand allocates nothing.
  void placePanels() {
    const float viewTop = fCtx.fTop;
    const float viewBottom = fCtx.fBottom;
    const float halfHeight = fCtx.fHeight * 0.5f;
    const float pop = skiff::paint::outQuint(fPop);
    std::size_t used = 0;
    const float lookBehind = 2.0f * std::max(fSetHeight, fDiffHeight);
    const float contentBottom = fScrollAnim + viewBottom - viewTop;
    const std::size_t first = static_cast<std::size_t>(std::distance(
        fOffsets.begin(),
        std::ranges::lower_bound(fOffsets, fScrollAnim - lookBehind)));
    const std::size_t last = static_cast<std::size_t>(std::distance(
        fOffsets.begin(),
        std::ranges::upper_bound(fOffsets, contentBottom)));
    for (std::size_t i = first; i < last; ++i) {
      const Row &row = fRows[i];
      const float height = this->rowHeight(row);
      const float y = viewTop + fOffsets[i] - fScrollAnim;
      if (y + height < viewTop - height || y > viewBottom) {
        continue;
      }
      const bool selected =
          row.fDiff < 0 ? row.fSet == fCtx.fSelectedSet
                        : row.fSet == fCtx.fSelectedSet &&
                              row.fDiff == fCtx.fSelectedDiff;
      const float dist = std::abs(1.0f - (y + height * 0.5f) / halfHeight);
      float x = fLeft + offsetX(dist, halfHeight) + fCorner;
      if (!selected) {
        x += fActiveX * (row.fDiff < 0 ? 4.0f : 2.0f);
      }
      x += fActiveX * (1.0f - (selected ? pop : 0.0f));

      PanelNode *panel = fScene->panel(used++);
      panel->show(row, selected, fCorner);
      panel->place(skia::SkRect::MakeXYWH(x, y, fPanelWidth, height));
    }
    for (std::size_t i = used; i < fScene->panels(); ++i) {
      fScene->panel(i)->hide();
    }
  }

  PaintPanel fPaint;
  Ctx fCtx;
  std::unique_ptr<RootNode> fScene;
  std::vector<Row> fRows;
  std::vector<float> fOffsets; // where each row starts, before scrolling
  std::uint64_t fSourceRevision = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t fRowsRevision = 0;
  std::uint64_t fMeasuredRowsRevision =
      std::numeric_limits<std::uint64_t>::max();
  int fExpandedSet = -2;
  float fMeasuredScale = -1.0f;
  float fTotal = 0.0f;
  float fSetHeight = 72.0f;
  float fDiffHeight = 45.0f;
  float fGap = 5.0f;
  float fCorner = 10.0f;
  float fActiveX = 25.0f;
  float fPanelWidth = 680.0f;
  float fLeft = 0.0f;
  float fTarget = 0.0f;
  float fScrollAnim = 0.0f;
  float fPop = 1.0f;
  int fFollowedSet = -1;
  int fFollowedDiff = -1;
  std::uint64_t fFollowedRowsRevision =
      std::numeric_limits<std::uint64_t>::max();
  Hit fPending;
};

} // namespace client::carousel
