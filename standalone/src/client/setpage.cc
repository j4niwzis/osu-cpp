export module client.setpage;

import std;
import skia;
import client.palette;
import skiff.scene;
import skiff.nodes;
import client.listing;

// osu!lazer's BeatmapSetOverlay: the page a beatmap card opens.
//
// Built as a scene tree. Sources: osu.Game/Overlays/BeatmapSetOverlay.cs and
// Overlays/BeatmapSet/{BeatmapSetHeaderContent,BeatmapPicker,BasicStats,
// Info}.cs. Their numbers: Y_PADDING 25, RIGHT_WIDTH 275, HORIZONTAL_PADDING
// 50, buttons 45 high and 5 apart, title 30, artist 20, difficulty tiles 40.
export namespace client::setpage {

using listing::Entry;
namespace scene = skiff::scene;
namespace nodes = skiff::nodes;

inline constexpr float kYPadding = 25.0f;
inline constexpr float kRightWidth = 275.0f;
inline constexpr float kHorizontalPadding = 50.0f;
inline constexpr float kButtonsHeight = 45.0f;
inline constexpr float kButtonsSpacing = 5.0f;
inline constexpr float kTileSize = 40.0f;
inline constexpr float kTileSpacing = 2.0f;
inline constexpr float kHeaderHeight = 250.0f;

// The cover's darkening, which the header text sits on: black at the left,
// clear at the right.
class CoverGradient : public scene::Drawable {
protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    paint::horizontalGradient(
        canvas, fBounds,
        skia::colorSetARGB(static_cast<std::uint32_t>(255.0f * 0.85f), 0, 0, 0),
        skia::colorSetARGB(static_cast<std::uint32_t>(255.0f * 0.85f * 0.25f),
                           0, 0, 0),
        alpha);
  }
};

// The play/pause glyph with CircularProgress around it, as the header shows
// while a preview runs.
class PreviewGlyph : public scene::Drawable {
public:
  bool fPlaying = false;
  float fProgress = 0.0f;

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(listing::kContent1);
    paint.setAlphaf(alpha);
    const float cx = fBounds.centerX();
    const float cy = fBounds.centerY();
    if (!fPlaying) {
      skia::SkPathBuilder tri;
      tri.moveTo(cx - 7.0f, cy - 10.0f)
          .lineTo(cx + 10.0f, cy)
          .lineTo(cx - 7.0f, cy + 10.0f)
          .close();
      canvas->drawPath(tri.detach(), paint);
      return;
    }
    canvas->drawRect(skia::SkRect::MakeXYWH(cx - 7.0f, cy - 9.0f, 5.0f, 18.0f),
                     paint);
    canvas->drawRect(skia::SkRect::MakeXYWH(cx + 2.0f, cy - 9.0f, 5.0f, 18.0f),
                     paint);
    skia::SkPaint ring;
    ring.setAntiAlias(true);
    ring.setStyle(skia::kStrokeStyle);
    ring.setStrokeWidth(2.5f);
    ring.setColor(listing::kColour1);
    ring.setAlphaf(alpha);
    const float r = fBounds.width() * 0.42f;
    canvas->drawArc(skia::SkRect::MakeXYWH(cx - r, cy - r, r * 2.0f, r * 2.0f),
                    -90.0f, 360.0f * std::clamp(fProgress, 0.0f, 1.0f), false,
                    ring);
  }
};

// UserRatings: the positive/negative split bar over the 1..10 histogram.
class Ratings : public scene::Drawable {
public:
  explicit Ratings(std::vector<int> counts) : fCounts(std::move(counts)) {
    fCounts.resize(10, 0);
  }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    const int negative = fCounts[0] + fCounts[1] + fCounts[2] + fCounts[3] +
                         fCounts[4];
    const int positive = fCounts[5] + fCounts[6] + fCounts[7] + fCounts[8] +
                         fCounts[9];
    const int total = negative + positive;
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(alpha);

    const skia::SkRect bar =
        skia::SkRect::MakeXYWH(fBounds.fLeft, fBounds.fTop, fBounds.width(), 5.0f);
    paint.setColor(skia::colorSetARGB(255, 255, 102, 102));
    canvas->drawRRect(skia::SkRRect::MakeRectXY(bar, 2.5f, 2.5f), paint);
    if (total > 0) {
      const float share =
          static_cast<float>(positive) / static_cast<float>(total);
      paint.setColor(listing::kColour1);
      canvas->drawRRect(
          skia::SkRRect::MakeRectXY(
              skia::SkRect::MakeXYWH(bar.fLeft + bar.width() * (1.0f - share),
                                     bar.fTop, bar.width() * share, 5.0f),
              2.5f, 2.5f),
          paint);
    }

