export module client.filtercontrol;

import std;
import skia;
import client.ui;

export namespace client {

// osu!lazer's FilterControl: a sheared panel hanging from the top-right of
// song select holding the search box, a two-handle difficulty range slider
// and the Sort / Group dropdowns. The panel is sheared while its children
// stay upright, exactly as lazer shears the container and un-shears the
// contents.
class FilterControl {
public:
  enum class SortMode : std::uint8_t {
    kArtist,
    kAuthor,
    kDifficulty,
    kLength,
    kTitle,
  };
  static constexpr std::array<const char *, 5> kSortNames = {
      "Artist", "Author", "Difficulty", "Length", "Title"};
  enum class GroupMode : std::uint8_t {
    kNone,
    kArtist,
    kAuthor,
    kDifficulty,
    kLength,
    kTitle,
  };
  static constexpr std::array<const char *, 6> kGroupNames = {
      "No grouping", "Artist", "Author", "Difficulty", "Length", "Title"};

  static constexpr float kShear = 0.15f;   // OsuGame.SHEAR
  static constexpr float kHeight = 141.0f; // HEIGHT_FROM_SCREEN_TOP
  static constexpr float kDiffRangeCap = 10.0f;

  [[nodiscard]] const std::string &text() const noexcept { return fFilterText; }

  // Where the control actually is. It is a wedge anchored to the top right --
  // it does not span the width of the screen, and a client that repaints
  // regions was repainting the title wedge on the far left along with it.
  [[nodiscard]] static skia::SkRect bounds(int screenW) {
    const float sw = static_cast<float>(screenW);
    const float panelW = std::min(760.0f, sw * 0.56f);
    return skia::SkRect::MakeLTRB(sw - panelW, 0.0f, sw, kHeight);
  }

  // The search box within it: the caret and the set count are the only things
  // up here that change without being touched, and both are inside this.
  [[nodiscard]] const skia::SkRect &searchBox() const noexcept {
    return fSearchBoxRect;
  }

  // A caret is only drawn where there is text to put it after.
  [[nodiscard]] bool caretShown(double nowMs) const {
    return !fFilterText.empty() && std::fmod(nowMs, 1000.0) < 600.0;
  }
  [[nodiscard]] SortMode sortMode() const noexcept { return fSortMode; }
  [[nodiscard]] GroupMode groupMode() const noexcept { return fGroupMode; }
  [[nodiscard]] float rangeMin() const noexcept { return fDiffRangeMin; }
  [[nodiscard]] float rangeMax() const noexcept { return fDiffRangeMax; }
  [[nodiscard]] bool dragging() const noexcept { return fDraggingRange >= 0; }
  [[nodiscard]] bool takeDirty() noexcept {
    const bool was = fDirty;
    fDirty = false;
    return was;
  }
  void markDirty() noexcept { fDirty = true; }

  void appendText(std::string_view utf8) {
    fFilterText += utf8;
    fDirty = true;
  }
  void setText(std::string text) {
    fFilterText = std::move(text);
    fDirty = true;
  }
  void clearText() {
    fFilterText.clear();
    fDirty = true;
  }
  void popText() {
    while (!fFilterText.empty()) {
      const auto c = static_cast<unsigned char>(fFilterText.back());
      fFilterText.pop_back();
      if ((c & 0xC0) != 0x80) {
        break;
      }
    }
    fDirty = true;
  }

  void endDrag() noexcept { fDraggingRange = -1; }

  void cycleSort() {
    fSortMode = static_cast<SortMode>((static_cast<int>(fSortMode) + 1) %
                                      static_cast<int>(kSortNames.size()));
    fDirty = true;
  }

