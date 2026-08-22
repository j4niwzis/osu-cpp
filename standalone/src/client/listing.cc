export module client.listing;

import std;
import skia;
import skiff.paint;
import client.palette;
import skiff.scene;
import skiff.nodes;
import skiff.widgets.textbox;
import skiff.widgets.tabbar;

// osu!lazer's BeatmapListingOverlay, port of the layout and the palette.
//
// Sources: osu.Game/Overlays/BeatmapListingOverlay.cs, its
// BeatmapListing/{BeatmapListingFilterControl,BeatmapListingSearchControl,
// BeatmapSearchFilterRow,FilterTabItem,BeatmapListingSortTabControl}.cs and
// osu.Game/Beatmaps/Drawables/Cards/BeatmapCard{,Normal}.cs. Numbers below are
// theirs; where this cannot follow, the comment says so.
export namespace client::listing {

// ---- OverlayColourProvider(OverlayColourScheme.Blue), hue 200 -------------
// Colour4.FromHSL(200/360, saturation, lightness), evaluated once here.
inline constexpr skia::SkColor kColour1 = skia::colorSetARGB(255, 102, 204, 255);
inline constexpr skia::SkColor kContent1 = skia::colorSetARGB(255, 255, 255, 255);
inline constexpr skia::SkColor kContent2 = skia::colorSetARGB(255, 219, 233, 240);
inline constexpr skia::SkColor kLight1 = skia::colorSetARGB(255, 184, 211, 224);
inline constexpr skia::SkColor kLight2 = skia::colorSetARGB(255, 166, 200, 217);
inline constexpr skia::SkColor kLight3 = skia::colorSetARGB(255, 148, 189, 209);
inline constexpr skia::SkColor kColour3 = skia::colorSetARGB(255, 51, 153, 204);
inline constexpr skia::SkColor kDark3 = skia::colorSetARGB(255, 51, 68, 76);
inline constexpr skia::SkColor kDark6 = skia::colorSetARGB(255, 20, 27, 31);
inline constexpr skia::SkColor kForeground1 = skia::colorSetARGB(255, 143, 156, 163);
inline constexpr skia::SkColor kBackground2 = skia::colorSetARGB(255, 69, 79, 84);
inline constexpr skia::SkColor kBackground3 = skia::colorSetARGB(255, 57, 66, 70);
inline constexpr skia::SkColor kBackground4 = skia::colorSetARGB(255, 46, 53, 56);
inline constexpr skia::SkColor kBackground5 = skia::colorSetARGB(255, 34, 40, 42);
inline constexpr skia::SkColor kBackground6 = skia::colorSetARGB(255, 23, 26, 28);

// ---- Filter values, in the order lazer lists them -------------------------
enum class Category : int {
  kAny, kLeaderboard, kRanked, kQualified, kLoved, kFavourites, kPending, kWip,
  kGraveyard, kMine
};
enum class Genre : int {
  kAny, kUnspecified, kVideoGame, kAnime, kRock, kPop, kOther, kNovelty,
  kHipHop, kElectronic, kMetal, kClassical, kFolk, kJazz
};
// In display order, which SearchLanguage sets with [Order] attributes rather
// than by enum value.
enum class Language : int {
  kAny, kEnglish, kChinese, kFrench, kGerman, kItalian, kJapanese, kKorean,
  kSpanish, kSwedish, kRussian, kPolish, kInstrumental, kOther, kUnspecified
};
enum class Played : int { kAny, kPlayed, kUnplayed };
enum class Explicit : int { kHide, kShow };
enum class Sort : int {
  kTitle, kArtist, kDifficulty, kUpdated, kRanked, kRating, kPlays,
  kFavourites, kRelevance, kNominations
};
enum class CardSize : int { kNormal, kExtra };

inline constexpr const char *kCategoryLabels[] = {
    "Any",        "Has Leaderboard", "Ranked",    "Qualified", "Loved",
    "Favourites", "Pending",         "WIP", "Graveyard", "My Maps"};
inline constexpr const char *kGenreLabels[] = {
    "Any",     "Unspecified", "Video Game", "Anime",      "Rock",
    "Pop",     "Other",       "Novelty",    "Hip Hop",    "Electronic",
    "Metal",   "Classical",   "Folk",       "Jazz"};
inline constexpr const char *kLanguageLabels[] = {
    "Any",     "English",  "Chinese",      "French",  "German",
    "Italian", "Japanese", "Korean",       "Spanish", "Swedish",
    "Russian", "Polish",   "Instrumental", "Other",   "Unspecified"};
inline constexpr const char *kGeneralLabels[] = {
    "Recommended difficulty", "Include converted beatmaps",
    "Subscribed mappers", "Spotlights", "Featured Artists"};
inline constexpr const char *kRulesetLabels[] = {"osu!", "osu!taiko",
                                                 "osu!catch", "osu!mania"};
inline constexpr const char *kExtraLabels[] = {"Has Video", "Has Storyboard"};
inline constexpr const char *kRankLabels[] = {"XH", "X", "SH", "S",
                                              "A",  "B", "C",  "D"};
inline constexpr const char *kPlayedLabels[] = {"Any", "Played", "Unplayed"};
inline constexpr const char *kExplicitLabels[] = {"Hide", "Show"};
inline constexpr const char *kSortLabels[] = {
    "Title",  "Artist",     "Difficulty", "Updated",   "Ranked",
    "Rating", "Play Count", "Favourites", "Relevance", "Nominations"};

// General/Extra/Ranks are multiple-selection rows; the rest are single.
struct Filters {
  std::string fQuery;
  std::array<bool, 5> fGeneral{false, false, false, false, true}; // Featured
  int fRuleset = 0;
  Category fCategory = Category::kLeaderboard; // lazer's default
  Genre fGenre = Genre::kAny;
  Language fLanguage = Language::kAny;
  std::array<bool, 2> fExtra{};
  std::array<bool, 8> fRanks{};
  Played fPlayed = Played::kAny;
  Explicit fExplicit = Explicit::kHide;
  Sort fSort = Sort::kRanked;
  bool fDescending = true;
  CardSize fCardSize = CardSize::kNormal;
};

// One beatmap set in the listing, with everything a card draws.
struct Entry {
  long fSetId = -1;
  std::string fTitle, fTitleUnicode, fArtist, fArtistUnicode, fCreator;
  std::string fStatus;
  std::string fUpdated;   // yyyy-mm-dd, for the date statistic
  std::string fSource;
  struct Difficulty {
    std::string fVersion;
    float fStars = 0.0f;
    int fMode = 0;
    double fLengthMs = 0.0;
    double fCs = 0.0, fAr = 0.0, fOd = 0.0, fHp = 0.0;
    int fMaxCombo = 0;
  };
  std::vector<Difficulty> fDiffs; // sorted by star rating, as lazer lists them
  float fStarsMin = 0.0f, fStarsMax = 0.0f;
  int fDiffCount = 0;
  double fBpm = 0.0;
  long fPlayCount = 0, fFavouriteCount = 0;
  int fGenre = 0, fLanguage = 0; // osu! ids
  std::string fCardCover, fFullCover; // covers.card@2x / covers.cover@2x
  std::string fTags;
  std::vector<int> fRatings; // the 1..10 histogram osu! returns
  skia::Sp<skia::SkImage> fPageCover; // the big cover, fetched on demand
  enum class Cover : std::uint8_t { kNone, kFetching, kReady, kFailed };
  Cover fPageCoverSt = Cover::kNone;
  bool fVideo = false, fStoryboard = false, fNsfw = false, fSpotlight = false;
  bool fFeatured = false;  // track_id != null
  double fRating = 0.0;
  std::int64_t fRankedDate = 0; // sortable stamp, 0 when unranked
  std::int64_t fUpdatedDate = 0;
  enum class St : std::uint8_t { kIdle, kFetching, kDone, kError };
  St fSt = St::kIdle;
  float fProgress = 0.0f;
  enum class Thumb : std::uint8_t { kNone, kFetching, kReady, kFailed };
  Thumb fThumbSt = Thumb::kNone;
  skia::Sp<skia::SkImage> fThumb;
};

// ---- BeatmapCard metrics --------------------------------------------------
inline constexpr float kCardWidth = 345.0f;       // BeatmapCard.WIDTH
inline constexpr float kCardNormalHeight = 80.0f; // BeatmapCardNormal.HEIGHT
inline constexpr float kCardExtraHeight = 112.0f; // BeatmapCardExtra height
inline constexpr float kCardCorner = 8.0f;        // BeatmapCard.CORNER_RADIUS
inline constexpr float kCardSpacing = 10.0f;
inline constexpr float kButtonsCollapsed = kCardCorner;
inline constexpr float kButtonsExpanded = 24.0f;
inline constexpr float kTransitionMs = 360.0f;    // BeatmapCard.TRANSITION_DURATION
inline constexpr float kHorizontalPadding = 50.0f; // WaveOverlayContainer
inline constexpr float kPanelPadding = 20.0f;      // panelTarget horizontal
inline constexpr float kRowLabelWidth = 100.0f;    // BeatmapSearchFilterRow grid
inline constexpr float kRowSpacing = 5.0f;
inline constexpr float kTabSpacing = 10.0f;
inline constexpr float kFilterFontSize = 13.0f;
// FillFlowContainer auto-sizes a row to its text; 13px Torus lands here.
inline constexpr float kFilterLineHeight = 16.0f;
inline constexpr float kSortBarHeight = 40.0f;
inline constexpr float kExpandedMaxHeight = 200.0f; // ExpandedContentScrollContainer
inline constexpr float kExpandDelayMs = 100.0f;     // BeatmapCardContent.ExpandAfterDelay

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;
namespace paint = skiff::paint;
namespace widgets = skiff::widgets;

// The listing's greys, handed to the widgets rather than baked into them.
inline const widgets::Theme kTabTheme = {
    .fSurface = kBackground4,
    .fText = kContent1,
    .fLabel = kContent2,
    .fTextDim = kLight2,
    .fTextFaint = kLight3,
    .fAccent = kColour1,
    .fOnAccent = kBackground6,
};

inline const widgets::Theme kTextBoxTheme = {
    .fSurface = kBackground4,
    .fSurfaceHover = kBackground3,
    .fSurfaceActive = kBackground2,
    .fText = kContent1,
    .fTextDim = kLight1,
    .fTextFaint = kLight3,
    .fAccent = kColour1,
    .fOnAccent = kBackground6,
    .fCorner = 5.0f,
    .fFontSize = 16.0f,
    .fRowHeight = 40.0f,
    .fPaddingX = 12.0f,
};

namespace style {
struct Root;
struct Scroll;
struct Column;
struct Header;
struct SearchPanel;
struct SearchColumn;
struct SearchBox;
struct FilterRows;
struct FilterRow;
struct SortBar;
struct Empty;
struct Cards;
struct Expansion;
} // namespace style

struct ListingTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::select<nodes::Box, style::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth,
                 .backgroundColour = kBackground6})
          .rule(scene::select<nodes::ScrollContainer, style::Scroll>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth})
          .rule(scene::select<nodes::FillFlow, style::Column>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY})
          .rule(scene::selectAny<style::Header>(),
                {.width = 1.0f,
                 .height = 55.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::select<nodes::Box, style::SearchPanel>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY,
                 .padding = scene::Margin{20.0f, kHorizontalPadding, 20.0f,
                                          kHorizontalPadding},
                 .backgroundColour = kDark6})
          .rule(scene::select<nodes::FillFlow, style::SearchColumn>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY})
          .rule(scene::select<widgets::TextBox, style::SearchBox>(),
                {.width = 1.0f,
                 .height = 40.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::select<nodes::FillFlow, style::FilterRows>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY,
                 .padding = scene::Margin::horizontal(10.0f)})
          .rule(scene::select<widgets::TabBar, style::FilterRow>(),
                {.width = 1.0f, .relativeSize = scene::Axes::kX})
          .rule(scene::select<widgets::TabBar, style::SortBar>(),
                {.width = 1.0f,
                 .height = kSortBarHeight,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::selectAny<style::Empty>(),
                {.width = 1.0f,
                 .height = 250.0f,
                 .relativeSize = scene::Axes::kX})
          .rule(scene::select<nodes::FillFlow, style::Cards>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY,
                 .padding = scene::Margin{15.0f, kPanelPadding,
                                          20.0f + kExpandedMaxHeight,
                                          kPanelPadding}})
          .rule(scene::selectAny<style::Expansion>(), {.masking = true});
};