    const float graphTop = fBounds.fTop + 26.0f;
    const float graphH = fBounds.height() - 34.0f;
    const float slot = fBounds.width() / 10.0f;
    const int peak = std::max(1, *std::ranges::max_element(fCounts));
    paint.setColor(listing::kColour1);
    paint.setAlphaf(alpha * 0.8f);
    for (std::size_t i = 0; i < fCounts.size(); ++i) {
      const float h = graphH * static_cast<float>(fCounts[i]) /
                      static_cast<float>(peak);
      const skia::SkRect column = skia::SkRect::MakeXYWH(
          fBounds.fLeft + static_cast<float>(i) * slot + 2.0f,
          graphTop + graphH - h, slot - 4.0f, std::max(1.0f, h));
      canvas->drawRRect(skia::SkRRect::MakeRectXY(column, 2.0f, 2.0f), paint);
    }
  }

private:
  std::vector<int> fCounts;
};

class SetPage {
public:
  struct Ctx {
    skia::SkCanvas *fCanvas = nullptr;
    skia::SkFont *fFont = nullptr;
    float fWidth = 0.0f, fHeight = 0.0f;
    float fMouseX = 0.0f, fMouseY = 0.0f;
    double fNowMs = 0.0;
    bool fPreviewPlaying = false;
    float fPreviewProgress = 0.0f;
    const Entry *fEntry = nullptr; // looked up by id each frame
  };
  enum class Action { kNone, kClose, kDownload, kPreview, kSelectDiff };
  struct Result {
    Action fAction = Action::kNone;
    int fValue = 0;
  };

  [[nodiscard]] bool open() const noexcept { return fOpen; }
  [[nodiscard]] long setId() const noexcept { return fSetId; }
  [[nodiscard]] int selectedDiff() const noexcept { return fSelected; }

  void show(const Entry &entry) {
    fSetId = entry.fSetId;
    fOpen = true;
    fSelected = entry.fDiffs.empty()
                    ? 0
                    : static_cast<int>(entry.fDiffs.size()) - 1; // hardest
    fScene.reset();
    fFingerprint.clear();
  }

  void close() {
    fOpen = false;
    fSetId = -1;
    fScene.reset();
  }

  void scroll(float ticks) { fScrollTicks += ticks; }

  // Split in two on purpose: everything that decides what the page looks
  // like happens in update(), before a frame has been committed to, and
  // render() only puts it on a canvas. That is what lets the client find out
  // whether a frame is worth drawing before it draws it.
  void draw(const Ctx &ctx) {
    this->update(ctx);
    this->render(ctx.fCanvas);
  }