  void draw(skia::SkCanvas *canvas, skia::SkFont &font, int screenW,
            float mouseX, float mouseY, std::size_t visibleCount,
            double nowMs) {
    fMouseX = mouseX;
    fMouseY = mouseY;
    const ui::Painter p(canvas, font);
    const float sw = static_cast<float>(screenW);
    const float panelW = std::min(760.0f, sw * 0.56f);
    const float left = sw - panelW;
    const float h = kHeight;
    const float shear = kShear * h;

    // Sheared background wedge, anchored top-right and mirrored, as lazer's
    // WedgeBackground with Scale(-1, 1).
    skia::SkPathBuilder wedge;
    wedge.moveTo(left + shear, 0.0f);
    wedge.lineTo(sw, 0.0f);
    wedge.lineTo(sw, h);
    wedge.lineTo(left, h);
    wedge.close();
    skia::SkPaint bg;
    bg.setAntiAlias(true);
    bg.setColor(skia::colorSetARGB(232, 24, 19, 32));
    p.canvas()->drawPath(wedge.detach(), bg);

    const float pad = 16.0f;
    const float contentL = left + shear + pad;
    const float contentR = sw - 40.0f; // lazer keeps a 40px right margin

    // ---- Search box.
    fSearchBoxRect =
        skia::SkRect::MakeLTRB(contentL, 12.0f, contentR, 44.0f);
    p.fillRounded(fSearchBoxRect, 6.0f,
                      skia::colorSetARGB(255, 40, 32, 52));
    p.strokeRounded(fSearchBoxRect, 6.0f,
                        fFilterText.empty() ? skia::colorSetARGB(255, 70, 58, 88)
                                            : ui::kAccent,
                        2.0f);
    const bool caret = std::fmod(nowMs, 1000.0) < 600.0;
    if (fFilterText.empty()) {
      p.textClipped("type to search", fSearchBoxRect.fLeft + 12.0f,
                            fSearchBoxRect.centerY() + 5.0f,
                            fSearchBoxRect.width() - 100.0f, 15.0f,
                            skia::kWhite, 0.42f);
    } else {
      p.textClipped(fFilterText + (caret ? "|" : " "),
                            fSearchBoxRect.fLeft + 12.0f,
                            fSearchBoxRect.centerY() + 5.0f,
                            fSearchBoxRect.width() - 100.0f, 15.0f,
                            skia::kWhite);
    }
    const std::string count =
        std::format("{} sets", visibleCount);
    p.textClipped(count, fSearchBoxRect.fRight - 84.0f,
                          fSearchBoxRect.centerY() + 5.0f, 80.0f, 13.0f,
                          skia::kWhite, 0.55f);

    // ---- Difficulty range slider (two handles, min range 0.1 as in lazer).
    const float rangeW = (contentR - contentL) * 0.62f;
    fRangeRect = skia::SkRect::MakeXYWH(contentL, 58.0f, rangeW, 26.0f);
    this->drawDifficultyRange(p, fRangeRect);

    // ---- Sort / Group dropdowns.
    const float ddY = 96.0f;
    const float ddW = std::min(180.0f, (contentR - contentL - 10.0f) * 0.5f);
    fSortRect = skia::SkRect::MakeXYWH(contentL, ddY, ddW, 30.0f);
    fGroupRect = skia::SkRect::MakeXYWH(contentL + ddW + 10.0f, ddY, ddW, 30.0f);
    this->drawDropdown(p, fSortRect, "Sort",
                       kSortNames[static_cast<std::size_t>(fSortMode)],
                       fSortOpen);
    this->drawDropdown(p, fGroupRect, "Group",
                       kGroupNames[static_cast<std::size_t>(fGroupMode)],
                       fGroupOpen);

    // Expanded lists draw last so they sit above the carousel.
    fSortItemRects.clear();
    fGroupItemRects.clear();
    if (fSortOpen) {
      this->drawDropdownItems(p, fSortRect,
                              std::span<const char *const>(kSortNames),
                              static_cast<int>(fSortMode), fSortItemRects);
    }
    if (fGroupOpen) {
      this->drawDropdownItems(p, fGroupRect,
                              std::span<const char *const>(kGroupNames),
                              static_cast<int>(fGroupMode), fGroupItemRects);
    }
  }

