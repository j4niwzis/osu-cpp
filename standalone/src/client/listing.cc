export module client.listing;

import std;
import skia;
import client.ui;

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

class Listing {
public:
  struct Ctx {
    skia::SkCanvas *fCanvas = nullptr;
    skia::SkFont *fFont = nullptr;
    float fWidth = 0.0f, fHeight = 0.0f;
    float fMouseX = 0.0f, fMouseY = 0.0f;
    double fNowMs = 0.0;
    double fDtMs = 16.0;
    std::span<Entry> fEntries;
    bool fLoading = false;
  };
  enum class Action { kNone, kSearch, kRefilter, kDownload, kOpen, kPreview };
  struct Result {
    Action fAction = Action::kNone;
    std::size_t fIndex = 0; // entry index for kDownload
  };

  // Which set the preview is playing, and how far through it is.
  void setPreview(long setId, float progress) {
    fPreviewId = setId;
    fPreviewProgress = progress;
  }

  [[nodiscard]] Filters &filters() { return fFilters; }
  [[nodiscard]] const Filters &filters() const { return fFilters; }
  // Indices of fEntries that survive the client-side filters, in sort order.
  [[nodiscard]] std::span<const int> visible() const { return fVisible; }

  void scroll(float ticks) {
    fScrollTarget -= ticks * 60.0f;
    fScrollTarget = std::max(0.0f, std::min(fScrollTarget, fMaxScroll));
  }
  void scrollToStart() {
    fScrollTarget = 0.0f;
    fScroll = 0.0f;
  }

