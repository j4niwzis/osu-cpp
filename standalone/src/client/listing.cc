export module client.listing;

import std;
import skia;
import client.ui;
import client.scene;
import client.nodes;

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

namespace scene = client::scene;
namespace nodes = client::nodes;

// Painting shared by the nodes below. Free functions rather than methods,
// because every node needs them and none of them owns them.
namespace paint {

inline void rect(skia::SkCanvas *canvas, const skia::SkRect &r,
                 skia::SkColor colour, float alpha = 1.0f) {
  skia::SkPaint p;
  p.setAntiAlias(true);
  p.setColor(colour);
  p.setAlphaf(alpha);
  canvas->drawRect(r, p);
}

inline void rounded(skia::SkCanvas *canvas, const skia::SkRect &r,
                    float radius, skia::SkColor colour, float alpha = 1.0f) {
  skia::SkPaint p;
  p.setAntiAlias(true);
  p.setColor(colour);
  p.setAlphaf(alpha);
  canvas->drawRRect(skia::SkRRect::MakeRectXY(r, radius, radius), p);
}

// FillMode.Fill: cropped to the destination's aspect ratio, not squashed.
inline void imageFilled(skia::SkCanvas *canvas, const skia::SkImage *image,
                        const skia::SkRect &dst) {
  const float iw = static_cast<float>(image->width());
  const float ih = static_cast<float>(image->height());
  if (iw <= 0.0f || ih <= 0.0f) {
    return;
  }
  const float scale = std::max(dst.width() / iw, dst.height() / ih);
  const float srcW = dst.width() / scale;
  const float srcH = dst.height() / scale;
  const skia::SkRect src = skia::SkRect::MakeXYWH((iw - srcW) * 0.5f,
                                                  (ih - srcH) * 0.5f, srcW,
                                                  srcH);
  canvas->drawImageRect(image, src, dst,
                        skia::SkSamplingOptions(skia::SkFilterMode::kLinear),
                        nullptr, skia::SkCanvas::kStrict_SrcRectConstraint);
}

inline float measure(skia::SkFont &font, const std::string &s, float size,
                     bool bold) {
  font.setSize(size);
  client::ui::fonts().applyWeight(font, bold);
  const float w = client::ui::fonts().measure(font, s);
  client::ui::fonts().applyWeight(font, false);
  return w;
}

inline void text(skia::SkCanvas *canvas, skia::SkFont &font,
                 const std::string &s, float x, float y, float size,
                 skia::SkColor colour, bool bold = false, float alpha = 1.0f) {
  font.setSize(size);
  client::ui::fonts().applyWeight(font, bold);
  skia::SkPaint p;
  p.setAntiAlias(true);
  p.setColor(colour);
  p.setAlphaf(alpha);
  client::ui::fonts().draw(canvas, font, s, x, y, p);
  client::ui::fonts().applyWeight(font, false);
}

inline void textClipped(skia::SkCanvas *canvas, skia::SkFont &font,
                        const std::string &s, float x, float y, float maxW,
                        float size, skia::SkColor colour, bool bold = false,
                        float alpha = 1.0f) {
  canvas->save();
  canvas->clipIRect(skia::SkIRect::MakeXYWH(
      static_cast<int>(x), static_cast<int>(y - size * 1.3f),
      static_cast<int>(maxW), static_cast<int>(size * 1.9f)));
  text(canvas, font, s, x, y, size, colour, bold, alpha);
  canvas->restore();
}

inline void textCentered(skia::SkCanvas *canvas, skia::SkFont &font,
                         const std::string &s, float cx, float y, float size,
                         skia::SkColor colour, bool bold = false,
                         float alpha = 1.0f) {
  text(canvas, font, s, cx - measure(font, s, size, bold) * 0.5f, y, size,
       colour, bold, alpha);
}

// Colour4.Lighten, as FilterTabItem applies on hover.
inline skia::SkColor lighten(skia::SkColor c, float amount) {
  const auto ch = [amount](std::uint32_t v) {
    return static_cast<std::uint8_t>(
        std::min(255.0f, static_cast<float>(v) * (1.0f + amount)));
  };
  return skia::colorSetARGB(255, ch((c >> 16) & 0xffu), ch((c >> 8) & 0xffu),
                            ch(c & 0xffu));
}

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

} // namespace paint

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
  void setPreview(long setId, float progress) {
    fPreviewId = setId;
    fPreviewProgress = progress;
  }

  [[nodiscard]] Filters &filters() { return fFilters; }
  [[nodiscard]] const Filters &filters() const { return fFilters; }
  [[nodiscard]] std::span<const int> visible() const { return fVisible; }

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

  void tick(double nowMs) { fBlink = nowMs; }

  void draw(const Ctx &ctx) {
    fFont = ctx.fFont;
    fMouseX = ctx.fMouseX;
    fMouseY = ctx.fMouseY;
    fEntries = ctx.fEntries;
    fLoading = ctx.fLoading;
    this->rebuildVisible(ctx.fEntries);

    // The tree is rebuilt when what it shows changes, and reused otherwise.
    const std::string shape = this->treeShape(ctx);
    if (!fScene || shape != fShape) {
      fShape = shape;
      fScene = this->build();
    }

    const skia::SkRect screen = skia::SkRect::MakeWH(ctx.fWidth, ctx.fHeight);
    fScene->updateTree(ctx.fNowMs);
    if (fScrollToStart && fScroll != nullptr) {
      fScroll->scrollToStart();
      fScrollToStart = false;
    }
    if (fScrollTicks != 0.0f && fScroll != nullptr) {
      fScene->scroll(ctx.fMouseX, ctx.fMouseY, fScrollTicks);
      fScrollTicks = 0.0f;
    }
    fScene->layoutIfNeeded(screen);
    fScene->setHover(ctx.fMouseX, ctx.fMouseY);
    fScene->draw(ctx.fCanvas);
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

private:
  // What a clickable filter stands for. Rows and the sort bar report their
  // hits in these terms, and the owner turns them into state changes.
  enum class Kind : std::uint8_t {
    kGeneral, kRuleset, kCategory, kGenre, kLanguage, kExtra, kRank, kPlayed,
    kExplicit, kSort, kCardSize
  };

  // ---- nodes ---------------------------------------------------------------

  // BeatmapSearchTextBox: OsuTextBox is 40 high with a 5px corner radius.
  class SearchBoxNode : public scene::Drawable {
  public:
    explicit SearchBoxNode(Listing *owner) : fOwner(owner) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
      fHeight = 40.0f;
    }

  protected:
    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      auto &font = *fOwner->fFont;
      paint::rounded(canvas, fBounds, 5.0f, kBackground4, alpha);
      skia::SkPaint icon;
      icon.setAntiAlias(true);
      icon.setStyle(skia::kStrokeStyle);
      icon.setStrokeWidth(1.8f);
      icon.setColor(kLight1);
      icon.setAlphaf(alpha);
      const float ix = fBounds.fLeft + 18.0f;
      const float iy = fBounds.centerY();
      canvas->drawCircle(ix, iy - 1.0f, 5.5f, icon);
      canvas->drawLine(ix + 4.0f, iy + 3.0f, ix + 8.0f, iy + 7.0f, icon);

      const auto &query = fOwner->fFilters.fQuery;
      if (query.empty()) {
        paint::text(canvas, font, "type in keywords...", fBounds.fLeft + 32.0f,
                    fBounds.centerY() + 6.0f, 16.0f, kLight3, false,
                    alpha * 0.6f);
      } else {
        paint::textClipped(canvas, font, query, fBounds.fLeft + 32.0f,
                           fBounds.centerY() + 6.0f, fBounds.width() - 44.0f,
                           16.0f, kContent1, false, alpha);
      }
      if (std::fmod(fOwner->fBlink, 1000.0) < 600.0) {
        const float cx = fBounds.fLeft + 32.0f +
                         paint::measure(font, query, 16.0f, false) + 2.0f;
        paint::rect(canvas,
                    skia::SkRect::MakeXYWH(cx, fBounds.centerY() - 9.0f, 1.5f,
                                           18.0f),
                    kContent1, alpha * 0.8f);
      }
      fOwner->fTextBoxBounds = fBounds;
    }

  private:
    Listing *fOwner;
  };

  // BeatmapSearchFilterRow: a 100px label column beside a wrapping tab flow.
  class FilterRowNode : public scene::Drawable {
  public:
    FilterRowNode(Listing *owner, const char *header,
                  std::span<const char *const> labels, Kind kind)
        : fOwner(owner), fHeader(header), fLabels(labels), fKind(kind) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
    }

  protected:
    // The height depends on how the tabs wrap, which depends on the width
    // this row is being given -- so it is worked out here, where that is
    // known, and the positions are kept for drawing and for hit testing.
    void measure(const skia::SkRect &parent) override {
      auto &font = *fOwner->fFont;
      const float width = parent.width();
      const float tabsX = kRowLabelWidth;
      float cx = tabsX;
      float cy = 0.0f;
      fTabs.clear();
      for (std::size_t i = 0; i < fLabels.size(); ++i) {
        const std::string label = fLabels[i];
        // Measured bold either way, so toggling a filter does not shuffle
        // the row around.
        const float w = paint::measure(font, label, kFilterFontSize, true);
        if (cx > tabsX && cx + w > width) {
          cx = tabsX;
          cy += kFilterLineHeight;
        }
        fTabs.push_back({skia::SkRect::MakeXYWH(cx, cy, w, kFilterLineHeight),
                         static_cast<int>(i)});
        cx += w + kTabSpacing;
      }
      fHeight = cy + kFilterLineHeight;
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      auto &font = *fOwner->fFont;
      paint::text(canvas, font, fHeader, fBounds.fLeft,
                  fBounds.fTop + kFilterFontSize, kFilterFontSize, kContent2,
                  false, alpha);
      for (std::size_t i = 0; i < fTabs.size(); ++i) {
        const bool active = fOwner->filterActive(fKind, fTabs[i].fValue);
        const skia::SkRect box = this->tabBounds(i);
        const bool hovered = box.contains(fOwner->fMouseX, fOwner->fMouseY);
        skia::SkColor colour = active ? kContent1 : kLight2;
        if (hovered) {
          colour = paint::lighten(colour, 0.2f);
        }
        paint::text(canvas, font, fLabels[fTabs[i].fValue], box.fLeft,
                    box.fTop + kFilterFontSize, kFilterFontSize, colour,
                    active, alpha);
      }
    }

    bool acceptsInput() const override { return true; }

    bool onClick(float x, float y) override {
      for (std::size_t i = 0; i < fTabs.size(); ++i) {
        if (this->tabBounds(i).contains(x, y)) {
          fOwner->activateFilter(fKind, fTabs[i].fValue);
          return true;
        }
      }
      return false;
    }

  private:
    [[nodiscard]] skia::SkRect tabBounds(std::size_t i) const {
      const skia::SkRect &local = fTabs[i].fRect;
      return skia::SkRect::MakeXYWH(fBounds.fLeft + local.fLeft,
                                    fBounds.fTop + local.fTop, local.width(),
                                    local.height());
    }

    struct Tab {
      skia::SkRect fRect; // relative to the row
      int fValue;
    };
    Listing *fOwner;
    const char *fHeader;
    std::span<const char *const> fLabels;
    Kind fKind;
    std::vector<Tab> fTabs;
  };

  // The 40px sort bar: criteria on the left, card size on the right.
  class SortBarNode : public scene::Drawable {
  public:
    explicit SortBarNode(Listing *owner) : fOwner(owner) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
      fHeight = kSortBarHeight;
    }

  protected:
    void measure(const skia::SkRect &parent) override {
      auto &font = *fOwner->fFont;
      fSorts.clear();
      float x = 20.0f;
      for (int i = 0; i < static_cast<int>(std::size(kSortLabels)); ++i) {
        const auto sort = static_cast<Sort>(i);
        if (!fOwner->sortAvailable(sort)) {
          continue;
        }
        const float w = paint::measure(font, kSortLabels[i], kFilterFontSize,
                                       true);
        fSorts.push_back({skia::SkRect::MakeXYWH(x, 0.0f, w, kSortBarHeight),
                          i});
        x += w + kTabSpacing * 1.5f +
             (static_cast<int>(fOwner->fFilters.fSort) == i ? 13.0f : 0.0f);
      }
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

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      auto &font = *fOwner->fFont;
      paint::rect(canvas, fBounds, kBackground4, alpha);
      const float baseline = fBounds.fTop + kSortBarHeight * 0.5f + 5.0f;
      for (const auto &item : fSorts) {
        const bool active =
            static_cast<int>(fOwner->fFilters.fSort) == item.fValue;
        const skia::SkRect box = this->localToScreen(item.fRect);
        const bool hovered = box.contains(fOwner->fMouseX, fOwner->fMouseY);
        skia::SkColor colour = active ? kContent1 : kLight2;
        if (hovered) {
          colour = paint::lighten(colour, 0.2f);
        }
        paint::text(canvas, font, kSortLabels[item.fValue], box.fLeft,
                    baseline, kFilterFontSize, colour, active, alpha);
        if (!active) {
          continue;
        }
        // The active criterion carries the direction chevron.
        skia::SkPaint arrow;
        arrow.setAntiAlias(true);
        arrow.setColor(kContent1);
        arrow.setAlphaf(alpha);
        skia::SkPathBuilder pb;
        const float ax = box.fRight + 5.0f;
        const float ay = baseline - 4.0f;
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

      // BeatmapListingCardSizeTabControl: two icons, 10 apart, 20 from the
      // right edge.
      for (const auto &item : fCardSizes) {
        const skia::SkRect box = this->localToScreen(item.fRect);
        const bool active =
            static_cast<int>(fOwner->fFilters.fCardSize) == item.fValue;
        const bool hovered = box.contains(fOwner->fMouseX, fOwner->fMouseY);
        skia::SkPaint p;
        p.setAntiAlias(true);
        p.setStyle(skia::kStrokeStyle);
        p.setStrokeWidth(1.6f);
        p.setColor(active ? kContent1 : (hovered ? kLight1 : kLight3));
        p.setAlphaf(alpha);
        const float ix = box.fLeft + 2.0f;
        const float iy = box.centerY();
        if (item.fValue == 0) { // normal: two stacked bars
          canvas->drawRect(
              skia::SkRect::MakeXYWH(ix, iy - 7.0f, 14.0f, 5.0f), p);
          canvas->drawRect(
              skia::SkRect::MakeXYWH(ix, iy + 2.0f, 14.0f, 5.0f), p);
        } else { // extra: one taller bar
          canvas->drawRect(
              skia::SkRect::MakeXYWH(ix, iy - 7.0f, 14.0f, 14.0f), p);
        }
      }
    }

    bool acceptsInput() const override { return true; }

    bool onClick(float x, float y) override {
      for (const auto &item : fSorts) {
        if (this->localToScreen(item.fRect).contains(x, y)) {
          fOwner->activateFilter(Kind::kSort, item.fValue);
          return true;
        }
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
    std::vector<Item> fSorts;
    std::vector<Item> fCardSizes;
  };

  // BeatmapCardNormal, and its taller sibling. One node per card: it knows
  // its own hover and expansion, which is what the parallel arrays indexed by
  // entry used to be for, and those went stale whenever the list reordered.
  class CardNode : public scene::Drawable {
  public:
    CardNode(Listing *owner, int entry) : fOwner(owner), fEntry(entry) {
      fWidth = kCardWidth;
    }

  protected:
    void measure(const skia::SkRect &) override {
      const Entry &e = fOwner->fEntries[static_cast<std::size_t>(fEntry)];
      const float cardH = fOwner->fFilters.fCardSize == CardSize::kNormal
                              ? kCardNormalHeight
                              : kCardExtraHeight;
      const float full =
          std::min(kExpandedMaxHeight,
                   static_cast<float>(e.fDiffs.size()) * 20.0f + 20.0f);
      fCardHeight = cardH;
      fHeight = cardH + full * client::ui::outQuint(fExpanded);
    }

    void update(double nowMs) override {
      const double dt = fLastMs > 0.0 ? nowMs - fLastMs : 16.0;
      fLastMs = nowMs;
      const Entry &e = fOwner->fEntries[static_cast<std::size_t>(fEntry)];
      const bool hovered = fHovered;
      const float previousExpand = fExpand;
      const float previousExpanded = fExpanded;
      fExpand = client::ui::approach(fExpand, hovered ? 1.0f : 0.0f,
                                     kTransitionMs / 6.0f, dt);
      // Hovering the bottom of the card opens it after a moment, as
      // BeatmapCardContent.ExpandAfterDelay does.
      const bool overInfo = hovered && !e.fDiffs.empty() &&
                            fOwner->fMouseY > fBounds.fTop + fCardHeight -
                                                  22.0f &&
                            fOwner->fMouseY < fBounds.fTop + fCardHeight;
      fHoverMs = overInfo ? fHoverMs + dt : 0.0;
      const bool wantExpanded =
          fHoverMs > kExpandDelayMs || (fExpanded > 0.5f && hovered);
      fExpanded = client::ui::approach(fExpanded, wantExpanded ? 1.0f : 0.0f,
                                       kTransitionMs / 5.0f, dt);
      if (fExpand != previousExpand || fExpanded != previousExpanded) {
        this->invalidateLayout();
      }
    }

    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      Entry &e = fOwner->fEntries[static_cast<std::size_t>(fEntry)];
      auto &font = *fOwner->fFont;
      const bool extra = fOwner->fFilters.fCardSize == CardSize::kExtra;
      const skia::SkRect card = skia::SkRect::MakeXYWH(
          fBounds.fLeft, fBounds.fTop, kCardWidth, fCardHeight);
      const float h = card.height();
      const float buttonsW = kButtonsCollapsed +
                             (kButtonsExpanded - kButtonsCollapsed) * fExpand;

      paint::rounded(canvas, card, kCardCorner, kBackground2, alpha);
      const skia::SkRect main = skia::SkRect::MakeLTRB(
          card.fLeft, card.fTop, card.fRight - buttonsW, card.fBottom);

      // Cover art: a square of the card's height on the left, and the same
      // image dimmed behind the text.
      const skia::SkRect thumb =
          skia::SkRect::MakeXYWH(card.fLeft, card.fTop, h, h);
      canvas->save();
      canvas->clipRRect(
          skia::SkRRect::MakeRectXY(main, kCardCorner, kCardCorner), true);
      paint::rect(canvas, main, kBackground3, alpha);
      if (e.fThumbSt == Entry::Thumb::kReady && e.fThumb) {
        paint::imageFilled(
            canvas, e.fThumb.get(),
            skia::SkRect::MakeXYWH(card.fLeft + h - kCardCorner, card.fTop,
                                   main.width() - h + kCardCorner, h));
        paint::rect(canvas,
                    skia::SkRect::MakeLTRB(card.fLeft + h - kCardCorner,
                                           card.fTop, main.fRight,
                                           card.fBottom),
                    kBackground6, alpha * (fHovered ? 0.9f : 0.8f));
        paint::imageFilled(canvas, e.fThumb.get(), thumb);
      }
      canvas->restore();

      // Main content, inset 10 horizontal and 4 vertical.
      const float tx = card.fLeft + h + 10.0f - kCardCorner;
      const float tw = main.fRight - tx - 10.0f;
      float ty = card.fTop + 4.0f + 18.0f;
      paint::textClipped(canvas, font,
                         e.fTitleUnicode.empty() ? e.fTitle : e.fTitleUnicode,
                         tx, ty, tw, 18.0f, kContent1, true, alpha);
      ty += 17.0f;
      paint::textClipped(canvas, font,
                         "by " + (e.fArtistUnicode.empty() ? e.fArtist
                                                           : e.fArtistUnicode),
                         tx, ty, tw, 14.0f, kContent1, true, alpha);
      if (extra) {
        ty += 14.0f;
        paint::textClipped(canvas, font, e.fSource, tx, ty, tw, 11.0f,
                           kContent2, true, alpha);
      } else {
        ty += 14.0f;
        const std::string mapped = "mapped by ";
        paint::text(canvas, font, mapped, tx, ty, 11.0f, kContent2, true,
                    alpha);
        paint::textClipped(canvas, font, e.fCreator,
                           tx + paint::measure(font, mapped, 11.0f, true), ty,
                           tw, 11.0f, kContent1, true, alpha);
      }

      const float bottom = card.fBottom - 6.0f;
      if (e.fSt == Entry::St::kFetching) {
        // BeatmapCardDownloadProgressBar: 5 high, across the bottom content.
        const skia::SkRect bar =
            skia::SkRect::MakeXYWH(tx, card.fBottom - 9.0f, tw, 5.0f);
        paint::rounded(canvas, bar, 2.5f, kBackground6, alpha);
        paint::rounded(canvas,
                       skia::SkRect::MakeXYWH(bar.fLeft, bar.fTop,
                                              bar.width() * e.fProgress, 5.0f),
                       2.5f, kColour1, alpha);
      } else if (extra) {
        const std::string mapped = "mapped by ";
        paint::text(canvas, font, mapped, tx, bottom - 34.0f, 11.0f, kContent2,
                    true, alpha);
        paint::textClipped(canvas, font, e.fCreator,
                           tx + paint::measure(font, mapped, 11.0f, true),
                           bottom - 34.0f, tw, 11.0f, kContent1, true, alpha);
        this->drawStatistics(canvas, font, tx, bottom - 20.0f, tw, e, alpha);
        this->drawExtraInfoRow(canvas, font, tx, bottom, tw, e, alpha);
      } else {
        if (fExpand > 0.01f) {
          this->drawStatistics(canvas, font, tx, bottom - 15.0f, tw, e,
                               alpha * fExpand);
        }
        this->drawExtraInfoRow(canvas, font, tx, bottom, tw, e, alpha);
      }

      this->drawThumbnailPlay(canvas, thumb, e, alpha);
      this->drawButtons(canvas, card, main, e, alpha);
      if (fExpanded > 0.01f) {
        this->drawExpanded(canvas, font, card, e, alpha);
      }
    }

    bool acceptsInput() const override { return true; }

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
      paint::textClipped(
          canvas, font,
          std::format("{} plays    {} favourites    {}", e.fPlayCount,
                      e.fFavouriteCount, e.fUpdated),
          x, baseline, maxW, 11.0f, kContent2, false, alpha);
    }

    // BeatmapCardExtraInfoRow: the status pill and the difficulty spectrum.
    void drawExtraInfoRow(skia::SkCanvas *canvas, skia::SkFont &font, float x,
                          float baseline, float maxW, const Entry &e,
                          float alpha) {
      const std::string status = e.fStatus.empty() ? "unknown" : e.fStatus;
      const float pillW = paint::measure(font, status, 11.0f, true) + 12.0f;
      const skia::SkRect pill =
          skia::SkRect::MakeXYWH(x, baseline - 11.0f, pillW, 15.0f);
      paint::rounded(canvas, pill, 7.5f, paint::statusColour(status), alpha);
      paint::text(canvas, font, status, x + 6.0f, baseline - 1.0f, 11.0f,
                  skia::colorSetARGB(255, 20, 24, 26), true, alpha);

      // DifficultySpectrumDisplay: 5x10 dots, 1px apart, in star order.
      float dx = x + pillW + 8.0f;
      for (const auto &diff : e.fDiffs) {
        if (dx + 6.0f > x + maxW) {
          break;
        }
        paint::rounded(canvas,
                       skia::SkRect::MakeXYWH(dx, baseline - 10.0f, 5.0f,
                                              10.0f),
                       1.0f, client::ui::starColor(diff.fStars), alpha);
        dx += 6.0f;
      }
    }

    // BeatmapCardThumbnail's PlayButton, with its CircularProgress.
    void drawThumbnailPlay(skia::SkCanvas *canvas, const skia::SkRect &thumb,
                           const Entry &e, float alpha) {
      const bool playing = fOwner->fPreviewId == e.fSetId;
      if (!fHovered && !playing) {
        return;
      }
      paint::rect(canvas, thumb, kBackground6, alpha * 0.6f);
      skia::SkPaint p;
      p.setAntiAlias(true);
      p.setColor(kContent1);
      p.setAlphaf(alpha);
      const float cx = thumb.centerX();
      const float cy = thumb.centerY();
      if (!playing) {
        skia::SkPathBuilder tri;
        tri.moveTo(cx - 6.0f, cy - 9.0f)
            .lineTo(cx + 9.0f, cy)
            .lineTo(cx - 6.0f, cy + 9.0f)
            .close();
        canvas->drawPath(tri.detach(), p);
        return;
      }
      paint::rect(canvas,
                  skia::SkRect::MakeXYWH(cx - 6.0f, cy - 8.0f, 4.0f, 16.0f),
                  kContent1, alpha);
      paint::rect(canvas,
                  skia::SkRect::MakeXYWH(cx + 2.0f, cy - 8.0f, 4.0f, 16.0f),
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
      const skia::SkRect buttons = skia::SkRect::MakeLTRB(
          main.fRight, card.fTop, card.fRight, card.fBottom);
      paint::rounded(canvas, buttons, kCardCorner, kBackground3, alpha);
      paint::rect(canvas,
                  skia::SkRect::MakeXYWH(buttons.fLeft - kCardCorner,
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
      const float rowH = 20.0f;
      const float height = fBounds.fBottom - card.fBottom;
      const skia::SkRect panel = skia::SkRect::MakeXYWH(
          card.fLeft, card.fBottom - kCardCorner, card.width(),
          height + kCardCorner);
      paint::rounded(canvas, panel, kCardCorner, kBackground4,
                     alpha * fExpanded);
      canvas->save();
      canvas->clipRect(panel);
      float y = card.fBottom + 10.0f; // Padding: horizontal 8, vertical 10
      for (const auto &diff : e.fDiffs) {
        if (y > panel.fBottom - 4.0f) {
          break;
        }
        const std::string stars = std::format("{:.2f}", diff.fStars);
        const float pillW = paint::measure(font, stars, 11.0f, true) + 16.0f;
        const skia::SkRect pill =
            skia::SkRect::MakeXYWH(card.fLeft + 8.0f, y - 12.0f, pillW, 16.0f);
        paint::rounded(canvas, pill, 8.0f, client::ui::starColor(diff.fStars),
                       alpha * fExpanded);
        paint::text(canvas, font, stars, pill.fLeft + 8.0f, y, 11.0f,
                    skia::colorSetARGB(255, 20, 24, 26), true,
                    alpha * fExpanded);
        paint::textClipped(canvas, font, diff.fVersion, pill.fRight + 6.0f, y,
                           card.width() - pillW - 24.0f, 14.0f, kContent1,
                           true, alpha * fExpanded);
        y += rowH;
      }
      canvas->restore();
    }

    Listing *fOwner;
    int fEntry;
    float fCardHeight = kCardNormalHeight;
    float fExpand = 0.0f;   // button column and statistics
    float fExpanded = 0.0f; // the difficulty list under the card
    double fHoverMs = 0.0;
    double fLastMs = 0.0;
  };

  // OverlayHeader with its title and description.
  class HeaderNode : public scene::Drawable {
  public:
    explicit HeaderNode(Listing *owner) : fOwner(owner) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
      fHeight = 55.0f;
    }

  protected:
    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      auto &font = *fOwner->fFont;
      paint::rect(canvas, fBounds, kBackground5, alpha);
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
      paint::text(canvas, font, title, titleX, fBounds.centerY() + 7.0f, 20.0f,
                  kContent1, false, alpha);
      paint::text(canvas, font, "browse for new beatmaps",
                  titleX + paint::measure(font, title, 20.0f, false) + 12.0f,
                  fBounds.centerY() + 6.0f, 14.0f, kContent2, false,
                  alpha * 0.8f);
    }

  private:
    Listing *fOwner;
  };

  // NotFoundDrawable: 250 high, its text centred.
  class EmptyNode : public scene::Drawable {
  public:
    explicit EmptyNode(Listing *owner) : fOwner(owner) {
      fRelativeSizeAxes = scene::Axes::kX;
      fWidth = 1.0f;
      fHeight = 250.0f;
    }

  protected:
    void drawSelf(skia::SkCanvas *canvas, float alpha) override {
      paint::textCentered(canvas, *fOwner->fFont,
                          fOwner->fLoading
                              ? "searching..."
                              : "no beatmaps match your criteria!",
                          fBounds.centerX(), fBounds.centerY(), 16.0f,
                          kContent2, false, alpha);
    }

  private:
    Listing *fOwner;
  };

  // ---- the tree -----------------------------------------------------------

  [[nodiscard]] std::unique_ptr<scene::Drawable> build() {
    fScroll = nullptr;

    auto root = std::make_unique<nodes::Box>(kBackground6);
    root->fRelativeSizeAxes = scene::Axes::kBoth;
    root->fWidth = 1.0f;
    root->fHeight = 1.0f;

    auto scroll = std::make_unique<nodes::ScrollContainer>();
    scroll->fRelativeSizeAxes = scene::Axes::kBoth;
    scroll->fWidth = 1.0f;
    scroll->fHeight = 1.0f;
    fScroll = scroll.get();

    auto column =
        std::make_unique<nodes::FillFlow>(nodes::FillFlow::Direction::kVertical);
    column->fRelativeSizeAxes = scene::Axes::kX;
    column->fWidth = 1.0f;
    column->fAutoSizeAxes = scene::Axes::kY;

    column->add(std::make_unique<HeaderNode>(this));

    // BeatmapListingSearchControl over Dark6: padded 20 vertical and
    // HORIZONTAL_PADDING horizontal, contents 20 apart.
    auto panel = std::make_unique<nodes::Box>(kDark6);
    panel->fRelativeSizeAxes = scene::Axes::kX;
    panel->fWidth = 1.0f;
    panel->fAutoSizeAxes = scene::Axes::kY;
    panel->fPadding = {20.0f, kHorizontalPadding, 20.0f, kHorizontalPadding};

    auto panelColumn =
        std::make_unique<nodes::FillFlow>(nodes::FillFlow::Direction::kVertical);
    panelColumn->fRelativeSizeAxes = scene::Axes::kX;
    panelColumn->fWidth = 1.0f;
    panelColumn->fAutoSizeAxes = scene::Axes::kY;
    panelColumn->setSpacing(0.0f, 20.0f);
    panelColumn->add(std::make_unique<SearchBoxNode>(this));

    // The filter rows, indented 10 and spaced 5, in lazer's order.
    auto rows =
        std::make_unique<nodes::FillFlow>(nodes::FillFlow::Direction::kVertical);
    rows->fRelativeSizeAxes = scene::Axes::kX;
    rows->fWidth = 1.0f;
    rows->fAutoSizeAxes = scene::Axes::kY;
    rows->fPadding = {0.0f, 10.0f, 0.0f, 10.0f};
    rows->setSpacing(0.0f, kRowSpacing);
    rows->add(std::make_unique<FilterRowNode>(this, "General", kGeneralLabels,
                                              Kind::kGeneral));
    rows->add(std::make_unique<FilterRowNode>(this, "Mode", kRulesetLabels,
                                              Kind::kRuleset));
    rows->add(std::make_unique<FilterRowNode>(this, "Categories",
                                              kCategoryLabels,
                                              Kind::kCategory));
    rows->add(std::make_unique<FilterRowNode>(this, "Genre", kGenreLabels,
                                              Kind::kGenre));
    rows->add(std::make_unique<FilterRowNode>(this, "Language",
                                              kLanguageLabels,
                                              Kind::kLanguage));
    rows->add(std::make_unique<FilterRowNode>(this, "Extra", kExtraLabels,
                                              Kind::kExtra));
    rows->add(std::make_unique<FilterRowNode>(this, "Rank Achieved",
                                              kRankLabels, Kind::kRank));
    rows->add(std::make_unique<FilterRowNode>(this, "Played", kPlayedLabels,
                                              Kind::kPlayed));
    rows->add(std::make_unique<FilterRowNode>(this, "Explicit Content",
                                              kExplicitLabels,
                                              Kind::kExplicit));
    panelColumn->add(std::move(rows));
    panel->add(std::move(panelColumn));
    column->add(std::move(panel));

    column->add(std::make_unique<SortBarNode>(this));

    // The cards, in panelTarget's 20px padding: as many per row as fit, 10
    // apart, rows centred -- which is the flow's job, not arithmetic here.
    if (fVisible.empty()) {
      column->add(std::make_unique<EmptyNode>(this));
    } else {
      auto grid = std::make_unique<nodes::FillFlow>(
          nodes::FillFlow::Direction::kHorizontal);
      grid->fRelativeSizeAxes = scene::Axes::kX;
      grid->fWidth = 1.0f;
      grid->fAutoSizeAxes = scene::Axes::kY;
      grid->fPadding = {15.0f, kPanelPadding, 20.0f, kPanelPadding};
      grid->setSpacing(kCardSpacing, kCardSpacing);
      grid->fCentreRows = true;
      for (const int idx : fVisible) {
        grid->add(std::make_unique<CardNode>(this, idx));
      }
      column->add(std::move(grid));
    }

    scroll->add(std::move(column));
    root->add(std::move(scroll));
    return root;
  }

  // What the tree is built from: rebuilt when any of this changes, reused
  // when none of it does.
  [[nodiscard]] std::string treeShape(const Ctx &ctx) const {
    std::string shape = std::format(
        "{}x{}|{}|{}|{}|{}|", static_cast<int>(ctx.fWidth),
        static_cast<int>(ctx.fHeight), static_cast<int>(fFilters.fCardSize),
        static_cast<int>(fFilters.fSort), fFilters.fDescending ? 1 : 0,
        fVisible.size());
    for (const int idx : fVisible) {
      shape += std::to_string(ctx.fEntries[static_cast<std::size_t>(idx)].fSetId);
      shape.push_back(',');
    }
    return shape;
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
  double fBlink = 0.0;
  long fPreviewId = -1;
  float fPreviewProgress = 0.0f;
  skia::SkRect fTextBoxBounds = skia::SkRect::MakeEmpty();

  // The tree, and what it was built for.
  std::unique_ptr<scene::Drawable> fScene;
  std::string fShape;
  nodes::ScrollContainer *fScroll = nullptr; // owned by the tree
  float fScrollTicks = 0.0f;
  bool fScrollToStart = false;
  float fScrollCurrent = 0.0f;
  float fScrollExtent = 0.0f;
  Result fPending;
};

} // namespace client::listing
