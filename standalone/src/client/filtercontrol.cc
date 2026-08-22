export module client.filtercontrol;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets.textbox;
import skiff.widgets.dropdown;
import skiff.widgets.sliderbar;
import client.palette;

export namespace client {

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;
namespace widgets = skiff::widgets;

inline constexpr float kFilterShear = 0.15f;
inline constexpr float kFilterHeight = 141.0f;

namespace filter_style {
struct Search;
struct Count;
struct Range;
struct RangeLabel;
struct RangeValue;
struct RangeSlider;
struct Dropdowns;
struct Dropdown;
struct List;
} // namespace filter_style

namespace filter_detail {

// The only screen-specific drawing left in the control. Its children are
// ordinary upright drawables; only the dark plate has lazer's shear.
class Wedge : public scene::TypedDrawable<Wedge> {
protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkPathBuilder path;
    path.moveTo(fBounds.fLeft + kFilterShear * fBounds.height(), fBounds.fTop);
    path.lineTo(fBounds.fRight, fBounds.fTop);
    path.lineTo(fBounds.fRight, fBounds.fBottom);
    path.lineTo(fBounds.fLeft, fBounds.fBottom);
    path.close();
    skia::SkPaint background;
    background.setAntiAlias(true);
    background.setColor(skia::colorSetARGB(232, 24, 19, 32));
    background.setAlphaf(alpha);
    canvas->drawPath(path.detach(), background);
  }
};

// When a list is open, a click outside it dismisses it before reaching the
// carousel. Closed, this root is transparent to input.
class Root : public scene::TypedDrawable<Root> {
public:
  std::function<bool()> fDismiss;

protected:
  bool acceptsInput() const override { return static_cast<bool>(fDismiss); }
  bool hoverChangesAppearance() const override { return false; }
  bool onClick(float, float) override { return fDismiss && fDismiss(); }
};

} // namespace filter_detail