  // The listing pages in as it is scrolled, the way the overlay's scroll
  // container asks for the next cursor near the bottom. Also true when the
  // client-side filters left too little on screen to fill it.
  [[nodiscard]] bool wantsMore() const {
    return fMaxScroll > 0.0f
               ? fScrollTarget > fMaxScroll - 600.0f
               : fVisible.size() < 12;
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

  void draw(const Ctx &ctx) {
    fCanvas = ctx.fCanvas;
    fFont = ctx.fFont;
    fMouseX = ctx.fMouseX;
    fMouseY = ctx.fMouseY;
    fHits.clear();

    const float w = ctx.fWidth;
    this->rebuildVisible(ctx.fEntries);

    fScroll = client::ui::approach(fScroll, fScrollTarget, 30.0f, ctx.fDtMs);
    this->rect(skia::SkRect::MakeXYWH(0, 0, w, ctx.fHeight), kBackground6);

    fCanvas->save();
    fCanvas->translate(0.0f, -fScroll);

    float y = this->drawHeader(w);
    y = this->drawSearchControl(w, y);
    y = this->drawSortBar(w, y);
    y = this->drawCards(w, y, ctx);

    fCanvas->restore();
    fMaxScroll = std::max(0.0f, y - ctx.fHeight + 40.0f);
    fScrollTarget = std::min(fScrollTarget, fMaxScroll);
  }

  // Hit testing runs against the rectangles the last frame recorded.
  [[nodiscard]] Result click(float x, float y) {
    for (const auto &hit : fHits) {
      if (!hit.fRect.contains(x, y + fScroll)) {
        continue;
      }
      return this->activate(hit);
    }
    return {};
  }

  [[nodiscard]] bool textBoxHit(float x, float y) const {
    return fTextBox.contains(x, y + fScroll);
  }

private:
  // What a recorded rectangle stands for.
  enum class Kind : std::uint8_t {
    kGeneral, kRuleset, kCategory, kGenre, kLanguage, kExtra, kRank, kPlayed,
    kExplicit, kSort, kCardSize, kCard, kDownload, kPreview
  };
  struct Hit {
    skia::SkRect fRect;
    Kind fKind;
    int fValue;
  };

  Result activate(const Hit &hit) {
    switch (hit.fKind) {
    case Kind::kGeneral:
      fFilters.fGeneral[static_cast<std::size_t>(hit.fValue)] =
          !fFilters.fGeneral[static_cast<std::size_t>(hit.fValue)];
      return {Action::kRefilter};
    case Kind::kRuleset:
      fFilters.fRuleset = hit.fValue;
      return {Action::kSearch}; // the mirror filters by ruleset
    case Kind::kCategory:
      fFilters.fCategory = static_cast<Category>(hit.fValue);
      return {Action::kSearch}; // and by status
    case Kind::kGenre:
      fFilters.fGenre = static_cast<Genre>(hit.fValue);
      return {Action::kRefilter};
    case Kind::kLanguage:
      fFilters.fLanguage = static_cast<Language>(hit.fValue);
      return {Action::kRefilter};
    case Kind::kExtra:
      fFilters.fExtra[static_cast<std::size_t>(hit.fValue)] =
          !fFilters.fExtra[static_cast<std::size_t>(hit.fValue)];
      return {Action::kRefilter};
    case Kind::kRank:
      fFilters.fRanks[static_cast<std::size_t>(hit.fValue)] =
          !fFilters.fRanks[static_cast<std::size_t>(hit.fValue)];
      return {Action::kRefilter};
    case Kind::kPlayed:
      fFilters.fPlayed = static_cast<Played>(hit.fValue);
      return {Action::kRefilter};
    case Kind::kExplicit:
      fFilters.fExplicit = static_cast<Explicit>(hit.fValue);
      return {Action::kRefilter};
    case Kind::kSort: {
      const auto sort = static_cast<Sort>(hit.fValue);
      // Clicking the active criterion flips the direction, as SortTabControl
      // does; picking another resets to descending.
      if (sort == fFilters.fSort) {
        fFilters.fDescending = !fFilters.fDescending;
      } else {
        fFilters.fSort = sort;
        fFilters.fDescending = true;
      }
      return {Action::kRefilter};
    }
    case Kind::kCardSize:
      fFilters.fCardSize = static_cast<CardSize>(hit.fValue);
      return {Action::kRefilter};
    case Kind::kCard:
      // Clicking a card opens its page, as it opens BeatmapSetOverlay.
      return {Action::kOpen, static_cast<std::size_t>(hit.fValue)};
    case Kind::kDownload:
      return {Action::kDownload, static_cast<std::size_t>(hit.fValue)};
    case Kind::kPreview:
      return {Action::kPreview, static_cast<std::size_t>(hit.fValue)};
    }
    return {};
  }

  // ---- drawing helpers ----------------------------------------------------
  void rect(const skia::SkRect &r, skia::SkColor color, float alpha = 1.0f) {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(alpha);
    fCanvas->drawRect(r, p);
  }
  void rounded(const skia::SkRect &r, float radius, skia::SkColor color,
               float alpha = 1.0f) {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(alpha);
    fCanvas->drawRRect(skia::SkRRect::MakeRectXY(r, radius, radius), p);
  }
  // FillMode.Fill: the source is cropped to the destination's aspect ratio
  // instead of being squashed into it, which is what the covers were doing.
  void imageFilled(const skia::SkImage *image, const skia::SkRect &dst) {
    const float iw = static_cast<float>(image->width());
    const float ih = static_cast<float>(image->height());
    if (iw <= 0.0f || ih <= 0.0f) {
      return;
    }
    const float scale = std::max(dst.width() / iw, dst.height() / ih);
    const float srcW = dst.width() / scale;
    const float srcH = dst.height() / scale;
    const skia::SkRect src = skia::SkRect::MakeXYWH(
        (iw - srcW) * 0.5f, (ih - srcH) * 0.5f, srcW, srcH);
    fCanvas->drawImageRect(image, src, dst,
                           skia::SkSamplingOptions(skia::SkFilterMode::kLinear),
                           nullptr,
                           skia::SkCanvas::kStrict_SrcRectConstraint);
  }

  [[nodiscard]] float measure(const std::string &s, float size, bool bold) {
    fFont->setSize(size);
    fFont->setEmbolden(bold);
    const float w =
        fFont->measureText(s.c_str(), s.size(), skia::SkTextEncoding::kUTF8);
    fFont->setEmbolden(false);
    return w;
  }
  void text(const std::string &s, float x, float y, float size,
            skia::SkColor color, bool bold = false, float alpha = 1.0f) {
    fFont->setSize(size);
    fFont->setEmbolden(bold);
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(alpha);
    fCanvas->drawString(s.c_str(), x, y, *fFont, p);
    fFont->setEmbolden(false);
  }
  void textClipped(const std::string &s, float x, float y, float maxW,
                   float size, skia::SkColor color, bool bold = false,
                   float alpha = 1.0f) {
    fCanvas->save();
    fCanvas->clipIRect(skia::SkIRect::MakeXYWH(
        static_cast<int>(x), static_cast<int>(y - size * 1.3f),
        static_cast<int>(maxW), static_cast<int>(size * 1.9f)));
    this->text(s, x, y, size, color, bold, alpha);
    fCanvas->restore();
  }

  // FilterTabItem: 13px, active is Content1 and bold, otherwise Light2, and
  // hovering lightens by 0.2.
  // Colour4.Lighten(amount), as FilterTabItem applies on hover.
  static skia::SkColor lighten(skia::SkColor c, float amount) {
    const auto ch = [amount](std::uint32_t v) {
      return static_cast<std::uint8_t>(
          std::min(255.0f, static_cast<float>(v) * (1.0f + amount)));
    };
    return skia::colorSetARGB(255, ch((c >> 16) & 0xffu), ch((c >> 8) & 0xffu),
                              ch(c & 0xffu));
  }

  float tabItem(const std::string &label, float x, float y, bool active,
                Kind kind, int value, bool emit) {
    // Both weights are measured at the bold width so that toggling a filter
    // does not shuffle the row around.
    const float w = this->measure(label, kFilterFontSize, true);
    if (!emit) {
      return w;
    }
    const skia::SkRect box =
        skia::SkRect::MakeXYWH(x, y - kFilterFontSize, w, kFilterLineHeight);
    const bool hover = box.contains(fMouseX, fMouseY + fScroll);
    skia::SkColor colour = active ? kContent1 : kLight2;
    if (hover) {
      colour = lighten(colour, 0.2f);
    }
    this->text(label, x, y, kFilterFontSize, colour, active);
    fHits.push_back({box, kind, value});
    return w;
  }

  // One BeatmapSearchFilterRow: a 100px label column and a wrapping tab flow.
  float filterRow(const char *header, float x, float y, float width,
                  std::span<const char *const> labels, Kind kind,
                  const std::function<bool(int)> &isActive, bool emit) {
    if (emit) {
      this->text(header, x, y + kFilterFontSize, kFilterFontSize, kContent2);
    }
    const float tabsX = x + kRowLabelWidth;
    float cx = tabsX;
    float cy = y + kFilterFontSize;
    const float lineHeight = kFilterLineHeight;
    for (std::size_t i = 0; i < labels.size(); ++i) {
      const std::string label = labels[i];
      const float w = this->measure(label, kFilterFontSize, true);
      if (cx > tabsX && cx + w > x + width) {
        cx = tabsX;
        cy += lineHeight;
      }
      this->tabItem(label, cx, cy, isActive(static_cast<int>(i)), kind,
                    static_cast<int>(i), emit);
      cx += w + kTabSpacing;
    }
    // The height of the row, which is what the flow above adds up. (This used
    // to return the last baseline, an absolute coordinate, so each row pushed
    // the next one down by the whole panel offset.)
    return cy - y - kFilterFontSize + lineHeight;
  }

  // ---- sections -----------------------------------------------------------
  // OverlayHeader: the title bar above the filter control.
  float drawHeader(float w) {
    const float height = 55.0f;
    this->rect(skia::SkRect::MakeXYWH(0, 0, w, height), kBackground5);
    // OverlayTitle: a 30px icon, 10px of spacing, the 20px title, and the
    // description beside it in Content2.
    const float iconSize = 30.0f;
    const float x = kHorizontalPadding;
    skia::SkPaint icon;
    icon.setAntiAlias(true);
    icon.setColor(kContent2);
    icon.setStyle(skia::kStrokeStyle);
    icon.setStrokeWidth(2.5f);
    const float cx = x + iconSize * 0.5f;
    fCanvas->drawCircle(cx, height * 0.5f, iconSize * 0.36f, icon);
    fCanvas->drawCircle(cx, height * 0.5f, iconSize * 0.12f, icon);
    const std::string title = "beatmap listing";
    const float titleX = x + iconSize + 10.0f;
    this->text(title, titleX, height * 0.5f + 7.0f, 20.0f, kContent1);
    this->text("browse for new beatmaps",
               titleX + this->measure(title, 20.0f, false) + 12.0f,
               height * 0.5f + 6.0f, 14.0f, kContent2, false, 0.8f);
    return height;
  }

  // BeatmapListingSearchControl over Dark6, padded 20 vertical and
  // HORIZONTAL_PADDING horizontal, contents spaced 20 apart. The panel is
  // auto-sized in lazer, so its height is measured before the background can
  // be filled: the same layout runs twice, drawing only on the second pass.
  float drawSearchControl(float w, float top) {
    const float height = this->searchControl(w, top, /*emit=*/false);
    this->rect(skia::SkRect::MakeXYWH(0, top, w, height - top), kDark6);
    return this->searchControl(w, top, /*emit=*/true);
  }

  float searchControl(float w, float top, bool emit) {
    const float x = kHorizontalPadding;
    const float innerW = w - kHorizontalPadding * 2.0f;
    float y = top + 20.0f;

    // BeatmapSearchTextBox: OsuTextBox is 40 high with a 5px corner radius.
    const skia::SkRect box = skia::SkRect::MakeXYWH(x, y, innerW, 40.0f);
    if (emit) {
      fTextBox = box;
      this->drawTextBox(box);
    }
    y += 40.0f + 20.0f;

    // The filter rows are indented by 10 and spaced 5 apart.
    const float rowsX = x + 10.0f;
    const float rowsW = innerW - 20.0f;
    float rowY = y;
    const auto row = [&](const char *header,
                         std::span<const char *const> labels, Kind kind,
                         const std::function<bool(int)> &active) {
      rowY += this->filterRow(header, rowsX, rowY, rowsW, labels, kind, active,
                              emit) +
              kRowSpacing;
    };

    row("General", kGeneralLabels, Kind::kGeneral, [this](int i) {
      return fFilters.fGeneral[static_cast<std::size_t>(i)];
    });
    row("Mode", kRulesetLabels, Kind::kRuleset,
        [this](int i) { return fFilters.fRuleset == i; });
    row("Categories", kCategoryLabels, Kind::kCategory,
        [this](int i) { return static_cast<int>(fFilters.fCategory) == i; });
    row("Genre", kGenreLabels, Kind::kGenre,
        [this](int i) { return static_cast<int>(fFilters.fGenre) == i; });
    row("Language", kLanguageLabels, Kind::kLanguage,
        [this](int i) { return static_cast<int>(fFilters.fLanguage) == i; });
    row("Extra", kExtraLabels, Kind::kExtra, [this](int i) {
      return fFilters.fExtra[static_cast<std::size_t>(i)];
    });
    row("Rank Achieved", kRankLabels, Kind::kRank, [this](int i) {
      return fFilters.fRanks[static_cast<std::size_t>(i)];
    });
    row("Played", kPlayedLabels, Kind::kPlayed,
        [this](int i) { return static_cast<int>(fFilters.fPlayed) == i; });
    row("Explicit Content", kExplicitLabels, Kind::kExplicit,
        [this](int i) { return static_cast<int>(fFilters.fExplicit) == i; });

    return rowY - kRowSpacing + 20.0f;
  }

  void drawTextBox(const skia::SkRect &box) {
    this->rounded(box, 5.0f, kBackground4);
    skia::SkPaint icon;
    icon.setAntiAlias(true);
    icon.setStyle(skia::kStrokeStyle);
    icon.setStrokeWidth(1.8f);
    icon.setColor(kLight1);
    const float ix = box.fLeft + 18.0f;
    const float iy = box.centerY();
    fCanvas->drawCircle(ix, iy - 1.0f, 5.5f, icon);
    fCanvas->drawLine(ix + 4.0f, iy + 3.0f, ix + 8.0f, iy + 7.0f, icon);

    if (fFilters.fQuery.empty()) {
      this->text("type in keywords...", box.fLeft + 32.0f,
                 box.centerY() + 6.0f, 16.0f, kLight3, false, 0.6f);
    } else {
      this->textClipped(fFilters.fQuery, box.fLeft + 32.0f,
                        box.centerY() + 6.0f, box.width() - 44.0f, 16.0f,
                        kContent1);
    }
    if (std::fmod(fBlink, 1000.0) < 600.0) {
      const float cx = box.fLeft + 32.0f +
                       this->measure(fFilters.fQuery, 16.0f, false) + 2.0f;
      this->rect(skia::SkRect::MakeXYWH(cx, box.centerY() - 9.0f, 1.5f, 18.0f),
                 kContent1, 0.8f);
    }
  }

  // BeatmapListingSortTabControl.Reset: which criteria are on offer depends on
  // the category and on whether a query was typed.
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

  // The 40px sort bar: criteria on the left, card size on the right.
  float drawSortBar(float w, float top) {
    this->rect(skia::SkRect::MakeXYWH(0, top, w, kSortBarHeight),
               kBackground4);
    float x = 20.0f;
    const float baseline = top + kSortBarHeight * 0.5f + 5.0f;
    for (int i = 0; i < static_cast<int>(std::size(kSortLabels)); ++i) {
      if (!this->sortAvailable(static_cast<Sort>(i))) {
        continue;
      }
      const bool active = static_cast<int>(fFilters.fSort) == i;
      const std::string label = kSortLabels[i];
      const float tw =
          this->tabItem(label, x, baseline, active, Kind::kSort, i, true);
      if (active) {
        // The active criterion carries the direction chevron.
        skia::SkPaint arrow;
        arrow.setAntiAlias(true);
        arrow.setColor(kContent1);
        skia::SkPathBuilder pb;
        const float ax = x + tw + 5.0f;
        const float ay = baseline - 4.0f;
        if (fFilters.fDescending) {
          pb.moveTo(ax, ay - 3.0f).lineTo(ax + 8.0f, ay - 3.0f)
              .lineTo(ax + 4.0f, ay + 3.0f).close();
        } else {
          pb.moveTo(ax, ay + 3.0f).lineTo(ax + 8.0f, ay + 3.0f)
              .lineTo(ax + 4.0f, ay - 3.0f).close();
        }
        fCanvas->drawPath(pb.detach(), arrow);
        x += 13.0f;
      }
      x += tw + kTabSpacing * 1.5f;
    }

    // BeatmapListingCardSizeTabControl: two icons, 10 apart, 20 from the right.
    const float iconY = top + kSortBarHeight * 0.5f;
    for (int i = 0; i < 2; ++i) {
      const float ix = w - 20.0f - 14.0f - static_cast<float>(1 - i) * 24.0f;
      const skia::SkRect box =
          skia::SkRect::MakeXYWH(ix - 2.0f, iconY - 9.0f, 18.0f, 18.0f);
      const bool active = static_cast<int>(fFilters.fCardSize) == i;
      const bool hover = box.contains(fMouseX, fMouseY + fScroll);
      skia::SkPaint p;
      p.setAntiAlias(true);
      p.setStyle(skia::kStrokeStyle);
      p.setStrokeWidth(1.6f);
      p.setColor(active ? kContent1 : (hover ? kLight1 : kLight3));
      if (i == 0) { // normal: two stacked bars
        fCanvas->drawRect(skia::SkRect::MakeXYWH(ix, iconY - 7.0f, 14.0f, 5.0f), p);
        fCanvas->drawRect(skia::SkRect::MakeXYWH(ix, iconY + 2.0f, 14.0f, 5.0f), p);
      } else { // extra: one taller bar
        fCanvas->drawRect(skia::SkRect::MakeXYWH(ix, iconY - 7.0f, 14.0f, 14.0f), p);
      }
      fHits.push_back({box, Kind::kCardSize, i});
    }
    return top + kSortBarHeight;
  }

  // The cards flow left to right and wrap, inside panelTarget's 20px padding.
  float drawCards(float w, float top, const Ctx &ctx) {
    const float cardH = fFilters.fCardSize == CardSize::kNormal
                            ? kCardNormalHeight
                            : kCardExtraHeight;
    float y = top + 15.0f; // createCardContainerFor: Top = 15
    if (fVisible.empty()) {
      // NotFoundDrawable: 250 high, its text centred.
      const std::string text = ctx.fLoading ? "searching..."
                                            : "no beatmaps match your criteria!";
      this->text(text, w * 0.5f - this->measure(text, 16.0f, false) * 0.5f,
                 y + 125.0f, 16.0f, kContent2);
      return y + 250.0f;
    }

    const float available = w - kPanelPadding * 2.0f;
    const int columns = std::max(
        1, static_cast<int>((available + kCardSpacing) /
                            (kCardWidth + kCardSpacing)));
    const float rowWidth = static_cast<float>(columns) * kCardWidth +
                           static_cast<float>(columns - 1) * kCardSpacing;
    const float rowLeft = (w - rowWidth) * 0.5f;

    int column = 0;
    float rowExpansion = 0.0f; // the tallest expanded panel in this row
    for (const int idx : fVisible) {
      Entry &e = ctx.fEntries[static_cast<std::size_t>(idx)];
      const float x = rowLeft + static_cast<float>(column) *
                                    (kCardWidth + kCardSpacing);
      const skia::SkRect card =
          skia::SkRect::MakeXYWH(x, y, kCardWidth, cardH);
      if (y - fScroll < ctx.fHeight + cardH && y - fScroll + cardH > -cardH) {
        this->drawCard(card, e, idx, ctx);
      }
      fHits.push_back({card, Kind::kCard, idx});

      // An expanded card pushes the whole row down, not just itself.
      const auto &state = fCardState[static_cast<std::size_t>(idx)];
      if (state.fExpanded > 0.01f) {
        const float full =
            std::min(kExpandedMaxHeight,
                     static_cast<float>(e.fDiffs.size()) * 20.0f + 20.0f);
        rowExpansion = std::max(rowExpansion,
                                full * client::ui::outQuint(state.fExpanded));
      }

      if (++column == columns) {
        column = 0;
        y += cardH + kCardSpacing + rowExpansion;
        rowExpansion = 0.0f;
      }
    }
    if (column != 0) {
      y += cardH + kCardSpacing + rowExpansion;
    }
    return y + 20.0f;
  }

  void drawCard(const skia::SkRect &card, Entry &e, int index,
                const Ctx &ctx) {
    const bool extra = fFilters.fCardSize == CardSize::kExtra;
    const bool hover = card.contains(fMouseX, fMouseY + fScroll);
    const float h = card.height();
    auto &state = fCardState[static_cast<std::size_t>(index)];

    // The button column expands on hover, and hovering the bottom of the card
    // expands the whole card after a short delay.
    state.fExpand = client::ui::approach(state.fExpand, hover ? 1.0f : 0.0f,
                                         kTransitionMs / 6.0f, ctx.fDtMs);
    const bool overInfo =
        hover && fMouseY + fScroll > card.fBottom - 22.0f && !e.fDiffs.empty();
    state.fHoverMs = overInfo ? state.fHoverMs + ctx.fDtMs : 0.0;
    const bool wantExpanded = state.fHoverMs > kExpandDelayMs ||
                              (state.fExpanded > 0.5f && hover);
    state.fExpanded = client::ui::approach(state.fExpanded,
                                           wantExpanded ? 1.0f : 0.0f,
                                           kTransitionMs / 5.0f, ctx.fDtMs);

    const float buttonsW =
        kButtonsCollapsed + (kButtonsExpanded - kButtonsCollapsed) * state.fExpand;

    this->rounded(card, kCardCorner, kBackground2);
    const skia::SkRect main = skia::SkRect::MakeLTRB(
        card.fLeft, card.fTop, card.fRight - buttonsW, card.fBottom);

    // Thumbnail: a square of the card's height on the left, the cover art
    // doubling as the card's background behind the text.
    const skia::SkRect thumb =
        skia::SkRect::MakeXYWH(card.fLeft, card.fTop, h, h);
    fCanvas->save();
    fCanvas->clipRRect(
        skia::SkRRect::MakeRectXY(main, kCardCorner, kCardCorner), true);
    this->rect(main, kBackground3);
    if (e.fThumbSt == Entry::Thumb::kReady && e.fThumb) {
      this->imageFilled(
          e.fThumb.get(),
          skia::SkRect::MakeXYWH(card.fLeft + h - kCardCorner, card.fTop,
                                 main.width() - h + kCardCorner, h));
      this->rect(skia::SkRect::MakeLTRB(card.fLeft + h - kCardCorner,
                                        card.fTop, main.fRight, card.fBottom),
                 kBackground6, hover ? 0.9f : 0.8f);
      this->imageFilled(e.fThumb.get(), thumb);
    }
    fCanvas->restore();

    // Main content, inset 10 horizontal and 4 vertical inside the main area.
    const float tx = card.fLeft + h + 10.0f - kCardCorner;
    const float tw = main.fRight - tx - 10.0f;
    float ty = card.fTop + 4.0f + 18.0f;
    this->textClipped(e.fTitleUnicode.empty() ? e.fTitle : e.fTitleUnicode, tx,
                      ty, tw, 18.0f, kContent1, true);
    ty += 17.0f;
    this->textClipped("by " + (e.fArtistUnicode.empty() ? e.fArtist
                                                        : e.fArtistUnicode),
                      tx, ty, tw, 14.0f, kContent1, true);
    if (extra) {
      // The extra card carries the source line where the normal one has the
      // author, and moves "mapped by" into the bottom content.
      ty += 14.0f;
      this->textClipped(e.fSource, tx, ty, tw, 11.0f, kContent2, true);
    } else {
      ty += 14.0f;
      const std::string mapped = "mapped by ";
      this->text(mapped, tx, ty, 11.0f, kContent2, true);
      this->textClipped(e.fCreator, tx + this->measure(mapped, 11.0f, true),
                        ty, tw, 11.0f, kContent1, true);
    }

    const float bottom = card.fBottom - 6.0f;
    if (e.fSt == Entry::St::kFetching) {
      // BeatmapCardDownloadProgressBar: 5 high, across the bottom content.
      const skia::SkRect bar =
          skia::SkRect::MakeXYWH(tx, card.fBottom - 9.0f, tw, 5.0f);
      this->rounded(bar, 2.5f, kBackground6);
      this->rounded(skia::SkRect::MakeXYWH(bar.fLeft, bar.fTop,
                                           bar.width() * e.fProgress, 5.0f),
                    2.5f, kColour1);
    } else if (extra) {
      // "mapped by", then the statistics grid, then the extra info row.
      const std::string mapped = "mapped by ";
      this->text(mapped, tx, bottom - 34.0f, 11.0f, kContent2, true);
      this->textClipped(e.fCreator, tx + this->measure(mapped, 11.0f, true),
                        bottom - 34.0f, tw, 11.0f, kContent1, true);
      this->drawStatistics(tx, bottom - 20.0f, tw, e, 1.0f);
      this->drawExtraInfoRow(tx, bottom, tw, e);
    } else {
      // The normal card only shows its statistics while hovered.
      if (state.fExpand > 0.01f) {
        this->drawStatistics(tx, bottom - 15.0f, tw, e, state.fExpand);
      }
      this->drawExtraInfoRow(tx, bottom, tw, e);
    }

    this->drawThumbnailPlay(thumb, e, index, hover);
    this->drawCardButtons(card, main, state.fExpand, e, index);

    // The expanded content: the difficulty list, under the card.
    if (state.fExpanded > 0.01f) {
      this->drawExpandedContent(card, e, state.fExpanded);
    }
  }

  // BeatmapCardThumbnail's PlayButton: it fades in over the cover on hover
  // and while the preview is playing, with a CircularProgress around it.
  void drawThumbnailPlay(const skia::SkRect &thumb, const Entry &e, int index,
                         bool hover) {
    const bool playing = fPreviewId == e.fSetId;
    if (hover || playing) {
      this->rect(thumb, kBackground6, 0.6f);
      skia::SkPaint paint;
      paint.setAntiAlias(true);
      paint.setColor(kContent1);
      const float cx = thumb.centerX();
      const float cy = thumb.centerY();
      if (playing) {
        // Pause glyph while this preview is the one playing.
        this->rect(skia::SkRect::MakeXYWH(cx - 6.0f, cy - 8.0f, 4.0f, 16.0f),
                   kContent1);
        this->rect(skia::SkRect::MakeXYWH(cx + 2.0f, cy - 8.0f, 4.0f, 16.0f),
                   kContent1);
        skia::SkPaint ring;
        ring.setAntiAlias(true);
        ring.setStyle(skia::kStrokeStyle);
        ring.setStrokeWidth(3.0f);
        ring.setColor(kColour1);
        const float r = 18.0f;
        fCanvas->drawArc(
            skia::SkRect::MakeXYWH(cx - r, cy - r, r * 2.0f, r * 2.0f), -90.0f,
            360.0f * std::clamp(fPreviewProgress, 0.0f, 1.0f), false, ring);
      } else {
        skia::SkPathBuilder tri;
        tri.moveTo(cx - 6.0f, cy - 9.0f)
            .lineTo(cx + 9.0f, cy)
            .lineTo(cx - 6.0f, cy + 9.0f)
            .close();
        fCanvas->drawPath(tri.detach(), paint);
      }
    }
    fHits.push_back({thumb, Kind::kPreview, index});
  }

  void drawCardButtons(const skia::SkRect &card, const skia::SkRect &main,
                       float expand, const Entry &e, int index) {
    const skia::SkRect buttons = skia::SkRect::MakeLTRB(
        main.fRight, card.fTop, card.fRight, card.fBottom);
    this->rounded(buttons, kCardCorner, kBackground3);
    this->rect(skia::SkRect::MakeXYWH(buttons.fLeft - kCardCorner, buttons.fTop,
                                      kCardCorner, buttons.height()),
               kBackground3);
    if (expand > 0.4f) {
      skia::SkPaint stroke;
      stroke.setAntiAlias(true);
      stroke.setColor(kContent2);
      stroke.setAlphaf(expand);
      stroke.setStyle(skia::kStrokeStyle);
      stroke.setStrokeWidth(1.6f);
      const float cx = buttons.centerX();
      const float hy = card.fTop + card.height() * 0.25f;
      skia::SkPathBuilder heart;
      heart.moveTo(cx, hy + 4.0f)
          .cubicTo(cx - 8.0f, hy - 2.0f, cx - 3.0f, hy - 7.0f, cx, hy - 2.0f)
          .cubicTo(cx + 3.0f, hy - 7.0f, cx + 8.0f, hy - 2.0f, cx, hy + 4.0f);
      fCanvas->drawPath(heart.detach(), stroke);

      const float dy = card.fTop + card.height() * 0.75f;
      skia::SkPaint solid;
      solid.setAntiAlias(true);
      solid.setAlphaf(expand);
      solid.setColor(e.fSt == Entry::St::kDone ? kColour1 : kContent1);
      skia::SkPathBuilder arrow;
      arrow.moveTo(cx - 5.0f, dy - 1.0f)
          .lineTo(cx + 5.0f, dy - 1.0f)
          .lineTo(cx, dy + 5.0f)
          .close();
      fCanvas->drawPath(arrow.detach(), solid);
      fCanvas->drawLine(cx, dy - 7.0f, cx, dy - 1.0f, stroke);
    }
    fHits.push_back({buttons, Kind::kDownload, index});
  }

  // The statistics that fade in with a hover on the normal card and sit in a
  // grid on the extra one: play count, favourites, and the date.
  void drawStatistics(float x, float baseline, float maxW, const Entry &e,
                      float alpha) {
    const std::string stats =
        std::format("{} plays    {} favourites    {}", e.fPlayCount,
                    e.fFavouriteCount, e.fUpdated);
    this->textClipped(stats, x, baseline, maxW, 11.0f, kContent2, false, alpha);
  }

  // BeatmapCardDifficultyList inside the expanded content: one row per
  // difficulty, 3 apart, star rating beside the name.
  void drawExpandedContent(const skia::SkRect &card, const Entry &e,
                           float progress) {
    const float rowH = 20.0f;
    const float full = std::min(kExpandedMaxHeight,
                                static_cast<float>(e.fDiffs.size()) * rowH +
                                    20.0f);
    const float height = full * client::ui::outQuint(progress);
    const skia::SkRect panel = skia::SkRect::MakeXYWH(
        card.fLeft, card.fBottom - kCardCorner, card.width(),
        height + kCardCorner);
    this->rounded(panel, kCardCorner, kBackground4, progress);

    fCanvas->save();
    fCanvas->clipRect(panel);
    float y = card.fBottom + 10.0f; // Padding: horizontal 8, vertical 10
    for (const auto &diff : e.fDiffs) {
      if (y > panel.fBottom - 4.0f) {
        break;
      }
      // StarRatingDisplay (small) then the difficulty name at 14 semibold.
      const std::string stars = std::format("{:.2f}", diff.fStars);
      const float pillW = this->measure(stars, 11.0f, true) + 16.0f;
      const skia::SkRect pill =
          skia::SkRect::MakeXYWH(card.fLeft + 8.0f, y - 12.0f, pillW, 16.0f);
      this->rounded(pill, 8.0f, client::ui::starColor(diff.fStars), progress);
      this->text(stars, pill.fLeft + 8.0f, y, 11.0f,
                 skia::colorSetARGB(255, 20, 24, 26), true, progress);
      this->textClipped(diff.fVersion, pill.fRight + 6.0f, y,
                        card.width() - pillW - 24.0f, 14.0f, kContent1, true,
                        progress);
      y += rowH;
    }
    fCanvas->restore();
  }

  // BeatmapCardExtraInfoRow: the status pill and the difficulty spectrum.
  void drawExtraInfoRow(float x, float baseline, float maxW, const Entry &e) {
    // BeatmapSetOnlineStatusPill: 13px text, 4px horizontal padding.
    const std::string status = e.fStatus.empty() ? "unknown" : e.fStatus;
    const float pillW = this->measure(status, 11.0f, true) + 12.0f;
    const skia::SkRect pill =
        skia::SkRect::MakeXYWH(x, baseline - 11.0f, pillW, 15.0f);
    this->rounded(pill, 7.5f, statusColour(status));
    this->text(status, x + 6.0f, baseline - 1.0f, 11.0f,
               skia::colorSetARGB(255, 20, 24, 26), true);

    // DifficultySpectrumDisplay: 5x10 dots, 1px apart, in star order.
    float dx = x + pillW + 8.0f;
    for (const auto &diff : e.fDiffs) {
      if (dx + 6.0f > x + maxW) {
        break;
      }
      this->rounded(skia::SkRect::MakeXYWH(dx, baseline - 10.0f, 5.0f, 10.0f),
                    1.0f, client::ui::starColor(diff.fStars));
      dx += 6.0f;
    }
  }

  [[nodiscard]] static skia::SkColor statusColour(std::string_view status) {
    if (status == "ranked" || status == "approved") {
      return skia::colorSetARGB(255, 102, 204, 255);
    }
    if (status == "loved") {
      return skia::colorSetARGB(255, 255, 102, 170);
    }
    if (status == "qualified") {
      return skia::colorSetARGB(255, 102, 204, 255);
    }
    if (status == "graveyard") {
      return skia::colorSetARGB(255, 140, 140, 155);
    }
    return skia::colorSetARGB(255, 179, 217, 68);
  }

  // Filters the mirror cannot apply, applied here, then the sort.
  void rebuildVisible(std::span<const Entry> entries) {
    fVisible.clear();
    fCardState.resize(entries.size());
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

  // osu! genre/language ids, in the order of the enums above.
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
  std::vector<Hit> fHits;
  std::vector<int> fVisible;
  struct CardState {
    float fExpand = 0.0f;   // button column / statistics
    float fExpanded = 0.0f; // the difficulty list under the card
    double fHoverMs = 0.0;  // time spent over the bottom info row
  };
  std::vector<CardState> fCardState;
  skia::SkRect fTextBox = skia::SkRect::MakeEmpty();
  skia::SkCanvas *fCanvas = nullptr;
  skia::SkFont *fFont = nullptr;
  float fMouseX = 0.0f, fMouseY = 0.0f;
  float fScroll = 0.0f, fScrollTarget = 0.0f, fMaxScroll = 0.0f;
  double fBlink = 0.0;
  long fPreviewId = -1;
  float fPreviewProgress = 0.0f;

public:
  void tick(double nowMs) { fBlink = nowMs; }
};

} // namespace client::listing