// BeatmapSetOnlineStatus, in the colours the website gives it.
inline skia::SkColor statusColour(std::string_view status) {
  if (status == "ranked" || status == "approved" || status == "qualified") {
    return skia::colorSetARGB(255, 102, 204, 255);
  }
  if (status == "loved") {
    return skia::colorSetARGB(255, 255, 102, 170);
  }
  if (status == "graveyard") {
    return skia::colorSetARGB(255, 140, 140, 155);
  }
  return skia::colorSetARGB(255, 179, 217, 68);
}

// The listing screen, as a tree.
//
// What used to be here was a frame's worth of arithmetic: rows measured and
// placed by hand, a grid whose columns were computed per frame, and a list of
// rectangles rebuilt every draw so that clicks could be matched against it.
// The layout is the tree's job now, hit testing walks it, and each part knows
// what it drew -- which is also what lets a frame repaint only what changed.
class Listing {
public:
  struct Ctx {
    skia::SkCanvas *fCanvas = nullptr;
    skia::SkFont *fFont = nullptr;
    float fWidth = 0.0f, fHeight = 0.0f;
    float fMouseX = 0.0f, fMouseY = 0.0f;
    double fNowMs = 0.0;
    double fDtMs = 16.0; // unused by the tree, which runs on the clock
    std::span<Entry> fEntries;
    bool fLoading = false;
  };
  enum class Action { kNone, kSearch, kRefilter, kDownload, kOpen, kPreview };
  struct Result {
    Action fAction = Action::kNone;
    std::size_t fIndex = 0; // entry index for kDownload, kOpen, kPreview
  };

  // Which set the preview is playing, and how far through it is.
  // The play button's ring is the only thing a preview moves, so the card it
  // is on is what gets marked. "A preview is playing" used to be worth the
  // whole screen, every frame, for the length of the clip.
  void setPreview(long setId, float progress) {
    if (setId != fPreviewId || progress != fPreviewProgress) {
      this->setChanged(fPreviewId); // where it was playing
      this->setChanged(setId);      // where it plays now
    }
    fPreviewId = setId;
    fPreviewProgress = progress;
  }

  // Something the card draws out of its entry changed under it -- a cover
  // that finished loading, another percent of a download. The card reads
  // those straight out of the entry, so it cannot notice by itself, and now
  // that frames only happen when something says it changed, not noticing
  // means never appearing. One card, not the screen: there are fifty covers.
  void entryChanged(int entry) {
    for (const auto &card : fCards) {
      if (card.first == entry && card.second != nullptr) {
        card.second->markDamaged();
        return;
      }
    }
  }

  void setChanged(long setId) {
    if (setId < 0) {
      return;
    }
    for (std::size_t i = 0; i < fEntries.size(); ++i) {
      if (fEntries[i].fSetId == setId) {
        this->entryChanged(static_cast<int>(i));
        return;
      }
    }
  }

  [[nodiscard]] Filters &filters() { return fFilters; }
  [[nodiscard]] const Filters &filters() const { return fFilters; }
  [[nodiscard]] std::span<const int> visible() const { return fVisible; }

  // The subset of those that a reader can actually get to without waiting:
  // fetching a cover for the four hundredth card of a list nobody has
  // scrolled is bandwidth, a decode, and a texture, spent on nothing.
  [[nodiscard]] std::span<const int> onScreen() const { return fOnScreen; }

  // The query is edited through the client, which types into a string the
  // box does not own. Handing the result over is what marks the box: a
  // client that only draws what says it changed would otherwise never show
  // the letter that was typed.
  void queryEdited() {
    if (fSearchBox != nullptr) {
      fSearchBox->setText(fFilters.fQuery);
    }
  }

  void scroll(float ticks) { fScrollTicks += ticks; }
  void scrollToStart() { fScrollToStart = true; }

  // The listing pages in as it is scrolled, the way the overlay's scroll
  // container asks for the next cursor near the bottom. Also true when the
  // client-side filters left too little on screen to fill it.
  [[nodiscard]] bool wantsMore() const {
    if (fScrollExtent <= 0.0f) {
      return fVisible.size() < 12;
    }
    return fScrollCurrent > fScrollExtent - 600.0f;
  }

