export module client.setpage;

import std;
import skia;
import client.ui;
import client.scene;
import client.nodes;
import client.listing;

// osu!lazer's BeatmapSetOverlay: the page a beatmap card opens.
//
// Built as a scene tree. Sources: osu.Game/Overlays/BeatmapSetOverlay.cs and
// Overlays/BeatmapSet/{BeatmapSetHeaderContent,BeatmapPicker,BasicStats,
// Info}.cs. Their numbers: Y_PADDING 25, RIGHT_WIDTH 275, HORIZONTAL_PADDING
// 50, buttons 45 high and 5 apart, title 30, artist 20, difficulty tiles 40.
export namespace client::setpage {

using listing::Entry;
namespace scene = client::scene;
namespace nodes = client::nodes;

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
    constexpr int kSteps = 24;
    skia::SkPaint paint;
    const float step = fBounds.width() / static_cast<float>(kSteps);
    for (int i = 0; i < kSteps; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(kSteps);
      paint.setColor(skia::colorSetARGB(255, 0, 0, 0));
      paint.setAlphaf(alpha * 0.85f * (1.0f - t * 0.75f));
      canvas->drawRect(skia::SkRect::MakeXYWH(fBounds.fLeft + step *
                                                                static_cast<float>(i),
                                              fBounds.fTop, step + 1.0f,
                                              fBounds.height()),
                       paint);
    }
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

  void draw(const Ctx &ctx) {
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
      fScene = this->build(e, ctx);
    }
    if (fPreviewGlyph != nullptr) {
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
    fScene->setHover(ctx.fMouseX, ctx.fMouseY);
    fScene->draw(ctx.fCanvas);
  }

  // The region this page repainted, for a caller that clips frames to
  // damage. Empty means nothing moved.
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
  [[nodiscard]] std::unique_ptr<scene::Drawable> build(const Entry &e,
                                                       const Ctx &ctx) {
    fPreviewGlyph = nullptr;

    auto root = std::make_unique<nodes::Box>(listing::kBackground6);
    root->fRelativeSizeAxes = scene::Axes::kBoth;
    root->fWidth = 1.0f;
    root->fHeight = 1.0f;

    auto scroll = std::make_unique<nodes::ScrollContainer>();
    scroll->fRelativeSizeAxes = scene::Axes::kBoth;
    scroll->fWidth = 1.0f;
    scroll->fHeight = 1.0f;

    auto column = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kVertical);
    column->fRelativeSizeAxes = scene::Axes::kX;
    column->fWidth = 1.0f;
    column->fAutoSizeAxes = scene::Axes::kY;

    column->add(this->buildHeader(e, ctx));
    column->add(this->buildInfo(e, ctx));