struct FilterTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::select<filter_detail::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth})
          .rule(scene::select<filter_detail::Wedge>(),
                {.anchor = scene::Anchor::kTopRight,
                 .origin = scene::Anchor::kTopRight,
                 .width = 0.56f,
                 .height = kFilterHeight,
                 .relativeSize = scene::Axes::kX,
                 .maxWidth = 760.0f,
                 .padding = scene::Margin{
                     12.0f, 40.0f, 0.0f,
                     kFilterShear * kFilterHeight + 16.0f}})
          .rule(scene::selectAny<filter_style::Search>(),
                {.width = 1.0f,
                 .height = 32.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::selectAny<filter_style::Count>(),
                {.anchor = scene::Anchor::kCentreRight,
                 .origin = scene::Anchor::kCentreRight,
                 .x = -8.0f,
                 .alpha = 0.55f,
                 .fontSize = 13.0f})
          .rule(scene::selectAny<filter_style::Range>(),
                {.x = 0.0f,
                 .y = 46.0f,
                 .width = 0.62f,
                 .height = 26.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::selectAny<filter_style::RangeLabel>(),
                {.alpha = 0.6f, .fontSize = 12.0f})
          .rule(scene::selectAny<filter_style::RangeValue>(),
                {.anchor = scene::Anchor::kTopRight,
                 .origin = scene::Anchor::kTopRight,
                 .alpha = 0.9f,
                 .colour = palette::kAccent2,
                 .fontSize = 12.0f})
          .rule(scene::selectAny<filter_style::RangeSlider>(),
                {.y = 10.0f,
                 .width = 1.0f,
                 .height = 14.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::selectAny<filter_style::Dropdowns>(),
                {.x = 0.0f,
                 .y = 84.0f,
                 .width = 1.0f,
                 .height = 30.0f,
                 .relativeSize = scene::Axes::kX,
                 .maxWidth = 370.0f})
          .rule(scene::selectAny<filter_style::Dropdown>(),
                {.height = 30.0f, .grow = scene::Axes::kX})
          .rule(scene::selectAny<filter_style::List>(),
                {.anchor = scene::Anchor::kBottomLeft,
                 .origin = scene::Anchor::kTopLeft,
                 .y = 4.0f,
                 .width = 1.0f,
                 .relativeSize = scene::Axes::kX});
};

// osu!lazer's song-select filter. The state exposed to the library remains
// small, while presentation is a retained Skiff tree: layouts place the
// controls, widgets own their hit testing, and damage follows the nodes that
// actually changed.
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

  static constexpr float kShear = kFilterShear;
  static constexpr float kHeight = kFilterHeight;
  static constexpr float kDiffRangeCap = 10.0f;

  struct Ctx {
    skia::SkFont *fFont = nullptr;
    float fWidth = 0.0f;
    float fHeight = 0.0f;
    float fMouseX = 0.0f;
    float fMouseY = 0.0f;
    std::size_t fVisibleCount = 0;
    double fNowMs = 0.0;
  };

  [[nodiscard]] const std::string &text() const noexcept { return fFilterText; }
  [[nodiscard]] SortMode sortMode() const noexcept { return fSortMode; }
  [[nodiscard]] GroupMode groupMode() const noexcept { return fGroupMode; }
  [[nodiscard]] float rangeMin() const noexcept { return fDiffRangeMin; }
  [[nodiscard]] float rangeMax() const noexcept { return fDiffRangeMax; }
  [[nodiscard]] bool dragging() const noexcept {
    return fRangeSlider != nullptr && fRangeSlider->dragging();
  }

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

  void cycleSort() {
    fSortMode = static_cast<SortMode>((static_cast<int>(fSortMode) + 1) %
                                      static_cast<int>(kSortNames.size()));
    fDirty = true;
  }

  void update(const Ctx &ctx) {
    if (ctx.fFont == nullptr) {
      return;
    }
    nodes::Text::setFont(ctx.fFont);
    bool rebuilt = false;
    if (!fScene) {
      fScene = this->build();
      rebuilt = true;
    }

    fSearchBox->setText(fFilterText);
    fSearchBox->tickCaret(ctx.fNowMs, !fFilterText.empty());
    fCount->setText(std::format("{} sets", ctx.fVisibleCount));
    fRangeValue->setText(
        fDiffRangeMax >= kDiffRangeCap
            ? std::format("{:.1f} - ∞", fDiffRangeMin)
            : std::format("{:.1f} - {:.1f}", fDiffRangeMin, fDiffRangeMax));
    fRangeSlider->setRange(fDiffRangeMin / kDiffRangeCap,
                           fDiffRangeMax / kDiffRangeCap);
    fSortButton->setValue(kSortNames[static_cast<std::size_t>(fSortMode)]);
    fGroupButton->setValue(kGroupNames[static_cast<std::size_t>(fGroupMode)]);
    fSortList->setCurrent(static_cast<int>(fSortMode));
    fGroupList->setCurrent(static_cast<int>(fGroupMode));
    this->syncOpenState();

    const skia::SkRect screen =
        skia::SkRect::MakeWH(ctx.fWidth, ctx.fHeight);
    fScene->updateTree(ctx.fNowMs);
    fScene->layoutIfNeeded(screen);
    if (rebuilt) {
      fScene->markDamaged();
    }
    fScene->setHover(ctx.fMouseX, ctx.fMouseY);
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] skia::SkRect takeDamage() {
    return fScene ? fScene->takeDamage() : skia::SkRect::MakeEmpty();
  }

  // Returns true when the control consumed the click.
  bool click(float x, float y, bool pressed) {
    if (!pressed) {
      this->endDrag();
      return false;
    }
    return fScene && fScene->click(x, y);
  }

  void dragRange(float x) {
    if (fRangeSlider != nullptr) {
      fRangeSlider->dragTo(x);
    }
  }

  void endDrag() noexcept {
    if (fRangeSlider != nullptr) {
      fRangeSlider->endDrag();
    }
  }

private:
  inline static const widgets::Theme kControlTheme = {
      .fSurface = skia::colorSetARGB(255, 40, 32, 52),
      .fSurfaceHover = skia::colorSetARGB(255, 56, 46, 72),
      .fSurfaceActive = skia::colorSetARGB(255, 70, 58, 88),
      .fText = skia::kWhite,
      .fLabel = skia::kWhite,
      .fTextDim = skia::colorSetARGB(255, 190, 180, 202),
      .fTextFaint = skia::colorSetARGB(255, 170, 160, 182),
      .fAccent = palette::kAccent,
      .fOnAccent = skia::colorSetARGB(255, 20, 16, 26),
      .fCorner = 6.0f,
      .fFontSize = 13.0f,
      .fRowHeight = 32.0f,
      .fPaddingX = 10.0f,
  };

  inline static const widgets::Theme kListTheme = {
      .fSurface = skia::colorSetARGB(248, 30, 24, 40),
      .fSurfaceHover = skia::colorSetARGB(255, 52, 42, 66),
      .fSurfaceActive = skia::colorSetARGB(255, 56, 46, 72),
      .fText = skia::kWhite,
      .fLabel = skia::kWhite,
      .fTextDim = skia::colorSetARGB(255, 190, 180, 202),
      .fTextFaint = skia::colorSetARGB(255, 170, 160, 182),
      .fAccent = palette::kAccent,
      .fOnAccent = skia::colorSetARGB(255, 20, 16, 26),
      .fCorner = 6.0f,
      .fFontSize = 13.0f,
      .fRowHeight = 26.0f,
      .fPaddingX = 10.0f,
  };

  [[nodiscard]] std::unique_ptr<scene::Drawable> build() {
    auto root = scene::make<filter_detail::Root>({});
    root->fDismiss = [this] {
      if (!fSortOpen && !fGroupOpen) {
        return false;
      }
      fSortOpen = false;
      fGroupOpen = false;
      this->syncOpenState();
      return true;
    };

    auto *panel = root->add<filter_detail::Wedge>({});

    auto *search = panel->add<nodes::Clickable>(
        {.roles = {scene::role<filter_style::Search>}}, [] {});
    fSearchBox = search->add<widgets::TextBox>(
        {.fill = true}, "type to search");
    fSearchBox->fTheme = kControlTheme;
    fSearchBox->fTrailingInset = 92.0f;
    fCount = search->add<nodes::Text>(
        {.roles = {scene::role<filter_style::Count>}}, "0 sets", 13.0f,
        skia::kWhite);

    auto *range = panel->add<scene::Drawable>(
        {.roles = {scene::role<filter_style::Range>}});
    range->add<nodes::Text>(
        {.roles = {scene::role<filter_style::RangeLabel>}}, "Difficulty",
        12.0f, skia::kWhite);
    fRangeValue = range->add<nodes::Text>(
        {.roles = {scene::role<filter_style::RangeValue>}}, "0.0 - ∞", 12.0f,
        palette::kAccent2);
    fRangeSlider = range->add<widgets::RangeSlider>(
        {.roles = {scene::role<filter_style::RangeSlider>}});
    fRangeSlider->fTheme = kControlTheme;
    fRangeSlider->fMinSpan = 0.1f / kDiffRangeCap;
    fRangeSlider->fOnSet = [this](float low, float high) {
      fDiffRangeMin = low * kDiffRangeCap;
      fDiffRangeMax = high * kDiffRangeCap;
      fDirty = true;
    };

    auto *dropdowns = panel->add<nodes::FillFlow>(
        {.roles = {scene::role<filter_style::Dropdowns>}},
        nodes::FillFlow::Direction::kHorizontal, 10.0f, 0.0f);
    dropdowns->fWrap = false;
    fSortButton = dropdowns->add<widgets::DropdownButton>(
        {.roles = {scene::role<filter_style::Dropdown>}}, "Sort", "Title");
    fGroupButton = dropdowns->add<widgets::DropdownButton>(
        {.roles = {scene::role<filter_style::Dropdown>}}, "Group",
        "No grouping");
    fSortButton->fTheme = kControlTheme;
    fGroupButton->fTheme = kControlTheme;
    fSortButton->fOnOpen = [this] {
      fSortOpen = !fSortOpen;
      fGroupOpen = false;
      this->syncOpenState();
    };
    fGroupButton->fOnOpen = [this] {
      fGroupOpen = !fGroupOpen;
      fSortOpen = false;
      this->syncOpenState();
    };

    fSortList = root->add<widgets::DropdownList>(
        {.roles = {scene::role<filter_style::List>}});
    fGroupList = root->add<widgets::DropdownList>(
        {.roles = {scene::role<filter_style::List>}});
    fSortList->fFollow = fSortButton;
    fGroupList->fFollow = fGroupButton;
    fSortList->fTheme = kListTheme;
    fGroupList->fTheme = kListTheme;
    fSortList->fRowHeight = 24.0f;
    fGroupList->fRowHeight = 24.0f;
    fSortList->setOptions(this->options(kSortNames));
    fGroupList->setOptions(this->options(kGroupNames));
    fSortList->fOnChoose = [this](int index) {
      fSortMode = static_cast<SortMode>(index);
      fSortOpen = false;
      fDirty = true;
      this->syncOpenState();
    };
    fGroupList->fOnChoose = [this](int index) {
      fGroupMode = static_cast<GroupMode>(index);
      fGroupOpen = false;
      fDirty = true;
      this->syncOpenState();
    };
    this->syncOpenState();
    root->setStyleSheet<FilterTheme>();
    return root;
  }

  template <std::size_t N>
  [[nodiscard]] static std::vector<std::string>
  options(const std::array<const char *, N> &names) {
    std::vector<std::string> out;
    out.reserve(N);
    for (const char *name : names) {
      out.emplace_back(name);
    }
    return out;
  }

  void syncOpenState() {
    if (fSortButton != nullptr) {
      fSortButton->setOpen(fSortOpen);
    }
    if (fGroupButton != nullptr) {
      fGroupButton->setOpen(fGroupOpen);
    }
    if (fSortList != nullptr) {
      fSortList->setExpanded(fSortOpen);
    }
    if (fGroupList != nullptr) {
      fGroupList->setExpanded(fGroupOpen);
    }
  }

  std::string fFilterText;
  SortMode fSortMode = SortMode::kTitle;
  GroupMode fGroupMode = GroupMode::kNone;
  float fDiffRangeMin = 0.0f;
  float fDiffRangeMax = kDiffRangeCap;
  bool fSortOpen = false;
  bool fGroupOpen = false;
  bool fDirty = true;

  std::unique_ptr<scene::Drawable> fScene;
  widgets::TextBox *fSearchBox = nullptr;
  nodes::Text *fCount = nullptr;
  nodes::Text *fRangeValue = nullptr;
  widgets::RangeSlider *fRangeSlider = nullptr;
  widgets::DropdownButton *fSortButton = nullptr;
  widgets::DropdownButton *fGroupButton = nullptr;
  widgets::DropdownList *fSortList = nullptr;
  widgets::DropdownList *fGroupList = nullptr;
};

} // namespace client