  void render(skia::SkCanvas *canvas) {
    if (fOpen && fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  void update(const Ctx &ctx) {
    if (!fOpen || ctx.fEntry == nullptr) {
      return;
    }
    const Entry &e = *ctx.fEntry;
    fSelected = std::clamp(fSelected, 0,
                           std::max(0, static_cast<int>(e.fDiffs.size()) - 1));

    // The tree is rebuilt when what it shows changes -- a selected
    // difficulty, an arriving cover, another percent of a download -- and
    // reused otherwise.
    const std::string fingerprint = this->fingerprintFor(e, ctx);
    if (!fScene || fingerprint != fFingerprint) {
      fFingerprint = fingerprint;
      fScene = this->build(e);
      fRebuilt = true;
    }
    if (fPreviewGlyph != nullptr) {
      // The ring around the play button is the only thing a preview moves on
      // this page, so it is the only thing that has to be repainted for it.
      if (fPreviewGlyph->fPlaying != ctx.fPreviewPlaying ||
          fPreviewGlyph->fProgress != ctx.fPreviewProgress) {
        fPreviewGlyph->markDamaged();
      }
      fPreviewGlyph->fPlaying = ctx.fPreviewPlaying;
      fPreviewGlyph->fProgress = ctx.fPreviewProgress;
    }

    const skia::SkRect screen = skia::SkRect::MakeWH(ctx.fWidth, ctx.fHeight);
    fScene->updateTree(ctx.fNowMs);
    if (fScrollTicks != 0.0f) {
      fScene->scroll(ctx.fMouseX, ctx.fMouseY, fScrollTicks);
      fScrollTicks = 0.0f;
    }
    fScene->layoutIfNeeded(screen);
    if (fRebuilt) {
      // Nothing in a freshly built tree has marked itself yet; after the
      // first layout it can say how much of the screen it covers.
      fRebuilt = false;
      fScene->markDamaged();
    }
    fScene->setHover(ctx.fMouseX, ctx.fMouseY);
  }

  // The region this page repainted, for a caller that clips frames to
  // damage. Empty means nothing moved.
  // Whether a transform in the tree is still running: eased values say so
  // themselves, transforms have to be asked.
  [[nodiscard]] bool animating() const {
    return fOpen && fScene && fScene->animatingTree();
  }

  [[nodiscard]] skia::SkRect takeDamage() {
    return fScene ? fScene->takeDamage() : skia::SkRect::MakeEmpty();
  }

  [[nodiscard]] Result click(float x, float y) {
    if (!fOpen || !fScene) {
      return {};
    }
    fPending = {};
    fScene->click(x, y);
    return fPending;
  }

private:
  [[nodiscard]] std::string fingerprintFor(const Entry &e,
                                           const Ctx &ctx) const {
    return std::format("{}|{}|{}|{}|{:.0f}|{}x{}", e.fSetId, fSelected,
                       static_cast<int>(e.fSt),
                       e.fPageCoverSt == Entry::Cover::kReady ? 1 : 0,
                       static_cast<double>(e.fProgress) * 100.0,
                       static_cast<int>(ctx.fWidth),
                       static_cast<int>(ctx.fHeight));
  }

  // ---- tree ---------------------------------------------------------------
  [[nodiscard]] std::unique_ptr<scene::Drawable> build(const Entry &e) {
    fPreviewGlyph = nullptr;

    auto root = scene::make<nodes::Box>({.fill = true}, listing::kBackground6);
    auto *scroll = root->add<nodes::ScrollContainer>({.fill = true});
    auto *column = scroll->add<nodes::FillFlow>(
        {.fillX = true, .autoSize = scene::Axes::kY},
        nodes::FillFlow::Direction::kVertical);

    column->add(this->buildHeader(e));
    column->add(this->buildInfo(e));
    return root;
  }

  [[nodiscard]] std::unique_ptr<scene::Drawable> buildHeader(const Entry &e) {
    auto header = scene::make<nodes::Box>(
        {.fillX = true, .height = kHeaderHeight, .masking = true},
        listing::kBackground5);

    if (e.fPageCoverSt == Entry::Cover::kReady && e.fPageCover) {
      header->add<nodes::Sprite>({.fill = true}, e.fPageCover);
    } else if (e.fThumbSt == Entry::Thumb::kReady && e.fThumb) {
      header->add<nodes::Sprite>({.fill = true}, e.fThumb);
    }
    header->add<CoverGradient>({.fill = true});

    header->add(this->buildHeaderLeft(e));
    header->add(this->buildPicker(e));
    return header;
  }

  [[nodiscard]] std::unique_ptr<scene::Drawable>
  buildHeaderLeft(const Entry &e) {
    auto left = scene::make<nodes::FillFlow>(
        {.fillX = true,
         .autoSize = scene::Axes::kY,
         .padding = {kYPadding, kRightWidth + kHorizontalPadding + 10.0f,
                     kYPadding, kHorizontalPadding}},
        nodes::FillFlow::Direction::kVertical, 0.0f, 4.0f);

    left->add<nodes::Text>(
        {}, e.fTitleUnicode.empty() ? e.fTitle : e.fTitleUnicode, 30.0f,
        listing::kContent1, true);

    auto *badges = left->add<nodes::FillFlow>(
        {.autoSize = scene::Axes::kBoth},
        nodes::FillFlow::Direction::kHorizontal, 4.0f, 0.0f);
    badges->fWrap = false;
    const auto badge = [&](const char *label, skia::SkColor colour) {
      badges
          ->add<nodes::Box>({.autoSize = scene::Axes::kBoth,
                             .padding = {2.0f, 6.0f, 2.0f, 6.0f},
                             .cornerRadius = 3.0f},
                            colour)
          ->add<nodes::Text>({}, label, 10.0f, listing::kBackground6, true);
    };
    if (e.fVideo) {
      badge("VIDEO", listing::kContent2);
    }
    if (e.fStoryboard) {
      badge("STORYBOARD", listing::kContent2);
    }
    if (e.fNsfw) {
      badge("EXPLICIT", skia::colorSetARGB(255, 255, 102, 102));
    }
    if (e.fSpotlight) {
      badge("SPOTLIGHT", listing::kColour1);
    }
    if (e.fFeatured) {
      badge("FEATURED ARTIST", skia::colorSetARGB(255, 255, 204, 102));
    }

    left->add<nodes::Text>(
        {}, e.fArtistUnicode.empty() ? e.fArtist : e.fArtistUnicode, 20.0f,
        listing::kContent1, false);

    auto *mapper = left->add<nodes::FillFlow>(
        {.autoSize = scene::Axes::kBoth},
        nodes::FillFlow::Direction::kHorizontal);
    mapper->fWrap = false;
    mapper->fCrossAlign = nodes::FillFlow::Cross::kMiddle;
    mapper->add<nodes::Text>({}, "mapped by ", 14.0f, listing::kContent2,
                             false);
    mapper->add<nodes::Text>({}, e.fCreator, 14.0f, listing::kContent1, true);

    left->add<nodes::Box>({.autoSize = scene::Axes::kBoth,
                           .margin = {6.0f, 0.0f, 0.0f, 0.0f},
                           .padding = {2.0f, 10.0f, 2.0f, 10.0f},
                           .cornerRadius = 9.0f},
                          listing::kColour3)
        ->add<nodes::Text>({}, e.fStatus.empty() ? "unknown" : e.fStatus, 11.0f,
                           listing::kBackground6, true);

    left->add(this->buildButtons(e));
    return left;
  }

  [[nodiscard]] std::unique_ptr<scene::Drawable> buildButtons(const Entry &e) {
    auto row = scene::make<nodes::FillFlow>(
        {.autoSize = scene::Axes::kBoth, .margin = {10.0f, 0.0f, 0.0f, 0.0f}},
        nodes::FillFlow::Direction::kHorizontal, kButtonsSpacing, 0.0f);
    row->fWrap = false;

    auto *play = row->add<nodes::Clickable>(
        {.width = kButtonsHeight, .height = kButtonsHeight},
        [this] { fPending = {Action::kPreview, 0}; });
    play->add<nodes::Box>({.fill = true, .cornerRadius = 6.0f},
                          listing::kBackground3);
    fPreviewGlyph = play->add<PreviewGlyph>({.fill = true});

    const bool done = e.fSt == Entry::St::kDone;
    std::string label = "Download";
    if (e.fSt == Entry::St::kFetching) {
      label = std::format("Downloading {:.0f}%",
                          static_cast<double>(e.fProgress) * 100.0);
    } else if (done) {
      label = "In library";
    } else if (e.fVideo) {
      label = "Download with video";
    }

    auto *download = row->add<nodes::Clickable>(
        {.width = 240.0f, .height = kButtonsHeight},
        [this] { fPending = {Action::kDownload, 0}; });
    download->add<nodes::Box>({.fill = true, .cornerRadius = 6.0f},
                              done ? listing::kBackground3 : listing::kColour3);
    download->add<nodes::Text>(
        {.place = scene::Anchor::kCentre}, label, 16.0f,
        done ? listing::kContent2 : listing::kBackground6, true);
    return row;
  }

  // BeatmapPicker: a tile per difficulty over the set's counts.
  [[nodiscard]] std::unique_ptr<scene::Drawable> buildPicker(const Entry &e) {
    auto right = scene::make<nodes::FillFlow>(
        {.place = scene::Anchor::kTopRight,
         .width = kRightWidth,
         .autoSize = scene::Axes::kY,
         .margin = {kYPadding, kHorizontalPadding, 0.0f, 0.0f}},
        nodes::FillFlow::Direction::kVertical, 0.0f, 6.0f);

    right->add<nodes::Text>(
        {},
        std::format("{} plays    {} favourites", e.fPlayCount,
                    e.fFavouriteCount),
        12.0f, listing::kContent2, false);

    auto *tiles = right->add<nodes::FillFlow>(
        {.width = kRightWidth, .autoSize = scene::Axes::kY},
        nodes::FillFlow::Direction::kHorizontal, kTileSpacing, kTileSpacing);
    for (std::size_t i = 0; i < e.fDiffs.size(); ++i) {
      const auto &diff = e.fDiffs[i];
      const bool selected = static_cast<int>(i) == fSelected;
      const int index = static_cast<int>(i);

      auto *tile = tiles->add<nodes::Clickable>(
          {.width = kTileSize, .height = kTileSize}, [this, index] {
            fSelected = index;
            fPending = {Action::kSelectDiff, index};
          });
      tile->add<nodes::Box>(
          {.fill = true, .cornerRadius = 4.0f},
          selected ? listing::kBackground3 : listing::kBackground5);
      tile->add<nodes::Box>({.place = scene::Anchor::kTopCentre,
                             .y = 6.0f,
                             .width = 16.0f,
                             .height = 16.0f,
                             .cornerRadius = 8.0f},
                            client::palette::starColor(diff.fStars));
      tile->add<nodes::Text>(
          {.place = scene::Anchor::kBottomCentre, .y = -3.0f},
          std::format("{:.1f}", diff.fStars), 10.0f,
          selected ? listing::kContent1 : listing::kContent2, selected);
    }

    if (!e.fDiffs.empty()) {
      right->add<nodes::Text>(
          {}, e.fDiffs[static_cast<std::size_t>(fSelected)].fVersion, 16.0f,
          listing::kContent1, true);
    }
    return right;
  }

  // Info: the selected difficulty's numbers, the metadata, the ratings.
  [[nodiscard]] std::unique_ptr<scene::Drawable> buildInfo(const Entry &e) {
    auto section = scene::make<nodes::Box>(
        {.fillX = true,
         .autoSize = scene::Axes::kY,
         .padding = {kYPadding, kHorizontalPadding, kYPadding,
                     kHorizontalPadding}},
        listing::kBackground5);

    // Two columns, 30 apart: the ratings at a fixed width and the rest
    // taking what is left.
    auto *columns = section->add<nodes::FillFlow>(
        {.fillX = true, .autoSize = scene::Axes::kY},
        nodes::FillFlow::Direction::kHorizontal, 30.0f, 0.0f);
    columns->fWrap = false;
    auto *left = columns->add<nodes::FillFlow>(
        {.grow = scene::Axes::kX, .autoSize = scene::Axes::kY},
        nodes::FillFlow::Direction::kVertical, 0.0f, 8.0f);

    if (!e.fDiffs.empty()) {
      const auto &diff = e.fDiffs[static_cast<std::size_t>(fSelected)];

      auto *heading = left->add<nodes::FillFlow>(
          {.autoSize = scene::Axes::kBoth},
          nodes::FillFlow::Direction::kHorizontal, 8.0f, 0.0f);
      heading->fWrap = false;
      heading->fCrossAlign = nodes::FillFlow::Cross::kMiddle;
      heading
          ->add<nodes::Box>({.autoSize = scene::Axes::kBoth,
                             .padding = {1.0f, 8.0f, 1.0f, 8.0f},
                             .cornerRadius = 9.0f},
                            client::palette::starColor(diff.fStars))
          ->add<nodes::Text>({}, std::format("{:.2f}", diff.fStars), 12.0f,
                             listing::kBackground6, true);
      heading->add<nodes::Text>({}, diff.fVersion, 16.0f, listing::kContent1,
                                true);

      auto *stats = left->add<nodes::FillFlow>(
          {.fillX = true, .autoSize = scene::Axes::kY},
          nodes::FillFlow::Direction::kHorizontal, 0.0f, 10.0f);
      const auto stat = [&](const char *label, std::string value, float bar) {
        auto *cell = stats->add<nodes::FillFlow>(
            {.width = 0.25f,
             .relativeSize = scene::Axes::kX,
             .autoSize = scene::Axes::kY},
            nodes::FillFlow::Direction::kVertical, 0.0f, 2.0f);
        cell->add<nodes::Text>({}, label, 11.0f, listing::kContent2, false);
        cell->add<nodes::Text>({}, std::move(value), 17.0f, listing::kContent1,
                               true);
        if (bar > 0.0f) {
          cell->add<nodes::Box>({.fillX = true,
                                 .height = 4.0f,
                                 .margin = {0.0f, 16.0f, 0.0f, 0.0f},
                                 .cornerRadius = 2.0f},
                                listing::kBackground6)
              ->add<nodes::Box>({.width = std::clamp(bar, 0.0f, 1.0f),
                                 .height = 1.0f,
                                 .relativeSize = scene::Axes::kBoth,
                                 .cornerRadius = 2.0f},
                                listing::kColour1);
        }
      };
      stat("Length", formatLength(diff.fLengthMs), 0.0f);
      stat("BPM", std::format("{:.0f}", e.fBpm), 0.0f);
      stat("Max Combo", std::format("{}", diff.fMaxCombo), 0.0f);
      stat("Star Rating", std::format("{:.2f}", diff.fStars),
           static_cast<float>(std::min(1.0, diff.fStars / 10.0)));
      stat("Circle Size", std::format("{:.1f}", diff.fCs),
           static_cast<float>(diff.fCs / 10.0));
      stat("HP Drain", std::format("{:.1f}", diff.fHp),
           static_cast<float>(diff.fHp / 10.0));
      stat("Accuracy", std::format("{:.1f}", diff.fOd),
           static_cast<float>(diff.fOd / 10.0));
      stat("Approach Rate", std::format("{:.1f}", diff.fAr),
           static_cast<float>(diff.fAr / 10.0));
    }

    const auto metaRow = [&](const char *label, std::string value) {
      auto *row = left->add<nodes::FillFlow>(
          {.fillX = true, .autoSize = scene::Axes::kY},
          nodes::FillFlow::Direction::kHorizontal);
      row->fWrap = false;
      row->add<nodes::Text>({.width = 110.0f}, label, 11.0f,
                            listing::kContent2, false)
          ->setMaxWidth(110.0f);
      row->add<nodes::Text>({.grow = scene::Axes::kX}, std::move(value), 13.0f,
                            listing::kContent1, false);
    };
    metaRow("Source", e.fSource.empty() ? "-" : e.fSource);
    metaRow("Genre", listing::kGenreLabels[genreIndex(e.fGenre)]);
    metaRow("Language", listing::kLanguageLabels[languageIndex(e.fLanguage)]);
    metaRow("Tags", e.fTags.empty() ? "-" : e.fTags);
    metaRow("Last updated", e.fUpdated);

    auto *right = columns->add<nodes::FillFlow>(
        {.width = kRightWidth, .autoSize = scene::Axes::kY},
        nodes::FillFlow::Direction::kVertical, 0.0f, 6.0f);
    right->add<nodes::Text>({}, "User Rating", 12.0f, listing::kContent2,
                            false);
    // osu! returns eleven buckets, the first unused.
    std::vector<int> counts(10, 0);
    for (std::size_t i = 1; i < e.fRatings.size() && i <= 10; ++i) {
      counts[i - 1] = e.fRatings[i];
    }
    right->add<Ratings>({.width = kRightWidth, .height = 110.0f},
                        std::move(counts));
    return section;
  }

  [[nodiscard]] static std::string formatLength(double ms) {
    const int total = static_cast<int>(ms / 1000.0);
    return std::format("{}:{:02}", total / 60, total % 60);
  }

  // The label tables are in display order; the ids are not contiguous.
  [[nodiscard]] static std::size_t genreIndex(int id) {
    constexpr int kIds[] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14};
    for (std::size_t i = 0; i < std::size(kIds); ++i) {
      if (kIds[i] == id) {
        return i;
      }
    }
    return 0;
  }
  [[nodiscard]] static std::size_t languageIndex(int id) {
    constexpr int kIds[] = {0, 2, 4, 7, 8, 11, 3, 6, 10, 9, 12, 13, 5, 14, 1};
    for (std::size_t i = 0; i < std::size(kIds); ++i) {
      if (kIds[i] == id) {
        return i;
      }
    }
    return 0;
  }

  long fSetId = -1;
  bool fOpen = false;
  int fSelected = 0;
  float fScrollTicks = 0.0f;
  Result fPending;
  std::string fFingerprint;
  bool fRebuilt = false;
  std::unique_ptr<scene::Drawable> fScene;
  PreviewGlyph *fPreviewGlyph = nullptr; // owned by the tree
};

} // namespace client::setpage