  void drawDifficultyRange(const ui::Painter &p, const skia::SkRect &r) {
    p.textClipped("Difficulty", r.fLeft, r.fTop + 2.0f, 90.0f,
                          12.0f, skia::kWhite, 0.6f);
    const skia::SkRect track =
        skia::SkRect::MakeXYWH(r.fLeft, r.fTop + 14.0f, r.width(), 6.0f);
    p.fillRounded(track, 3.0f, skia::colorSetARGB(255, 52, 44, 66));
    const float t0 = fDiffRangeMin / kDiffRangeCap;
    const float t1 = fDiffRangeMax / kDiffRangeCap;
    p.fillRounded(
        skia::SkRect::MakeLTRB(track.fLeft + track.width() * t0, track.fTop,
                               track.fLeft + track.width() * t1, track.fBottom),
        3.0f, ui::kAccent);
    p.circle(track.fLeft + track.width() * t0, track.centerY(), 7.0f,
             skia::kWhite);
    p.circle(track.fLeft + track.width() * t1, track.centerY(), 7.0f,
             skia::kWhite);
    const std::string label =
        fDiffRangeMax >= kDiffRangeCap
            ? std::format("{:.1f} - ∞", fDiffRangeMin)
            : std::format("{:.1f} - {:.1f}", fDiffRangeMin, fDiffRangeMax);
    p.textClipped(label, r.fRight - 96.0f, r.fTop + 2.0f, 96.0f,
                          12.0f, ui::kAccent2, 0.9f);
  }

  void drawDropdown(const ui::Painter &p, const skia::SkRect &r,
                    const char *label, const char *value, bool open) {
    const bool hover = r.contains(fMouseX, fMouseY);
    p.fillRounded(r, 6.0f,
                      hover || open ? skia::colorSetARGB(255, 56, 46, 72)
                                    : skia::colorSetARGB(255, 40, 32, 52));
    p.strokeRounded(r, 6.0f,
                        open ? ui::kAccent : skia::colorSetARGB(255, 70, 58, 88),
                        open ? 2.0f : 1.0f);
    p.textClipped(label, r.fLeft + 10.0f, r.centerY() + 4.0f,
                          52.0f, 12.0f, skia::kWhite, 0.5f);
    p.textClipped(value, r.fLeft + 62.0f, r.centerY() + 4.0f,
                          r.width() - 84.0f, 13.0f, skia::kWhite, 0.95f);
    // Chevron.
    skia::SkPaint tri;
    tri.setAntiAlias(true);
    tri.setColor(skia::kWhite);
    tri.setAlphaf(0.7f);
    skia::SkPathBuilder c;
    const float cx = r.fRight - 14.0f;
    const float cy = r.centerY();
    if (open) {
      c.moveTo(cx - 5.0f, cy + 2.5f);
      c.lineTo(cx + 5.0f, cy + 2.5f);
      c.lineTo(cx, cy - 3.5f);
    } else {
      c.moveTo(cx - 5.0f, cy - 2.5f);
      c.lineTo(cx + 5.0f, cy - 2.5f);
      c.lineTo(cx, cy + 3.5f);
    }
    c.close();
    p.canvas()->drawPath(c.detach(), tri);
  }

  void drawDropdownItems(const ui::Painter &p, const skia::SkRect &anchor,
                         std::span<const char *const> items, int current,
                         std::vector<skia::SkRect> &out) {
    const float itemH = 26.0f;
    const skia::SkRect box = skia::SkRect::MakeXYWH(
        anchor.fLeft, anchor.fBottom + 4.0f, anchor.width(),
        itemH * static_cast<float>(items.size()) + 8.0f);
    p.fillRounded(box, 6.0f, skia::colorSetARGB(248, 30, 24, 40));
    p.strokeRounded(box, 6.0f, ui::kAccent, 1.5f);
    for (std::size_t i = 0; i < items.size(); ++i) {
      const skia::SkRect row = skia::SkRect::MakeXYWH(
          box.fLeft + 4.0f, box.fTop + 4.0f + static_cast<float>(i) * itemH,
          box.width() - 8.0f, itemH);
      out.push_back(row);
      const bool hover = row.contains(fMouseX, fMouseY);
      const bool active = static_cast<int>(i) == current;
      if (hover || active) {
        p.fillRounded(row, 4.0f,
                          active ? ui::kAccent : skia::colorSetARGB(255, 52, 42, 66));
      }
      p.textClipped(items[i], row.fLeft + 10.0f,
                            row.centerY() + 4.0f, row.width() - 20.0f, 13.0f,
                            active ? skia::colorSetARGB(255, 20, 16, 26)
                                   : skia::kWhite,
                            0.95f);
    }
  }