    scroll->add(std::move(column));
    root->add(std::move(scroll));
    return root;
  }

  [[nodiscard]] std::unique_ptr<scene::Drawable> buildHeader(const Entry &e,
                                                             const Ctx &ctx) {
    auto header = std::make_unique<nodes::Box>(listing::kBackground5);
    header->fRelativeSizeAxes = scene::Axes::kX;
    header->fWidth = 1.0f;
    header->fHeight = kHeaderHeight;
    header->fMasking = true;

    if (e.fPageCoverSt == Entry::Cover::kReady && e.fPageCover) {
      auto cover = std::make_unique<nodes::Sprite>(e.fPageCover);
      cover->fRelativeSizeAxes = scene::Axes::kBoth;
      cover->fWidth = 1.0f;
      cover->fHeight = 1.0f;
      header->add(std::move(cover));
    } else if (e.fThumbSt == Entry::Thumb::kReady && e.fThumb) {
      auto cover = std::make_unique<nodes::Sprite>(e.fThumb);
      cover->fRelativeSizeAxes = scene::Axes::kBoth;
      cover->fWidth = 1.0f;
      cover->fHeight = 1.0f;
      header->add(std::move(cover));
    }
    auto gradient = std::make_unique<CoverGradient>();
    gradient->fRelativeSizeAxes = scene::Axes::kBoth;
    gradient->fWidth = 1.0f;
    gradient->fHeight = 1.0f;
    header->add(std::move(gradient));

    header->add(this->buildHeaderLeft(e));
    header->add(this->buildPicker(e));
    return header;
  }

  [[nodiscard]] std::unique_ptr<scene::Drawable>
  buildHeaderLeft(const Entry &e) {
    auto left = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kVertical);
    left->fRelativeSizeAxes = scene::Axes::kX;
    left->fWidth = 1.0f;
    left->fAutoSizeAxes = scene::Axes::kY;
    left->fPadding = {kYPadding, kRightWidth + kHorizontalPadding + 10.0f,
                      kYPadding, kHorizontalPadding};
    left->setSpacing(0.0f, 4.0f);

    auto title = std::make_unique<nodes::Text>(
        e.fTitleUnicode.empty() ? e.fTitle : e.fTitleUnicode, 30.0f,
        listing::kContent1, true);
    left->add(std::move(title));

    auto badges = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kHorizontal);
    badges->fAutoSizeAxes = scene::Axes::kBoth;
    badges->setSpacing(4.0f, 0.0f);
    badges->fWrap = false;
    const auto badge = [&](const char *label, skia::SkColor colour) {
      auto box = std::make_unique<nodes::Box>(colour);
      box->fAutoSizeAxes = scene::Axes::kBoth;
      box->fCornerRadius = 3.0f;
      box->fPadding = {2.0f, 6.0f, 2.0f, 6.0f};
      box->add(std::make_unique<nodes::Text>(label, 10.0f,
                                             listing::kBackground6, true));
      badges->add(std::move(box));
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
    left->add(std::move(badges));

    left->add(std::make_unique<nodes::Text>(
        e.fArtistUnicode.empty() ? e.fArtist : e.fArtistUnicode, 20.0f,
        listing::kContent1, false));

    auto mapper = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kHorizontal);
    mapper->fAutoSizeAxes = scene::Axes::kBoth;
    mapper->fWrap = false;
    mapper->add(std::make_unique<nodes::Text>("mapped by ", 14.0f,
                                              listing::kContent2, false));
    mapper->add(std::make_unique<nodes::Text>(e.fCreator, 14.0f,
                                              listing::kContent1, true));
    left->add(std::move(mapper));

    auto status = std::make_unique<nodes::Box>(listing::kColour3);
    status->fAutoSizeAxes = scene::Axes::kBoth;
    status->fCornerRadius = 9.0f;
    status->fPadding = {2.0f, 10.0f, 2.0f, 10.0f};
    status->fMargin = {6.0f, 0.0f, 0.0f, 0.0f};
    status->add(std::make_unique<nodes::Text>(
        e.fStatus.empty() ? "unknown" : e.fStatus, 11.0f,
        listing::kBackground6, true));
    left->add(std::move(status));

    left->add(this->buildButtons(e));
    return left;
  }

  [[nodiscard]] std::unique_ptr<scene::Drawable> buildButtons(const Entry &e) {
    auto row = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kHorizontal);
    row->fAutoSizeAxes = scene::Axes::kBoth;
    row->setSpacing(kButtonsSpacing, 0.0f);
    row->fWrap = false;
    row->fMargin = {10.0f, 0.0f, 0.0f, 0.0f};

    auto play = std::make_unique<nodes::Clickable>(
        [this] { fPending = {Action::kPreview, 0}; });
    play->fWidth = kButtonsHeight;
    play->fHeight = kButtonsHeight;
    auto playBg = std::make_unique<nodes::Box>(listing::kBackground3);
    playBg->fRelativeSizeAxes = scene::Axes::kBoth;
    playBg->fWidth = 1.0f;
    playBg->fHeight = 1.0f;
    playBg->fCornerRadius = 6.0f;
    play->add(std::move(playBg));
    auto glyph = std::make_unique<PreviewGlyph>();
    glyph->fRelativeSizeAxes = scene::Axes::kBoth;
    glyph->fWidth = 1.0f;
    glyph->fHeight = 1.0f;
    fPreviewGlyph = glyph.get();
    play->add(std::move(glyph));
    row->add(std::move(play));

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
    auto download = std::make_unique<nodes::Clickable>(
        [this] { fPending = {Action::kDownload, 0}; });
    download->fWidth = 240.0f;
    download->fHeight = kButtonsHeight;
    auto downloadBg = std::make_unique<nodes::Box>(
        done ? listing::kBackground3 : listing::kColour3);
    downloadBg->fRelativeSizeAxes = scene::Axes::kBoth;
    downloadBg->fWidth = 1.0f;
    downloadBg->fHeight = 1.0f;
    downloadBg->fCornerRadius = 6.0f;
    download->add(std::move(downloadBg));
    auto text = std::make_unique<nodes::Text>(
        label, 16.0f, done ? listing::kContent2 : listing::kBackground6, true);
    text->fAnchor = scene::Anchor::kCentre;
    text->fOrigin = scene::Anchor::kCentre;
    download->add(std::move(text));
    row->add(std::move(download));
    return row;
  }

  // BeatmapPicker: a tile per difficulty over the set's counts.
  [[nodiscard]] std::unique_ptr<scene::Drawable> buildPicker(const Entry &e) {
    auto right = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kVertical);
    right->fWidth = kRightWidth;
    right->fAutoSizeAxes = scene::Axes::kY;
    right->fAnchor = scene::Anchor::kTopRight;
    right->fOrigin = scene::Anchor::kTopRight;
    right->fMargin = {kYPadding, kHorizontalPadding, 0.0f, 0.0f};
    right->setSpacing(0.0f, 6.0f);

    right->add(std::make_unique<nodes::Text>(
        std::format("{} plays    {} favourites", e.fPlayCount,
                    e.fFavouriteCount),
        12.0f, listing::kContent2, false));

    auto tiles = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kHorizontal);
    tiles->fWidth = kRightWidth;
    tiles->fAutoSizeAxes = scene::Axes::kY;
    tiles->setSpacing(kTileSpacing, kTileSpacing);
    for (std::size_t i = 0; i < e.fDiffs.size(); ++i) {
      const auto &diff = e.fDiffs[i];
      const bool selected = static_cast<int>(i) == fSelected;
      const int index = static_cast<int>(i);
      auto tile = std::make_unique<nodes::Clickable>([this, index] {
        fSelected = index;
        fPending = {Action::kSelectDiff, index};
      });
      tile->fWidth = kTileSize;
      tile->fHeight = kTileSize;
      auto bg = std::make_unique<nodes::Box>(
          selected ? listing::kBackground3 : listing::kBackground5);
      bg->fRelativeSizeAxes = scene::Axes::kBoth;
      bg->fWidth = 1.0f;
      bg->fHeight = 1.0f;
      bg->fCornerRadius = 4.0f;
      tile->add(std::move(bg));
      auto dot = std::make_unique<nodes::Box>(client::ui::starColor(diff.fStars));
      dot->fWidth = 16.0f;
      dot->fHeight = 16.0f;
      dot->fCornerRadius = 8.0f;
      dot->fAnchor = scene::Anchor::kTopCentre;
      dot->fOrigin = scene::Anchor::kTopCentre;
      dot->fY = 6.0f;
      tile->add(std::move(dot));
      auto stars = std::make_unique<nodes::Text>(
          std::format("{:.1f}", diff.fStars), 10.0f,
          selected ? listing::kContent1 : listing::kContent2, selected);
      stars->fAnchor = scene::Anchor::kBottomCentre;
      stars->fOrigin = scene::Anchor::kBottomCentre;
      stars->fY = -3.0f;
      tile->add(std::move(stars));
      tiles->add(std::move(tile));
    }
    right->add(std::move(tiles));

    if (!e.fDiffs.empty()) {
      right->add(std::make_unique<nodes::Text>(
          e.fDiffs[static_cast<std::size_t>(fSelected)].fVersion, 16.0f,
          listing::kContent1, true));
    }
    return right;
  }

  // Info: the selected difficulty's numbers, the metadata, the ratings.
  [[nodiscard]] std::unique_ptr<scene::Drawable> buildInfo(const Entry &e,
                                                           const Ctx &ctx) {
    auto section = std::make_unique<nodes::Box>(listing::kBackground5);
    section->fRelativeSizeAxes = scene::Axes::kX;
    section->fWidth = 1.0f;
    section->fAutoSizeAxes = scene::Axes::kY;
    section->fPadding = {kYPadding, kHorizontalPadding, kYPadding,
                         kHorizontalPadding};

    auto left = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kVertical);
    left->fWidth = std::max(200.0f, ctx.fWidth - kHorizontalPadding * 2.0f -
                                        kRightWidth - 30.0f);
    left->fAutoSizeAxes = scene::Axes::kY;
    left->setSpacing(0.0f, 8.0f);

    if (!e.fDiffs.empty()) {
      const auto &diff = e.fDiffs[static_cast<std::size_t>(fSelected)];
      auto heading = std::make_unique<nodes::FillFlow>(
          nodes::FillFlow::Direction::kHorizontal);
      heading->fAutoSizeAxes = scene::Axes::kBoth;
      heading->setSpacing(8.0f, 0.0f);
      heading->fWrap = false;
      auto pill = std::make_unique<nodes::Box>(client::ui::starColor(diff.fStars));
      pill->fAutoSizeAxes = scene::Axes::kBoth;
      pill->fCornerRadius = 9.0f;
      pill->fPadding = {1.0f, 8.0f, 1.0f, 8.0f};
      pill->add(std::make_unique<nodes::Text>(
          std::format("{:.2f}", diff.fStars), 12.0f, listing::kBackground6,
          true));
      heading->add(std::move(pill));
      heading->add(std::make_unique<nodes::Text>(diff.fVersion, 16.0f,
                                                 listing::kContent1, true));
      left->add(std::move(heading));

      auto stats = std::make_unique<nodes::FillFlow>(
          nodes::FillFlow::Direction::kHorizontal);
      stats->fRelativeSizeAxes = scene::Axes::kX;
      stats->fWidth = 1.0f;
      stats->fAutoSizeAxes = scene::Axes::kY;
      stats->setSpacing(0.0f, 10.0f);
      const float cellWidth = left->fWidth / 4.0f - 1.0f;
      const auto stat = [&](const char *label, std::string value, float bar) {
        auto cell = std::make_unique<nodes::FillFlow>(
            nodes::FillFlow::Direction::kVertical);
        cell->fWidth = cellWidth;
        cell->fAutoSizeAxes = scene::Axes::kY;
        cell->setSpacing(0.0f, 2.0f);
        cell->add(std::make_unique<nodes::Text>(label, 11.0f,
                                                listing::kContent2, false));
        cell->add(std::make_unique<nodes::Text>(std::move(value), 17.0f,
                                                listing::kContent1, true));
        if (bar > 0.0f) {
          auto track = std::make_unique<nodes::Box>(listing::kBackground6);
          track->fWidth = cellWidth - 16.0f;
          track->fHeight = 4.0f;
          track->fCornerRadius = 2.0f;
          auto fill = std::make_unique<nodes::Box>(listing::kColour1);
          fill->fRelativeSizeAxes = scene::Axes::kBoth;
          fill->fWidth = std::clamp(bar, 0.0f, 1.0f);
          fill->fHeight = 1.0f;
          fill->fCornerRadius = 2.0f;
          track->add(std::move(fill));
          cell->add(std::move(track));
        }
        stats->add(std::move(cell));
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
      left->add(std::move(stats));
    }

    const auto metaRow = [&](const char *label, std::string value) {
      auto row = std::make_unique<nodes::FillFlow>(
          nodes::FillFlow::Direction::kHorizontal);
      row->fRelativeSizeAxes = scene::Axes::kX;
      row->fWidth = 1.0f;
      row->fAutoSizeAxes = scene::Axes::kY;
      row->fWrap = false;
      auto name = std::make_unique<nodes::Text>(label, 11.0f,
                                                listing::kContent2, false);
      name->fWidth = 110.0f;
      name->setMaxWidth(110.0f);
      row->add(std::move(name));
      auto text = std::make_unique<nodes::Text>(std::move(value), 13.0f,
                                                listing::kContent1, false);
      text->setMaxWidth(left->fWidth - 120.0f);
      row->add(std::move(text));
      left->add(std::move(row));
    };
    metaRow("Source", e.fSource.empty() ? "-" : e.fSource);
    metaRow("Genre", listing::kGenreLabels[genreIndex(e.fGenre)]);
    metaRow("Language", listing::kLanguageLabels[languageIndex(e.fLanguage)]);
    metaRow("Tags", e.fTags.empty() ? "-" : e.fTags);
    metaRow("Last updated", e.fUpdated);

    section->add(std::move(left));

    auto right = std::make_unique<nodes::FillFlow>(
        nodes::FillFlow::Direction::kVertical);
    right->fWidth = kRightWidth;
    right->fAutoSizeAxes = scene::Axes::kY;
    right->fAnchor = scene::Anchor::kTopRight;
    right->fOrigin = scene::Anchor::kTopRight;
    right->setSpacing(0.0f, 6.0f);
    right->add(std::make_unique<nodes::Text>("User Rating", 12.0f,
                                             listing::kContent2, false));
    // osu! returns eleven buckets, the first unused.
    std::vector<int> counts(10, 0);
    for (std::size_t i = 1; i < e.fRatings.size() && i <= 10; ++i) {
      counts[i - 1] = e.fRatings[i];
    }
    auto ratings = std::make_unique<Ratings>(std::move(counts));
    ratings->fWidth = kRightWidth;
    ratings->fHeight = 110.0f;
    right->add(std::move(ratings));
    section->add(std::move(right));
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
  std::unique_ptr<scene::Drawable> fScene;
  PreviewGlyph *fPreviewGlyph = nullptr; // owned by the tree
};

} // namespace client::setpage