  // BeatmapListingFilterControl.resetSortControl: a query sorts by relevance,
  // otherwise by the date the category is about.
  void resetSortForSearch() {
    fFilters.fSort = !fFilters.fQuery.empty() ? Sort::kRelevance
                     : fFilters.fCategory >= Category::kPending
                         ? Sort::kUpdated
                         : Sort::kRanked;
    fFilters.fDescending = true;
  }

  // Split in two: update() settles everything -- what is in the tree, where
  // it sits, what the pointer is over -- and marks what changed, while
  // render() only draws. The client runs the first half before it decides
  // whether the frame is worth drawing at all, which is how a pointer moving
  // over nothing in particular costs nothing at all.
  void draw(const Ctx &ctx) {
    this->update(ctx);
    this->render(ctx.fCanvas);
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  void update(const Ctx &ctx) {
    fFont = ctx.fFont;
    fMouseX = ctx.fMouseX;
    fMouseY = ctx.fMouseY;
    fEntries = ctx.fEntries;
    if (ctx.fLoading != fLoading && fScene) {
      // "searching..." replaces "nothing matches", and the placeholder is
      // drawn out of this rather than held as state of its own.
      fScene->markDamaged();
    }
    fLoading = ctx.fLoading;
    // Filtering and sorting the results is O(n log n) with string comparisons
    // in it, and on almost every frame it arrives at the answer it arrived at
    // last frame. It is redone when what it depends on changes.
    const std::uint64_t visible = this->visibleShape(ctx.fEntries);
    if (visible != fVisibleShape) {
      fVisibleShape = visible;
      this->rebuildVisible(ctx.fEntries);
    }

    // The tree is rebuilt when what it shows changes, and reused otherwise.
    const std::uint64_t shape = this->treeShape(ctx);
    if (!fScene || shape != fShape) {
      // A page arriving changes the shape, but not where the reader is: the
      // offset is carried over to the tree that replaces this one.
      const float offset = fScroll != nullptr ? fScroll->current() : 0.0f;
      fShape = shape;
      fScene = this->build();
      fRebuilt = true;
      if (fScroll != nullptr && offset > 0.0f && !fScrollToStart) {
        fScroll->setCurrent(offset);
      }
    }

    const skia::SkRect screen = skia::SkRect::MakeWH(ctx.fWidth, ctx.fHeight);
    fTicking = false; // nodes counting out a delay set this again below
    fScene->updateTree(ctx.fNowMs);
    fScene->layoutIfNeeded(screen);
    if (fRebuilt) {
      // A tree that has just been built has drawn nothing yet, so nothing in
      // it has marked itself: after the first layout it knows how big it is,
      // and says so. Without this a page of results arrives into a client
      // that has been told nothing changed.
      fRebuilt = false;
      fScene->markDamaged();
    }
    if (fScrollToStart && fScroll != nullptr) {
      fScroll->scrollToStart();
      fScrollToStart = false;
    }
    if (fScrollTicks != 0.0f) {
      // After the layout, so the wheel has something with bounds to land on:
      // on the frame a tree is built there are none yet.
      fScene->scroll(ctx.fMouseX, ctx.fMouseY, fScrollTicks);
      fScrollTicks = 0.0f;
      fScene->layoutIfNeeded(screen);
    }
    // Which cards are within reach of the viewport, for a caller deciding
    // what is worth fetching a cover for. A screen's worth of margin above
    // and below, so scrolling arrives at cards that already have one.
    fOnScreen.clear();
    {
      skia::SkRect reach = screen;
      reach.outset(0.0f, screen.height());
      for (const auto &card : fCards) {
        if (card.second != nullptr &&
            skia::SkRect::Intersects(card.second->box(), reach)) {
          fOnScreen.push_back(card.first);
        }
      }
    }
    // After the layout: whether the box is on screen is a fact about where
    // it ended up, and on the frame a tree is built there is no answer yet.
    if (fSearchBox != nullptr) {
      fSearchBox->setText(fFilters.fQuery);
      fTextBoxBounds = fSearchBox->fBounds;
      fCaretLive = skia::SkRect::Intersects(fTextBoxBounds, screen);
      fSearchBox->tickCaret(ctx.fNowMs, fCaretLive);
    }
    fScene->setHover(ctx.fMouseX, ctx.fMouseY);
    if (fScroll != nullptr) {
      fScrollCurrent = fScroll->current();
      fScrollExtent = fScroll->extent();
    }
  }

  [[nodiscard]] Result click(float x, float y) {
    fPending = {};
    if (fScene) {
      fScene->click(x, y);
    }
    return fPending;
  }

  // What this screen repainted, for a caller that clips frames to damage.
  [[nodiscard]] skia::SkRect takeDamage() {
    return fScene ? fScene->takeDamage() : skia::SkRect::MakeEmpty();
  }

  [[nodiscard]] bool textBoxHit(float x, float y) const {
    return fTextBoxBounds.contains(x, y);
  }

  // Whether anything in the tree is still moving. Eased values announce
  // themselves through paint::approach; transforms do not, so they are
  // asked directly.
  [[nodiscard]] bool animating() const {
    return fTicking || (fScene && fScene->animatingTree());
  }

  // When the picture changes next without anybody touching it: the caret is
  // shown for 600 ms of every 1000. Zero when there is no such moment --
  // nothing else here changes on a clock, and a caret that is scrolled off
  // the screen does not either.
  [[nodiscard]] double nextChangeWall(double nowMs) const {
    if (!fCaretLive) {
      return 0.0;
    }
    const double phase = std::fmod(nowMs, 1000.0);
    return nowMs + (phase < 600.0 ? 600.0 - phase : 1000.0 - phase);
  }

private:
  // What a clickable filter stands for. Rows and the sort bar report their
  // hits in these terms, and the owner turns them into state changes.
  enum class Kind : std::uint8_t {
    kGeneral, kRuleset, kCategory, kGenre, kLanguage, kExtra, kRank, kPlayed,
    kExplicit, kSort, kCardSize
  };

  // ---- nodes ---------------------------------------------------------------

  // The 40px sort bar: criteria on the left, card size on the right. The
  // criteria are a tab bar; the chevron after the active one and the two
  // card-size icons belong to this screen, so they stay here.
  class SortBarNode : public widgets::TabBar {
  public:
    explicit SortBarNode(Listing *owner) : fOwner(owner) {
      fTheme = kTabTheme;
      fHeaderWidth = 20.0f;
      fFontSize = kFilterFontSize;
      fLineHeight = kSortBarHeight;
      fBaseline = kSortBarHeight * 0.5f + 5.0f;
      fSpacing = kTabSpacing * 1.5f;
      fSelectedExtra = 13.0f; // room for the direction chevron
      fWrap = false;
      fOnSelect = [owner](int value) {
        owner->activateFilter(Kind::kSort, value);
      };
    }

  protected:
    void measure(const skia::SkRect &parent) override {
      // Which criteria exist depends on the category, so the list is rebuilt
      // before it is laid out rather than when the tree was.
      std::vector<Tab> tabs;
      for (int i = 0; i < static_cast<int>(std::size(kSortLabels)); ++i) {
        if (fOwner->sortAvailable(static_cast<Sort>(i))) {
          tabs.push_back({kSortLabels[i], i});
        }
      }
      this->setTabs(std::move(tabs));
      this->setSelected(static_cast<int>(fOwner->fFilters.fSort));
      widgets::TabBar::measure(parent);
      fHeight = kSortBarHeight;

      fCardSizes.clear();
      for (int i = 0; i < 2; ++i) {
        const float ix = parent.width() - 20.0f - 14.0f -
                         static_cast<float>(1 - i) * 24.0f;
        fCardSizes.push_back(
            {skia::SkRect::MakeXYWH(ix - 2.0f, kSortBarHeight * 0.5f - 9.0f,
                                    18.0f, 18.0f),
             i});
      }
    }

    void update(double nowMs) override {
      widgets::TabBar::update(nowMs);
      int hot = -1;
      for (std::size_t i = 0; i < fCardSizes.size(); ++i) {
        if (this->localToScreen(fCardSizes[i].fRect)
                .contains(this->hoverX(), this->hoverY())) {
          hot = static_cast<int>(i);
          break;
        }
      }
      if (hot != fHotSize) {
        fHotSize = hot;
        this->markDamaged();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      widgets::TabBar::drawSelf(canvas, alpha);

      // BeatmapListingCardSizeTabControl: two icons, 10 apart, 20 from the
      // right edge.
      for (const auto &item : fCardSizes) {
        const skia::SkRect box = this->localToScreen(item.fRect);
        const bool active =
            static_cast<int>(fOwner->fFilters.fCardSize) == item.fValue;
        const bool hovered = box.contains(this->hoverX(), this->hoverY());
        skia::SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(skia::kStrokeStyle);
        stroke.setStrokeWidth(1.6f);
        stroke.setColor(active ? kContent1 : (hovered ? kLight1 : kLight3));
        stroke.setAlphaf(alpha);
        const float ix = box.fLeft + 2.0f;
        const float iy = box.centerY();
        if (item.fValue == 0) { // normal: two stacked bars
          canvas->drawRect(skia::SkRect::MakeXYWH(ix, iy - 7.0f, 14.0f, 5.0f),
                           stroke);
          canvas->drawRect(skia::SkRect::MakeXYWH(ix, iy + 2.0f, 14.0f, 5.0f),
                           stroke);
        } else { // extra: one taller bar
          canvas->drawRect(skia::SkRect::MakeXYWH(ix, iy - 7.0f, 14.0f, 14.0f),
                           stroke);
        }
      }
    }

    // The active criterion carries the direction chevron.
    void drawDecoration(skia::SkCanvas *canvas, const skia::SkRect &box,
                        float alpha) override {
      skia::SkPaint arrow;
      arrow.setAntiAlias(true);
      arrow.setColor(kContent1);
      arrow.setAlphaf(alpha);
      skia::SkPathBuilder pb;
      const float ax = box.fRight + 5.0f;
      const float ay = box.fTop + fBaseline - 4.0f;
      if (fOwner->fFilters.fDescending) {
        pb.moveTo(ax, ay - 3.0f)
            .lineTo(ax + 8.0f, ay - 3.0f)
            .lineTo(ax + 4.0f, ay + 3.0f)
            .close();
      } else {
        pb.moveTo(ax, ay + 3.0f)
            .lineTo(ax + 8.0f, ay + 3.0f)
            .lineTo(ax + 4.0f, ay - 3.0f)
            .close();
      }
      canvas->drawPath(pb.detach(), arrow);
    }

    bool onClick(float x, float y) override {
      if (widgets::TabBar::onClick(x, y)) {
        return true;
      }
      for (const auto &item : fCardSizes) {
        if (this->localToScreen(item.fRect).contains(x, y)) {
          fOwner->activateFilter(Kind::kCardSize, item.fValue);
          return true;
        }
      }
      return false;
    }

  private:
    [[nodiscard]] skia::SkRect localToScreen(const skia::SkRect &r) const {
      return skia::SkRect::MakeXYWH(fBounds.fLeft + r.fLeft,
                                    fBounds.fTop + r.fTop, r.width(),
                                    r.height());
    }

    struct Item {
      skia::SkRect fRect;
      int fValue;
    };
    Listing *fOwner;
    std::vector<Item> fCardSizes;
    int fHotSize = -1;
  };

  // BeatmapCardNormal, and its taller sibling. One node per card: it knows
  // its own hover and expansion, which is what the parallel arrays indexed by
  // entry used to be for, and those went stale whenever the list reordered.
  class CardNode : public scene::Drawable {
  public:
    CardNode(Listing *owner, int entry) : fOwner(owner), fEntry(entry) {
      fWidth = kCardWidth;
    }

    [[nodiscard]] const skia::SkRect &box() const { return fBounds; }

  protected:
    void measure(const skia::SkRect &) override {
      // Fixed, expanded or not. BeatmapCardContent keeps its height and hangs
      // the dropdown off the bottom of it as a separate thing, so opening one
      // does not shove every card below it down the page.
      fCardHeight = fOwner->fFilters.fCardSize == CardSize::kNormal
                        ? kCardNormalHeight
                        : kCardExtraHeight;
      fHeight = fCardHeight;
    }

  public:
    // How far the list under this card reaches, at its current openness.
    [[nodiscard]] float expandedHeight() const {
      const Entry &e = fOwner->fEntries[static_cast<std::size_t>(fEntry)];
      const float full =
          std::min(kExpandedMaxHeight,
                   static_cast<float>(e.fDiffs.size()) * 20.0f + 20.0f);
      return full * paint::outQuint(fExpanded);
    }
    [[nodiscard]] float expansion() const noexcept { return fExpanded; }
    [[nodiscard]] int entry() const noexcept { return fEntry; }

  protected:
    // Two eased values: the hover weight and how far the card is open. Both
    // are compared against where they were told to go, which is held from the
    // last update since working it out again needs the pointer and the entry.
    bool settling() const override {
      return std::abs(fExpand - fExpandTarget) > scene::kSettled ||
             std::abs(fExpanded - fExpandedTarget) > scene::kSettled;
    }

    void update(double nowMs) override {
      const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
      fLastMs = nowMs;
      const Entry &e = fOwner->fEntries[static_cast<std::size_t>(fEntry)];
      // BeatmapCardContent stays open while either the card or the dropdown
      // is hovered; the dropdown is over the card, so it takes the hover.
      const bool hovered = fHovered || fOwner->expansionHovered(this);
      const float previousExpand = fExpand;
      const float previousExpanded = fExpanded;
      fExpandTarget = hovered ? 1.0f : 0.0f;
      fExpand =
          paint::approach(fExpand, fExpandTarget, kTransitionMs / 6.0f, dt);
      // Hovering the bottom of the card opens it after a moment, as
      // BeatmapCardContent.ExpandAfterDelay does.
      const bool overInfo = hovered && !e.fDiffs.empty() &&
                            fOwner->fMouseY > fBounds.fTop + fCardHeight -
                                                  22.0f &&
                            fOwner->fMouseY < fBounds.fTop + fCardHeight;
      fHoverMs = overInfo ? fHoverMs + dt : 0.0;
      const bool wantExpanded =
          fHoverMs > kExpandDelayMs || (fExpanded > 0.5f && hovered);
      // A card waiting out the delay changes nothing on screen, so nothing
      // marks itself -- and a client that only draws when something changed
      // would stop calling this, which would stop the delay from passing.
      if (overInfo && !wantExpanded) {
        fOwner->fTicking = true;
      }
      fExpandedTarget = wantExpanded ? 1.0f : 0.0f;
      fExpanded =
          paint::approach(fExpanded, fExpandedTarget, kTransitionMs / 5.0f, dt);
      // Which card owns the dropdown outlives the pass that decides it: a
      // card asks whether the dropdown under it is hovered, and clearing this
      // before the passes ran meant the answer was always no -- so moving the
      // pointer onto the list closed the list.
      if (fExpanded > 0.01f) {
        fOwner->fExpandedCard = this;
      } else if (fOwner->fExpandedCard == this) {
        fOwner->fExpandedCard = nullptr;
      }
      if (fExpand != previousExpand) {
        this->markDamaged(); // the buttons on the right widen with the hover
      }
      if (fExpanded != previousExpanded) {
        // The card itself does not change size any more; what moves is the
        // panel hanging under it, which is laid out from here.
        fOwner->invalidateExpansion();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      Entry &e = fOwner->fEntries[static_cast<std::size_t>(fEntry)];
      auto &font = *fOwner->fFont;
      const paint::Painter p(canvas, font);
      const bool extra = fOwner->fFilters.fCardSize == CardSize::kExtra;
      const skia::SkRect card = skia::SkRect::MakeXYWH(
          fBounds.fLeft, fBounds.fTop, kCardWidth, fCardHeight);
      const float h = card.height();
      const float buttonsW = kButtonsCollapsed +
                             (kButtonsExpanded - kButtonsCollapsed) * fExpand;

      p.fillRounded(card, kCardCorner, kBackground2, alpha);
      const skia::SkRect main = skia::SkRect::MakeLTRB(
          card.fLeft, card.fTop, card.fRight - buttonsW, card.fBottom);

      // Cover art: a square of the card's height on the left, and the same
      // image dimmed behind the text.
      const skia::SkRect thumb =
          skia::SkRect::MakeXYWH(card.fLeft, card.fTop, h, h);
      canvas->save();
      canvas->clipRRect(
          skia::SkRRect::MakeRectXY(main, kCardCorner, kCardCorner), true);
      p.fillRect(main, kBackground3, alpha);
      if (e.fThumbSt == Entry::Thumb::kReady && e.fThumb) {
        // Laid out across the whole card and clipped to the main area, so
        // the button strip slides over the artwork rather than squeezing it.
        // lazer resizes the area the cover fills, which re-crops it -- a few
        // per cent of zoom on a 900x250 cover, and rather more than that on
        // whatever a mirror hands us.
        p.imageFilled(
            e.fThumb.get(),
            skia::SkRect::MakeXYWH(card.fLeft + h - kCardCorner, card.fTop,
                                   card.width() - h + kCardCorner, h));
        p.fillRect(skia::SkRect::MakeLTRB(card.fLeft + h - kCardCorner,
                                          card.fTop, main.fRight, card.fBottom),
                   kBackground6, alpha * (fHovered ? 0.9f : 0.8f));
        p.imageFilled(e.fThumb.get(), thumb);
      }
      canvas->restore();

      // Main content, inset 10 horizontal and 4 vertical.
      const float tx = card.fLeft + h + 10.0f - kCardCorner;
      const float tw = main.fRight - tx - 10.0f;
      // Five rows have to fit in eighty pixels, and the bottom two are boxes
      // rather than text: the status pill is 15 tall and sits from the
      // baseline less 11, and the statistics line above it needs its own 11.
      // Counted from the bottom up, which is the only way this comes out.
      float ty = card.fTop + 17.0f;
      p.textClipped(e.fTitleUnicode.empty() ? e.fTitle : e.fTitleUnicode, tx,
                    ty, tw, 18.0f, kContent1, alpha, true);
      ty += 15.0f;
      p.textClipped(
          "by " + (e.fArtistUnicode.empty() ? e.fArtist : e.fArtistUnicode), tx,
          ty, tw, 14.0f, kContent1, alpha, true);
      if (extra) {
        ty += 13.0f;
        p.textClipped(e.fSource, tx, ty, tw, 11.0f, kContent2, alpha, true);
      } else {
        ty += 13.0f;
        const std::string mapped = "mapped by ";
        p.text(mapped, tx, ty, 11.0f, kContent2, alpha, true);
        p.textClipped(e.fCreator, tx + p.measure(mapped, 11.0f, true), ty, tw,
                      11.0f, kContent1, alpha, true);
      }

      const float bottom = card.fBottom - 8.0f;
      if (e.fSt == Entry::St::kFetching) {
        // BeatmapCardDownloadProgressBar: 5 high, across the bottom content.
        const skia::SkRect bar =
            skia::SkRect::MakeXYWH(tx, card.fBottom - 9.0f, tw, 5.0f);
        p.fillRounded(bar, 2.5f, kBackground6, alpha);
        p.fillRounded(skia::SkRect::MakeXYWH(bar.fLeft, bar.fTop,
                                             bar.width() * e.fProgress, 5.0f),
                      2.5f, kColour1, alpha);
      } else if (extra) {
        const std::string mapped = "mapped by ";
        p.text(mapped, tx, bottom - 34.0f, 11.0f, kContent2, alpha, true);
        p.textClipped(e.fCreator, tx + p.measure(mapped, 11.0f, true),
                      bottom - 34.0f, tw, 11.0f, kContent1, alpha, true);
        this->drawStatistics(canvas, font, tx, bottom - 20.0f, tw, e, alpha);
        this->drawExtraInfoRow(canvas, font, tx, bottom, tw, e, alpha);
      } else {
        // The statistics row keeps its place whether or not it is showing --
        // AlwaysPresent, over there -- so the line above it is laid out
        // clear of it instead of being run into when a card is hovered.
        if (fExpand > 0.01f) {
          this->drawStatistics(canvas, font, tx, bottom - 15.0f, tw, e,
                               alpha * fExpand);
        }
        this->drawExtraInfoRow(canvas, font, tx, bottom, tw, e, alpha);
      }

      this->drawThumbnailPlay(canvas, thumb, e, alpha);
      this->drawButtons(canvas, card, main, e, alpha);

    }

    bool acceptsInput() const override { return true; }
    bool hoverChangesAppearance() const override { return true; }

    bool onClick(float x, float y) override {
      const skia::SkRect card = skia::SkRect::MakeXYWH(
          fBounds.fLeft, fBounds.fTop, kCardWidth, fCardHeight);
      if (!card.contains(x, y)) {
        return false; // the expanded panel below it is not a target
      }
      const float buttonsW = kButtonsCollapsed +
                             (kButtonsExpanded - kButtonsCollapsed) * fExpand;
      if (x >= card.fRight - buttonsW) {
        fOwner->fPending = {Action::kDownload,
                            static_cast<std::size_t>(fEntry)};
        return true;
      }
      if (x <= card.fLeft + fCardHeight) {
        fOwner->fPending = {Action::kPreview,
                            static_cast<std::size_t>(fEntry)};
        return true;
      }
      fOwner->fPending = {Action::kOpen, static_cast<std::size_t>(fEntry)};
      return true;
    }

  private:
    // The statistics that fade in with a hover on the normal card and sit in
    // the bottom content of the extra one.
    void drawStatistics(skia::SkCanvas *canvas, skia::SkFont &font, float x,
                        float baseline, float maxW, const Entry &e,
                        float alpha) {
      const paint::Painter p(canvas, font);
      p.textClipped(std::format("{} plays    {} favourites    {}", e.fPlayCount,
                                e.fFavouriteCount, e.fUpdated),
                    x, baseline, maxW, 11.0f, kContent2, alpha);
    }

    // BeatmapCardExtraInfoRow: the status pill and the difficulty spectrum.
    void drawExtraInfoRow(skia::SkCanvas *canvas, skia::SkFont &font, float x,
                          float baseline, float maxW, const Entry &e,
                          float alpha) {
      const paint::Painter p(canvas, font);
      const std::string status = e.fStatus.empty() ? "unknown" : e.fStatus;
      const float pillW = p.measure(status, 11.0f, true) + 12.0f;
      const skia::SkRect pill =
          skia::SkRect::MakeXYWH(x, baseline - 11.0f, pillW, 15.0f);
      p.fillRounded(pill, 7.5f, statusColour(status), alpha);
      p.text(status, x + 6.0f, baseline - 1.0f, 11.0f,
             skia::colorSetARGB(255, 20, 24, 26), alpha, true);

      // DifficultySpectrumDisplay: 5x10 dots, 1px apart, in star order.
      float dx = x + pillW + 8.0f;
      for (const auto &diff : e.fDiffs) {
        if (dx + 6.0f > x + maxW) {
          break;
        }
        p.fillRounded(skia::SkRect::MakeXYWH(dx, baseline - 10.0f, 5.0f, 10.0f),
                      1.0f, client::palette::starColor(diff.fStars), alpha);
        dx += 6.0f;
      }
    }

    // BeatmapCardThumbnail's PlayButton, with its CircularProgress.
    void drawThumbnailPlay(skia::SkCanvas *canvas, const skia::SkRect &thumb,
                           const Entry &e, float alpha) {
      const paint::Painter p(canvas, *fOwner->fFont);
      const bool playing = fOwner->fPreviewId == e.fSetId;
      if (!fHovered && !playing) {
        return;
      }
      p.fillRect(thumb, kBackground6, alpha * 0.6f);
      skia::SkPaint glyph;
      glyph.setAntiAlias(true);
      glyph.setColor(kContent1);
      glyph.setAlphaf(alpha);
      const float cx = thumb.centerX();
      const float cy = thumb.centerY();
      if (!playing) {
        skia::SkPathBuilder tri;
        tri.moveTo(cx - 6.0f, cy - 9.0f)
            .lineTo(cx + 9.0f, cy)
            .lineTo(cx - 6.0f, cy + 9.0f)
            .close();
        canvas->drawPath(tri.detach(), glyph);
        return;
      }
      p.fillRect(skia::SkRect::MakeXYWH(cx - 6.0f, cy - 8.0f, 4.0f, 16.0f),
                 kContent1, alpha);
      p.fillRect(skia::SkRect::MakeXYWH(cx + 2.0f, cy - 8.0f, 4.0f, 16.0f),
                 kContent1, alpha);
      skia::SkPaint ring;
      ring.setAntiAlias(true);
      ring.setStyle(skia::kStrokeStyle);
      ring.setStrokeWidth(3.0f);
      ring.setColor(kColour1);
      ring.setAlphaf(alpha);
      const float r = 18.0f;
      canvas->drawArc(
          skia::SkRect::MakeXYWH(cx - r, cy - r, r * 2.0f, r * 2.0f), -90.0f,
          360.0f * std::clamp(fOwner->fPreviewProgress, 0.0f, 1.0f), false,
          ring);
    }

    void drawButtons(skia::SkCanvas *canvas, const skia::SkRect &card,
                     const skia::SkRect &main, const Entry &e, float alpha) {
      const paint::Painter p(canvas, *fOwner->fFont);
      const skia::SkRect buttons = skia::SkRect::MakeLTRB(
          main.fRight, card.fTop, card.fRight, card.fBottom);
      p.fillRounded(buttons, kCardCorner, kBackground3, alpha);
      p.fillRect(skia::SkRect::MakeXYWH(buttons.fLeft - kCardCorner,
                                        buttons.fTop, kCardCorner,
                                        buttons.height()),
                 kBackground3, alpha);
      if (fExpand <= 0.4f) {
        return;
      }
      skia::SkPaint stroke;
      stroke.setAntiAlias(true);
      stroke.setColor(kContent2);
      stroke.setAlphaf(alpha * fExpand);
      stroke.setStyle(skia::kStrokeStyle);
      stroke.setStrokeWidth(1.6f);
      const float cx = buttons.centerX();
      const float hy = card.fTop + card.height() * 0.25f;
      skia::SkPathBuilder heart;
      heart.moveTo(cx, hy + 4.0f)
          .cubicTo(cx - 8.0f, hy - 2.0f, cx - 3.0f, hy - 7.0f, cx, hy - 2.0f)
          .cubicTo(cx + 3.0f, hy - 7.0f, cx + 8.0f, hy - 2.0f, cx, hy + 4.0f);
      canvas->drawPath(heart.detach(), stroke);

      const float dy = card.fTop + card.height() * 0.75f;
      skia::SkPaint solid;
      solid.setAntiAlias(true);
      solid.setAlphaf(alpha * fExpand);
      solid.setColor(e.fSt == Entry::St::kDone ? kColour1 : kContent1);
      skia::SkPathBuilder arrow;
      arrow.moveTo(cx - 5.0f, dy - 1.0f)
          .lineTo(cx + 5.0f, dy - 1.0f)
          .lineTo(cx, dy + 5.0f)
          .close();
      canvas->drawPath(arrow.detach(), solid);
      canvas->drawLine(cx, dy - 7.0f, cx, dy - 1.0f, stroke);
    }

    // BeatmapCardDifficultyList: one row per difficulty under the card.
    void drawExpanded(skia::SkCanvas *canvas, skia::SkFont &font,
                      const skia::SkRect &card, const Entry &e, float alpha) {
      const paint::Painter p(canvas, font);
      const float rowH = 20.0f;
      const float height = fBounds.fBottom - card.fBottom;
      const skia::SkRect panel = skia::SkRect::MakeXYWH(
          card.fLeft, card.fBottom - kCardCorner, card.width(),
          height + kCardCorner);
      p.fillRounded(panel, kCardCorner, kBackground4, alpha * fExpanded);
      canvas->save();
      canvas->clipRect(panel);
      float y = card.fBottom + 10.0f; // Padding: horizontal 8, vertical 10
      for (const auto &diff : e.fDiffs) {
        if (y > panel.fBottom - 4.0f) {
          break;
        }
        const std::string stars = std::format("{:.2f}", diff.fStars);
        const float pillW = p.measure(stars, 11.0f, true) + 16.0f;
        const skia::SkRect pill =
            skia::SkRect::MakeXYWH(card.fLeft + 8.0f, y - 12.0f, pillW, 16.0f);
        p.fillRounded(pill, 8.0f, client::palette::starColor(diff.fStars),
                      alpha * fExpanded);
        p.text(stars, pill.fLeft + 8.0f, y, 11.0f,
               skia::colorSetARGB(255, 20, 24, 26), alpha * fExpanded, true);
        p.textClipped(diff.fVersion, pill.fRight + 6.0f, y,
                      card.width() - pillW - 24.0f, 14.0f, kContent1,
                      alpha * fExpanded, true);
        y += rowH;
      }
      canvas->restore();
    }

    Listing *fOwner;
    int fEntry;
    float fCardHeight = kCardNormalHeight;
    float fExpand = 0.0f;   // button column and statistics
    float fExpanded = 0.0f; // the difficulty list under the card
    // Where each was last told to go, kept so settling() can answer without
    // the pointer and the entry it would need to work them out again.
    float fExpandTarget = 0.0f;
    float fExpandedTarget = 0.0f;
    double fHoverMs = 0.0;
    double fLastMs = 0.0;
  };

  // BeatmapCardContent's dropdown. A drawable of its own, hanging under
  // whichever card is open, drawn after the grid and hit-tested before it:
  // inside the card it would have been under every card that comes after it
  // in the flow, and it would have had to make the card taller to fit, which
  // is what pushed the rest of the listing down the page.
  class ExpansionNode : public scene::Drawable {
  public:
    explicit ExpansionNode(Listing *owner) : fOwner(owner) {}

  protected:
    void measure(const skia::SkRect &parent) override {
      const CardNode *card = fOwner->fExpandedCard;
      const float height = card != nullptr ? card->expandedHeight() : 0.0f;
      fVisible = card != nullptr && height > 1.0f;
      if (!fVisible) {
        fWidth = 0.0f;
        fHeight = 0.0f;
        return;
      }
      const skia::SkRect &box = card->box();
      fX = box.fLeft - parent.fLeft;
      fY = box.fBottom - kCardCorner - parent.fTop;
      fWidth = box.width();
      fHeight = height + kCardCorner;
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const CardNode *card = fOwner->fExpandedCard;
      if (card == nullptr) {
        return;
      }
      auto &font = *fOwner->fFont;
      const paint::Painter p(canvas, font);
      const Entry &e =
          fOwner->fEntries[static_cast<std::size_t>(card->entry())];
      const float open = alpha * card->expansion();
      p.fillRounded(fBounds, kCardCorner, kBackground4, open);
      // ExpandedContentScrollContainer: 8 either side, 10 top and bottom.
      float y = fBounds.fTop + kCardCorner + 10.0f;
      for (const auto &diff : e.fDiffs) {
        if (y > fBounds.fBottom - 4.0f) {
          break;
        }
        const std::string stars = std::format("{:.2f}", diff.fStars);
        const float pillW = p.measure(stars, 11.0f, true) + 16.0f;
        const skia::SkRect pill = skia::SkRect::MakeXYWH(
            fBounds.fLeft + 8.0f, y - 12.0f, pillW, 16.0f);
        p.fillRounded(pill, 8.0f, client::palette::starColor(diff.fStars),
                      open);
        p.text(stars, pill.fLeft + 8.0f, y, 11.0f,
               skia::colorSetARGB(255, 20, 24, 26), open, true);
        p.textClipped(diff.fVersion, pill.fRight + 6.0f, y,
                      fBounds.width() - pillW - 24.0f, 14.0f, kContent1, open,
                      true);
        y += 20.0f;
      }
    }

    bool acceptsInput() const override { return fVisible; }

    // It takes input to keep it off the card underneath, not because it
    // lights up: the pointer crossing it is not a repaint.
    bool hoverChangesAppearance() const override { return false; }

    // Swallowed rather than passed through: the card underneath is covered.
    bool onClick(float, float) override { return true; }

  private:
    Listing *fOwner;
  };

  // The dropdown belongs to the tree rather than to the card, so the card
  // asks about it rather than looking only at its own hover.
  [[nodiscard]] bool expansionHovered(const CardNode *card) const {
    return fExpansion != nullptr && fExpansion->hovered() &&
           fExpandedCard == card;
  }

  void invalidateExpansion() {
    if (fExpansion != nullptr) {
      fExpansion->invalidateLayout();
    }
  }

  // OverlayHeader with its title and description.
  class HeaderNode : public scene::Drawable {
  public:
    explicit HeaderNode(Listing *owner) : fOwner(owner) {}

  protected:
    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      auto &font = *fOwner->fFont;
      const paint::Painter p(canvas, font);
      p.fillRect(fBounds, kBackground5, alpha);
      const float iconSize = 30.0f;
      const float x = fBounds.fLeft + kHorizontalPadding;
      skia::SkPaint icon;
      icon.setAntiAlias(true);
      icon.setColor(kContent2);
      icon.setAlphaf(alpha);
      icon.setStyle(skia::kStrokeStyle);
      icon.setStrokeWidth(2.5f);
      const float cx = x + iconSize * 0.5f;
      canvas->drawCircle(cx, fBounds.centerY(), iconSize * 0.36f, icon);
      canvas->drawCircle(cx, fBounds.centerY(), iconSize * 0.12f, icon);
      const std::string title = "beatmap listing";
      const float titleX = x + iconSize + 10.0f;
      p.text(title, titleX, fBounds.centerY() + 7.0f, 20.0f, kContent1, alpha);
      p.text("browse for new beatmaps",
             titleX + p.measure(title, 20.0f) + 12.0f, fBounds.centerY() + 6.0f,
             14.0f, kContent2, alpha * 0.8f);
    }

  private:
    Listing *fOwner;
  };

  // NotFoundDrawable: 250 high, its text centred.
  class EmptyNode : public scene::Drawable {
  public:
    explicit EmptyNode(Listing *owner) : fOwner(owner) {}

  protected:
    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      const paint::Painter p(canvas, *fOwner->fFont);
      p.textCentered(fOwner->fLoading ? "searching..."
                                      : "no beatmaps match your criteria!",
                     fBounds.centerX(), fBounds.centerY(), 16.0f, kContent2,
                     alpha);
    }

  private:
    Listing *fOwner;
  };

  // ---- the tree -----------------------------------------------------------

  [[nodiscard]] std::unique_ptr<scene::Drawable> build() {
    fScroll = nullptr;
    fSearchBox = nullptr;
    fExpansion = nullptr;
    fExpandedCard = nullptr;
    fCards.clear();

    auto root = scene::make<nodes::Box>(
        {.roles = {scene::role<style::Root>}}, kBackground6);

    auto scroll = scene::make<nodes::ScrollContainer>(
        {.roles = {scene::role<style::Scroll>}});
    fScroll = scroll.get();

    auto column = scene::make<nodes::FillFlow>(
        {.roles = {scene::role<style::Column>}},
        nodes::FillFlow::Direction::kVertical);

    column->add<HeaderNode>({.roles = {scene::role<style::Header>}}, this);

    // BeatmapListingSearchControl over Dark6: padded 20 vertical and
    // HORIZONTAL_PADDING horizontal, contents 20 apart.
    auto panel = scene::make<nodes::Box>(
        {.roles = {scene::role<style::SearchPanel>}}, kDark6);

    auto panelColumn = scene::make<nodes::FillFlow>(
        {.roles = {scene::role<style::SearchColumn>}},
        nodes::FillFlow::Direction::kVertical);
    panelColumn->setSpacing(0.0f, 20.0f);
    // BeatmapSearchTextBox: OsuTextBox is 40 high with a 5px corner radius.
    fSearchBox = panelColumn->add<widgets::TextBox>(
        {.roles = {scene::role<style::SearchBox>}}, "type in keywords...");
    fSearchBox->fSearchIcon = true;
    fSearchBox->fTheme = kTextBoxTheme;

    // The filter rows, indented 10 and spaced 5, in lazer's order.
    auto rows = scene::make<nodes::FillFlow>(
        {.roles = {scene::role<style::FilterRows>}},
        nodes::FillFlow::Direction::kVertical);
    rows->setSpacing(0.0f, kRowSpacing);
    // BeatmapSearchFilterRow: a 100px caption column beside a wrapping row
    // of tabs. Three of these are sets of toggles rather than one-of-many,
    // which is why the bar asks rather than being told what is on.
    const auto filterRow = [&](const char *header,
                               std::span<const char *const> labels, Kind kind) {
      auto *row = rows->add<widgets::TabBar>(
          {.roles = {scene::role<style::FilterRow>}});
      row->fTheme = kTabTheme;
      row->fHeader = header;
      row->fHeaderWidth = kRowLabelWidth;
      row->fFontSize = kFilterFontSize;
      row->fLineHeight = kFilterLineHeight;
      row->fSpacing = kTabSpacing;
      std::vector<widgets::TabBar::Tab> tabs;
      for (std::size_t i = 0; i < labels.size(); ++i) {
        tabs.push_back({labels[i], static_cast<int>(i)});
      }
      row->setTabs(std::move(tabs));
      row->fIsActive = [this, kind](int value) {
        return this->filterActive(kind, value);
      };
      row->fOnSelect = [this, kind](int value) {
        this->activateFilter(kind, value);
      };
    };
    filterRow("General", kGeneralLabels, Kind::kGeneral);
    filterRow("Mode", kRulesetLabels, Kind::kRuleset);
    filterRow("Categories", kCategoryLabels, Kind::kCategory);
    filterRow("Genre", kGenreLabels, Kind::kGenre);
    filterRow("Language", kLanguageLabels, Kind::kLanguage);
    filterRow("Extra", kExtraLabels, Kind::kExtra);
    filterRow("Rank Achieved", kRankLabels, Kind::kRank);
    filterRow("Played", kPlayedLabels, Kind::kPlayed);
    filterRow("Explicit Content", kExplicitLabels, Kind::kExplicit);
    panelColumn->add(std::move(rows));
    panel->add(std::move(panelColumn));
    column->add(std::move(panel));

    column->add<SortBarNode>({.roles = {scene::role<style::SortBar>}}, this);

    // The cards, in panelTarget's 20px padding: as many per row as fit, 10
    // apart, rows centred -- which is the flow's job, not arithmetic here.
    if (fVisible.empty()) {
      column->add<EmptyNode>({.roles = {scene::role<style::Empty>}}, this);
    } else {
      auto grid = scene::make<nodes::FillFlow>(
          {.roles = {scene::role<style::Cards>}},
          nodes::FillFlow::Direction::kHorizontal);
      grid->setSpacing(kCardSpacing, kCardSpacing);
      grid->fCentreRows = true;
      for (const int idx : fVisible) {
        auto card = scene::make<CardNode>({}, this, idx);
        fCards.emplace_back(idx, card.get());
        grid->add(std::move(card));
      }
      column->add(std::move(grid));
    }

    scroll->add(std::move(column));
    // After the column, so it draws over the cards and is asked about clicks
    // before they are -- and inside the scroll, so it travels with them.
    auto expansion = scene::make<ExpansionNode>(
        {.roles = {scene::role<style::Expansion>}}, this);
    fExpansion = expansion.get();
    scroll->add(std::move(expansion));
    root->add(std::move(scroll));
    root->setStyleSheet<ListingTheme>();
    return root;
  }

  // What the tree is built from: rebuilt when any of this changes, reused
  // when none of it does.
  // A number rather than a string: this is compared on every frame, and
  // formatting one set id per card each time was work spent to conclude that
  // nothing had changed.
  [[nodiscard]] std::uint64_t treeShape(const Ctx &ctx) const {
    std::uint64_t hash = 1469598103934665603ULL; // FNV-1a
    const auto mix = [&hash](std::uint64_t value) {
      hash ^= value;
      hash *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(ctx.fWidth));
    mix(static_cast<std::uint64_t>(ctx.fHeight));
    mix(static_cast<std::uint64_t>(fFilters.fCardSize));
    mix(static_cast<std::uint64_t>(fFilters.fSort));
    mix(fFilters.fDescending ? 1 : 0);
    mix(fVisible.size());
    for (const int idx : fVisible) {
      mix(static_cast<std::uint64_t>(
          ctx.fEntries[static_cast<std::size_t>(idx)].fSetId));
    }
    return hash;
  }

  // ---- filters -------------------------------------------------------------

  [[nodiscard]] bool filterActive(Kind kind, int value) const {
    const auto index = static_cast<std::size_t>(value);
    switch (kind) {
    case Kind::kGeneral: return fFilters.fGeneral[index];
    case Kind::kRuleset: return fFilters.fRuleset == value;
    case Kind::kCategory: return static_cast<int>(fFilters.fCategory) == value;
    case Kind::kGenre: return static_cast<int>(fFilters.fGenre) == value;
    case Kind::kLanguage: return static_cast<int>(fFilters.fLanguage) == value;
    case Kind::kExtra: return fFilters.fExtra[index];
    case Kind::kRank: return fFilters.fRanks[index];
    case Kind::kPlayed: return static_cast<int>(fFilters.fPlayed) == value;
    case Kind::kExplicit: return static_cast<int>(fFilters.fExplicit) == value;
    case Kind::kSort: return static_cast<int>(fFilters.fSort) == value;
    case Kind::kCardSize: return static_cast<int>(fFilters.fCardSize) == value;
    }
    return false;
  }

  void activateFilter(Kind kind, int value) {
    // A filter changes what a row draws whether or not it changes what the
    // results are, and the rows draw their state out of the filters rather
    // than holding it. Cheap to mark the tree here: this happens on a click.
    if (fScene) {
      fScene->markDamaged();
    }
    const auto index = static_cast<std::size_t>(value);
    switch (kind) {
    case Kind::kGeneral:
      fFilters.fGeneral[index] = !fFilters.fGeneral[index];
      fPending = {Action::kRefilter};
      return;
    case Kind::kRuleset:
      fFilters.fRuleset = value;
      fPending = {Action::kSearch}; // the mirror filters by ruleset
      return;
    case Kind::kCategory:
      fFilters.fCategory = static_cast<Category>(value);
      fPending = {Action::kSearch}; // and by status
      return;
    case Kind::kGenre:
      fFilters.fGenre = static_cast<Genre>(value);
      fPending = {Action::kSearch};
      return;
    case Kind::kLanguage:
      fFilters.fLanguage = static_cast<Language>(value);
      fPending = {Action::kSearch};
      return;
    case Kind::kExtra:
      fFilters.fExtra[index] = !fFilters.fExtra[index];
      fPending = {Action::kSearch};
      return;
    case Kind::kRank:
      fFilters.fRanks[index] = !fFilters.fRanks[index];
      fPending = {Action::kRefilter};
      return;
    case Kind::kPlayed:
      fFilters.fPlayed = static_cast<Played>(value);
      fPending = {Action::kRefilter};
      return;
    case Kind::kExplicit:
      fFilters.fExplicit = static_cast<Explicit>(value);
      fPending = {Action::kSearch};
      return;
    case Kind::kSort: {
      const auto sort = static_cast<Sort>(value);
      // Clicking the active criterion flips the direction, as SortTabControl
      // does; picking another resets to descending. Either way the mirror
      // sorts, so the query is asked again.
      if (sort == fFilters.fSort) {
        fFilters.fDescending = !fFilters.fDescending;
      } else {
        fFilters.fSort = sort;
        fFilters.fDescending = true;
      }
      fPending = {Action::kSearch};
      return;
    }
    case Kind::kCardSize:
      fFilters.fCardSize = static_cast<CardSize>(value);
      fPending = {Action::kRefilter};
      return;
    }
  }

  [[nodiscard]] bool sortAvailable(Sort sort) const {
    const Category cat = fFilters.fCategory;
    switch (sort) {
    case Sort::kUpdated:
      return cat == Category::kAny || cat > Category::kLoved;
    case Sort::kRanked:
      return cat < Category::kPending || cat == Category::kMine;
    case Sort::kRelevance:
      return !fFilters.fQuery.empty();
    case Sort::kNominations:
      return cat == Category::kPending;
    default:
      return true;
    }
  }

  // What the visible set is computed from: the results themselves, and the
  // filters that select and order them.
  [[nodiscard]] std::uint64_t visibleShape(std::span<const Entry> entries)
      const {
    std::uint64_t hash = 1469598103934665603ULL; // FNV-1a
    const auto mix = [&hash](std::uint64_t value) {
      hash ^= value;
      hash *= 1099511628211ULL;
    };
    mix(entries.size());
    for (const Entry &e : entries) {
      mix(static_cast<std::uint64_t>(e.fSetId));
    }
    mix(static_cast<std::uint64_t>(fFilters.fCategory));
    mix(static_cast<std::uint64_t>(fFilters.fGenre));
    mix(static_cast<std::uint64_t>(fFilters.fLanguage));
    mix(static_cast<std::uint64_t>(fFilters.fExplicit));
    mix(static_cast<std::uint64_t>(fFilters.fSort));
    mix(fFilters.fDescending ? 1 : 0);
    mix(fFilters.fExtra[0] ? 1 : 0);
    mix(fFilters.fExtra[1] ? 1 : 0);
    mix(fFilters.fGeneral[3] ? 1 : 0);
    mix(fFilters.fGeneral[4] ? 1 : 0);
    return hash;
  }

  void rebuildVisible(std::span<const Entry> entries) {
    fVisible.clear();
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const Entry &e = entries[i];
      if (fFilters.fCategory == Category::kLeaderboard &&
          !(e.fStatus == "ranked" || e.fStatus == "approved" ||
            e.fStatus == "qualified" || e.fStatus == "loved")) {
        continue;
      }
      if (fFilters.fGenre != Genre::kAny &&
          e.fGenre != genreId(fFilters.fGenre)) {
        continue;
      }
      if (fFilters.fLanguage != Language::kAny &&
          e.fLanguage != languageId(fFilters.fLanguage)) {
        continue;
      }
      if (fFilters.fExtra[0] && !e.fVideo) {
        continue;
      }
      if (fFilters.fExtra[1] && !e.fStoryboard) {
        continue;
      }
      if (fFilters.fExplicit == Explicit::kHide && e.fNsfw) {
        continue;
      }
      if (fFilters.fGeneral[3] && !e.fSpotlight) {
        continue;
      }
      if (fFilters.fGeneral[4] && !e.fFeatured) {
        continue;
      }
      fVisible.push_back(static_cast<int>(i));
    }
    const auto less = [&](int a, int b) {
      const Entry &x = entries[static_cast<std::size_t>(a)];
      const Entry &y = entries[static_cast<std::size_t>(b)];
      switch (fFilters.fSort) {
      case Sort::kTitle:
        return x.fTitle < y.fTitle;
      case Sort::kArtist:
        return x.fArtist < y.fArtist;
      case Sort::kDifficulty:
        return x.fStarsMax < y.fStarsMax;
      case Sort::kUpdated:
        return x.fUpdatedDate < y.fUpdatedDate;
      case Sort::kRanked:
        return x.fRankedDate < y.fRankedDate;
      case Sort::kRating:
        return x.fRating < y.fRating;
      case Sort::kPlays:
        return x.fPlayCount < y.fPlayCount;
      case Sort::kFavourites:
        return x.fFavouriteCount < y.fFavouriteCount;
      case Sort::kRelevance:
      case Sort::kNominations:
        return a < b; // the order the search returned
      }
      return a < b;
    };
    std::ranges::stable_sort(fVisible, [&](int a, int b) {
      return fFilters.fDescending ? less(b, a) : less(a, b);
    });
  }