  // Returns true when the control consumed the click.
  bool click(float x, float y, bool pressed) {
    if (!pressed) {
      fDraggingRange = -1;
      return false;
    }
    for (std::size_t i = 0; i < fSortItemRects.size(); ++i) {
      if (fSortItemRects[i].contains(x, y)) {
        fSortMode = static_cast<SortMode>(i);
        fSortOpen = false;
        fDirty = true;
        return true;
      }
    }
    for (std::size_t i = 0; i < fGroupItemRects.size(); ++i) {
      if (fGroupItemRects[i].contains(x, y)) {
        fGroupMode = static_cast<GroupMode>(i);
        fGroupOpen = false;
        fDirty = true;
        return true;
      }
    }
    if (fSortRect.contains(x, y)) {
      fSortOpen = !fSortOpen;
      fGroupOpen = false;
      return true;
    }
    if (fGroupRect.contains(x, y)) {
      fGroupOpen = !fGroupOpen;
      fSortOpen = false;
      return true;
    }
    if (fRangeRect.contains(x, y)) {
      const skia::SkRect track = skia::SkRect::MakeXYWH(
          fRangeRect.fLeft, fRangeRect.fTop + 14.0f, fRangeRect.width(), 6.0f);
      const float t =
          std::clamp((x - track.fLeft) / track.width(), 0.0f, 1.0f) *
          kDiffRangeCap;
      fDraggingRange =
          std::abs(t - fDiffRangeMin) <= std::abs(t - fDiffRangeMax) ? 0 : 1;
      this->dragRange(x);
      return true;
    }
    if (fSortOpen || fGroupOpen) {
      fSortOpen = fGroupOpen = false;
      return true;
    }
    return fSearchBoxRect.contains(x, y);
  }

  void dragRange(float x) {
    if (fDraggingRange < 0) {
      return;
    }
    const float t =
        std::clamp((x - fRangeRect.fLeft) / fRangeRect.width(), 0.0f, 1.0f) *
        kDiffRangeCap;
    constexpr float kMinRange = 0.1f; // DifficultyRangeSlider.MinRange
    if (fDraggingRange == 0) {
      fDiffRangeMin = std::min(t, fDiffRangeMax - kMinRange);
    } else {
      fDiffRangeMax = std::max(t, fDiffRangeMin + kMinRange);
    }
    fDirty = true;
  }

private:
  std::string fFilterText;
  SortMode fSortMode = SortMode::kTitle;
  GroupMode fGroupMode = GroupMode::kNone;
  float fDiffRangeMin = 0.0f;
  float fDiffRangeMax = kDiffRangeCap;
  int fDraggingRange = -1;
  bool fSortOpen = false;
  bool fGroupOpen = false;
  bool fDirty = true;
  float fMouseX = 0.0f;
  float fMouseY = 0.0f;
  skia::SkRect fSearchBoxRect = skia::SkRect::MakeEmpty();
  skia::SkRect fSortRect = skia::SkRect::MakeEmpty();
  skia::SkRect fGroupRect = skia::SkRect::MakeEmpty();
  skia::SkRect fRangeRect = skia::SkRect::MakeEmpty();
  std::vector<skia::SkRect> fSortItemRects;
  std::vector<skia::SkRect> fGroupItemRects;
};

} // namespace client