  [[nodiscard]] static int genreId(Genre g) {
    constexpr int kIds[] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14};
    return kIds[static_cast<std::size_t>(g)];
  }

  [[nodiscard]] static int languageId(Language l) {
    // osu! language ids, in the display order above.
    constexpr int kIds[] = {0, 2, 4, 7, 8, 11, 3, 6, 10, 9, 12, 13, 5, 14, 1};
    return kIds[static_cast<std::size_t>(l)];
  }

  Filters fFilters;
  std::vector<int> fVisible;
  std::span<Entry> fEntries;
  skia::SkFont *fFont = nullptr;
  bool fLoading = false;
  float fMouseX = 0.0f, fMouseY = 0.0f;
  long fPreviewId = -1;
  float fPreviewProgress = 0.0f;
  skia::SkRect fTextBoxBounds = skia::SkRect::MakeEmpty();

  // The tree, and what it was built for.
  std::unique_ptr<scene::Drawable> fScene;
  // The cards by entry, so a cover that arrives can mark the one it belongs
  // to. Rebuilt with the tree, so these never outlive what they point at.
  std::vector<std::pair<int, CardNode *>> fCards;
  std::uint64_t fShape = 0;
  bool fRebuilt = false;
  bool fTicking = false; // something in the tree is waiting on the clock
  bool fCaretLive = false; // the search box is on screen, so it blinks
  CardNode *fExpandedCard = nullptr; // whose dropdown is open, if any
  ExpansionNode *fExpansion = nullptr;
  std::vector<int> fOnScreen; // entries whose cards are within reach
  widgets::TextBox *fSearchBox = nullptr;
  std::uint64_t fVisibleShape = 0;
  nodes::ScrollContainer *fScroll = nullptr; // owned by the tree
  float fScrollTicks = 0.0f;
  bool fScrollToStart = false;
  float fScrollCurrent = 0.0f;
  float fScrollExtent = 0.0f;
  Result fPending;
};

} // namespace client::listing
