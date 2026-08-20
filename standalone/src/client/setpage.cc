export module client.setpage;

import std;
import skia;
import client.ui;
import client.listing;

// osu!lazer's BeatmapSetOverlay: the page a beatmap card opens.
//
// Sources: osu.Game/Overlays/BeatmapSetOverlay.cs and
// Overlays/BeatmapSet/{BeatmapSetHeaderContent,BeatmapPicker,BasicStats,
// Info}.cs. Their numbers: Y_PADDING 25, RIGHT_WIDTH 275, HORIZONTAL_PADDING
// 50, buttons 45 high and 5 apart, title 30 semibold italic, artist 20 medium
// italic, difficulty tiles 40.
export namespace client::setpage {

using listing::Entry;

inline constexpr float kYPadding = 25.0f;
inline constexpr float kRightWidth = 275.0f;
inline constexpr float kHorizontalPadding = 50.0f;
inline constexpr float kButtonsHeight = 45.0f;
inline constexpr float kButtonsSpacing = 5.0f;
inline constexpr float kTileSize = 40.0f;
inline constexpr float kTileSpacing = 2.0f;

class SetPage {
public:
  struct Ctx {
    skia::SkCanvas *fCanvas = nullptr;
    skia::SkFont *fFont = nullptr;
    float fWidth = 0.0f, fHeight = 0.0f;
    float fMouseX = 0.0f, fMouseY = 0.0f;
    double fDtMs = 16.0;
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
    fScroll = 0.0f;
  }
  void close() {
    fOpen = false;
    fSetId = -1;
  }
  void scroll(float ticks) {
    fScrollTarget = std::max(0.0f, fScrollTarget - ticks * 60.0f);
  }

  void draw(const Ctx &ctx) {
    if (!fOpen || ctx.fEntry == nullptr) {
      return;
    }
    fCanvas = ctx.fCanvas;
    fFont = ctx.fFont;
    fMouseX = ctx.fMouseX;
    fMouseY = ctx.fMouseY;
    fHits.clear();
    const Entry &e = *ctx.fEntry;
    const float w = ctx.fWidth;
    const float h = ctx.fHeight;
    fSelected = std::clamp(fSelected, 0,
                           std::max(0, static_cast<int>(e.fDiffs.size()) - 1));

    this->rect(skia::SkRect::MakeXYWH(0, 0, w, h), listing::kBackground6);
    fScroll = client::ui::approach(fScroll, fScrollTarget, 30.0f, ctx.fDtMs);
    fCanvas->save();
    fCanvas->translate(0.0f, -fScroll);

    const float headerH = this->drawHeader(e, w, ctx);
    const float bottom = this->drawInfo(e, w, headerH);

    fCanvas->restore();
    fMaxScroll = std::max(0.0f, bottom - h + 40.0f);
    fScrollTarget = std::min(fScrollTarget, fMaxScroll);
  }

  [[nodiscard]] Result click(float x, float y) {
    if (!fOpen) {
      return {};
    }
    for (const auto &hit : fHits) {
      if (hit.fRect.contains(x, y + fScroll)) {
        if (hit.fAction == Action::kSelectDiff) {
          fSelected = hit.fValue;
        }
        return {hit.fAction, hit.fValue};
      }
    }
    return {};
  }

private:
  struct Hit {
    skia::SkRect fRect;
    Action fAction;
    int fValue;
  };

  // BeatmapSetHeaderContent: the cover with a gradient over it, the title and
  // artist on the left, the download and preview buttons under them, and the
  // difficulty tiles with the set's statistics on the right.
  float drawHeader(const Entry &e, float w, const Ctx &ctx) {
    const float height = 250.0f;
    const skia::SkRect cover = skia::SkRect::MakeXYWH(0, 0, w, height);
    this->rect(cover, listing::kBackground5);
    if (e.fPageCoverSt == Entry::Cover::kReady && e.fPageCover) {
      this->imageFilled(e.fPageCover.get(), cover);
    } else if (e.fThumbSt == Entry::Thumb::kReady && e.fThumb) {
      this->imageFilled(e.fThumb.get(), cover); // until the big one arrives
    }
    // coverGradient: black at the left, transparent at the right.
    for (int i = 0; i < 24; ++i) {
      const float t = static_cast<float>(i) / 24.0f;
      this->rect(skia::SkRect::MakeXYWH(w * t, 0.0f, w / 24.0f + 1.0f, height),
                 skia::colorSetARGB(255, 0, 0, 0), 0.85f * (1.0f - t * 0.75f));
    }

    const float left = kHorizontalPadding;
    float y = kYPadding + 30.0f;
    const std::string title =
        e.fTitleUnicode.empty() ? e.fTitle : e.fTitleUnicode;
    this->text(title, left, y, 30.0f, listing::kContent1, true);
    // The badges lazer puts beside the title.
    float badgeX = left + this->measure(title, 30.0f, true) + 10.0f;
    const auto badge = [&](const char *label, skia::SkColor colour) {
      const float w = this->measure(label, 10.0f, true) + 12.0f;
      const skia::SkRect r =
          skia::SkRect::MakeXYWH(badgeX, y - 12.0f, w, 16.0f);
      this->rounded(r, 3.0f, colour);
      this->text(label, badgeX + 6.0f, y - 1.0f, 10.0f,
                 listing::kBackground6, true);
      badgeX += w + 4.0f;
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
    y += 28.0f;
    this->text(e.fArtistUnicode.empty() ? e.fArtist : e.fArtistUnicode, left, y,
               20.0f, listing::kContent1);
    y += 24.0f;
    const std::string mapped = "mapped by ";
    this->text(mapped, left, y, 14.0f, listing::kContent2);
    this->text(e.fCreator, left + this->measure(mapped, 14.0f, false), y, 14.0f,
               listing::kContent1, true);
    const std::string status = e.fStatus.empty() ? "unknown" : e.fStatus;
    const float pillW = this->measure(status, 11.0f, true) + 20.0f;
    const skia::SkRect pill =
        skia::SkRect::MakeXYWH(left, y + 8.0f, pillW, 18.0f);
    this->rounded(pill, 9.0f, listing::kColour3);
    this->textCentered(status, pill.centerX(), pill.fBottom - 5.0f, 11.0f,
                       listing::kBackground6, true);

    // Buttons: the preview toggle is the square one, then download.
    y += 40.0f;
    const skia::SkRect play = skia::SkRect::MakeXYWH(left, y, kButtonsHeight,
                                                     kButtonsHeight);
    this->rounded(play, 6.0f, listing::kBackground3);
    this->drawPreviewGlyph(play, ctx);
    fHits.push_back({play, Action::kPreview, 0});

    const skia::SkRect download = skia::SkRect::MakeXYWH(
        play.fRight + kButtonsSpacing, y, 240.0f, kButtonsHeight);
    const bool hover = download.contains(fMouseX, fMouseY + fScroll);
    const bool done = e.fSt == Entry::St::kDone;
    this->rounded(download, 6.0f,
                  done ? listing::kBackground3
                       : (hover ? listing::kColour1 : listing::kColour3));
    std::string label = "Download";
    if (e.fSt == Entry::St::kFetching) {
      label = std::format("Downloading {:.0f}%",
                          static_cast<double>(e.fProgress) * 100.0);
    } else if (done) {
      label = "In library";
    } else if (e.fVideo) {
      label = "Download with video";
    }
    this->textCentered(label, download.centerX(), download.centerY() + 6.0f,
                       16.0f, done ? listing::kContent2 : listing::kBackground6,
                       true);
    fHits.push_back({download, Action::kDownload, 0});
    y += kButtonsHeight;

    // The right column: BeatmapPicker's tiles over the set statistics.
    this->drawPicker(e, w);
    return std::max(height, y + kYPadding);
  }

  void drawPreviewGlyph(const skia::SkRect &box, const Ctx &ctx) {
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(listing::kContent1);
    const float cx = box.centerX();
    const float cy = box.centerY();
    if (ctx.fPreviewPlaying) {
      this->rect(skia::SkRect::MakeXYWH(cx - 7.0f, cy - 9.0f, 5.0f, 18.0f),
                 listing::kContent1);
      this->rect(skia::SkRect::MakeXYWH(cx + 2.0f, cy - 9.0f, 5.0f, 18.0f),
                 listing::kContent1);
      skia::SkPaint ring;
      ring.setAntiAlias(true);
      ring.setStyle(skia::kStrokeStyle);
      ring.setStrokeWidth(2.5f);
      ring.setColor(listing::kColour1);
      const float r = box.width() * 0.42f;
      fCanvas->drawArc(
          skia::SkRect::MakeXYWH(cx - r, cy - r, r * 2.0f, r * 2.0f), -90.0f,
          360.0f * std::clamp(ctx.fPreviewProgress, 0.0f, 1.0f), false, ring);
    } else {
      skia::SkPathBuilder tri;
      tri.moveTo(cx - 7.0f, cy - 10.0f)
          .lineTo(cx + 10.0f, cy)
          .lineTo(cx - 7.0f, cy + 10.0f)
          .close();
      fCanvas->drawPath(tri.detach(), paint);
    }
  }

  // BeatmapPicker: one 40px tile per difficulty, 2 apart, the selected one
  // named underneath, with the set's play and favourite counts above.
  void drawPicker(const Entry &e, float w) {
    const float right = w - kHorizontalPadding;
    const float x0 = right - kRightWidth;
    float y = kYPadding + 16.0f;

    this->text(std::format("{} plays    {} favourites", e.fPlayCount,
                           e.fFavouriteCount),
               x0, y, 12.0f, listing::kContent2);
    y += 18.0f;

    float x = x0;
    for (std::size_t i = 0; i < e.fDiffs.size(); ++i) {
      if (x + kTileSize > right) {
        x = x0;
        y += kTileSize + kTileSpacing;
      }
      const auto &diff = e.fDiffs[i];
      const skia::SkRect tile =
          skia::SkRect::MakeXYWH(x, y, kTileSize, kTileSize);
      const bool selected = static_cast<int>(i) == fSelected;
      const bool hover = tile.contains(fMouseX, fMouseY + fScroll);
      this->rounded(tile, 4.0f,
                    selected || hover ? listing::kBackground3
                                      : listing::kBackground5);
      this->circle(tile.centerX(), tile.centerY() - 3.0f, 8.0f,
                   client::ui::starColor(diff.fStars));
      this->textCentered(std::format("{:.1f}", diff.fStars), tile.centerX(),
                         tile.fBottom - 5.0f, 10.0f,
                         selected ? listing::kContent1 : listing::kContent2,
                         selected);
      fHits.push_back({tile, Action::kSelectDiff, static_cast<int>(i)});
      x += kTileSize + kTileSpacing;
    }
    y += kTileSize + 8.0f;
    if (!e.fDiffs.empty()) {
      const auto &diff = e.fDiffs[static_cast<std::size_t>(fSelected)];
      this->text(diff.fVersion, x0, y, 16.0f, listing::kContent1, true);
    }
  }

  // Info: the selected difficulty's numbers on the left, the metadata and the
  // ratings histogram in the right column, over Background5.
  float drawInfo(const Entry &e, float w, float top) {
    const float height = 330.0f;
    this->rect(skia::SkRect::MakeXYWH(0, top, w, height), listing::kBackground5);
    const float left = kHorizontalPadding;
    const float rightX = w - kHorizontalPadding - kRightWidth;
    const float leftW = rightX - left - 30.0f;
    float y = top + kYPadding + 8.0f;

    // The difficulty name over its own stats, as the picker's caption.
    if (!e.fDiffs.empty()) {
      const auto &diff = e.fDiffs[static_cast<std::size_t>(fSelected)];
      const std::string starText = std::format("{:.2f}", diff.fStars);
      const float starW = this->measure(starText, 12.0f, true) + 26.0f;
      const skia::SkRect star =
          skia::SkRect::MakeXYWH(left, y - 13.0f, starW, 18.0f);
      this->rounded(star, 9.0f, client::ui::starColor(diff.fStars));
      this->circle(star.fLeft + 10.0f, star.centerY(), 4.0f,
                   listing::kBackground6);
      this->text(starText, star.fLeft + 18.0f, y, 12.0f,
                 listing::kBackground6, true);
      this->text(diff.fVersion, star.fRight + 8.0f, y, 16.0f,
                 listing::kContent1, true);
      y += 26.0f;

      // BasicStats: a row of labelled numbers over their own strip.
      const skia::SkRect strip =
          skia::SkRect::MakeXYWH(left, y, leftW, 116.0f);
      this->rounded(strip, 6.0f, listing::kBackground4);
      const struct {
        const char *fLabel;
        std::string fValue;
        float fBar; // 0..1, drawn under the value like the difficulty bars
      } stats[] = {
          {"Length", formatLength(diff.fLengthMs), 0.0f},
          {"BPM", std::format("{:.0f}", e.fBpm), 0.0f},
          {"Max Combo", std::format("{}", diff.fMaxCombo), 0.0f},
          {"Circle Size", std::format("{:.1f}", diff.fCs),
           static_cast<float>(diff.fCs / 10.0)},
          {"HP Drain", std::format("{:.1f}", diff.fHp),
           static_cast<float>(diff.fHp / 10.0)},
          {"Accuracy", std::format("{:.1f}", diff.fOd),
           static_cast<float>(diff.fOd / 10.0)},
          {"Approach Rate", std::format("{:.1f}", diff.fAr),
           static_cast<float>(diff.fAr / 10.0)},
          {"Star Rating", std::format("{:.2f}", diff.fStars),
           static_cast<float>(std::min(1.0, diff.fStars / 10.0))},
      };
      const float cellW = (leftW - 24.0f) / 4.0f;
      float cx = left + 12.0f;
      float cy = y + 26.0f;
      int column = 0;
      for (const auto &stat : stats) {
        this->text(stat.fLabel, cx, cy, 11.0f, listing::kContent2);
        this->text(stat.fValue, cx, cy + 20.0f, 17.0f, listing::kContent1,
                   true);
        if (stat.fBar > 0.0f) {
          const skia::SkRect bar =
              skia::SkRect::MakeXYWH(cx, cy + 28.0f, cellW - 16.0f, 4.0f);
          this->rounded(bar, 2.0f, listing::kBackground6);
          this->rounded(skia::SkRect::MakeXYWH(bar.fLeft, bar.fTop,
                                               bar.width() * stat.fBar, 4.0f),
                        2.0f, listing::kColour1);
        }
        cx += cellW;
        if (++column == 4) {
          column = 0;
          cx = left + 12.0f;
          cy += 58.0f;
        }
      }
      y += 132.0f;
    }

    // Metadata, the way Info's MetadataSection stacks it.
    const struct {
      const char *fLabel;
      std::string fValue;
    } meta[] = {
        {"Source", e.fSource.empty() ? "-" : e.fSource},
        {"Genre", listing::kGenreLabels[genreIndex(e.fGenre)]},
        {"Language", listing::kLanguageLabels[languageIndex(e.fLanguage)]},
        {"Tags", e.fTags.empty() ? "-" : e.fTags},
        {"Last updated", e.fUpdated},
    };
    for (const auto &row : meta) {
      this->text(row.fLabel, left, y, 11.0f, listing::kContent2);
      fCanvas->save();
      fCanvas->clipIRect(skia::SkIRect::MakeXYWH(
          static_cast<int>(left + 110.0f), static_cast<int>(y - 14.0f),
          static_cast<int>(leftW - 110.0f), 20));
      this->text(row.fValue, left + 110.0f, y, 13.0f, listing::kContent1);
      fCanvas->restore();
      y += 22.0f;
    }

    this->drawRatings(e, rightX, top + kYPadding + 8.0f);
    return std::max(top + height, y + kYPadding);
  }

  // UserRatings: the 1..10 histogram with the positive share above it.
  void drawRatings(const Entry &e, float x, float y) {
    this->text("User Rating", x, y, 12.0f, listing::kContent2);
    y += 14.0f;
    std::vector<int> counts(10, 0);
    // osu! returns eleven buckets, the first unused.
    for (std::size_t i = 1; i < e.fRatings.size() && i <= 10; ++i) {
      counts[i - 1] = e.fRatings[i];
    }
    const int negative = counts[0] + counts[1] + counts[2] + counts[3] +
                         counts[4];
    const int positive = counts[5] + counts[6] + counts[7] + counts[8] +
                         counts[9];
    const int total = negative + positive;

    // The split bar: negative on the left, positive on the right.
    const skia::SkRect bar = skia::SkRect::MakeXYWH(x, y, kRightWidth, 5.0f);
    this->rounded(bar, 2.5f, skia::colorSetARGB(255, 255, 102, 102));
    if (total > 0) {
      const float share = static_cast<float>(positive) /
                          static_cast<float>(total);
      this->rounded(skia::SkRect::MakeXYWH(bar.fLeft + bar.width() *
                                                          (1.0f - share),
                                           bar.fTop, bar.width() * share, 5.0f),
                    2.5f, listing::kColour1);
    }
    y += 16.0f;
    this->text(std::format("{} negative", negative), x, y, 11.0f,
               listing::kContent2);
    const std::string pos = std::format("{} positive", positive);
    this->text(pos, x + kRightWidth - this->measure(pos, 11.0f, false), y,
               11.0f, listing::kContent2);
    y += 12.0f;

    // The histogram itself, tallest bucket filling the height.
    const float graphH = 70.0f;
    const float slot = kRightWidth / 10.0f;
    const int peak = std::max(1, *std::ranges::max_element(counts));
    for (std::size_t i = 0; i < counts.size(); ++i) {
      const float h = graphH * static_cast<float>(counts[i]) /
                      static_cast<float>(peak);
      const skia::SkRect column = skia::SkRect::MakeXYWH(
          x + static_cast<float>(i) * slot + 2.0f, y + graphH - h,
          slot - 4.0f, std::max(1.0f, h));
      this->rounded(column, 2.0f, listing::kColour1, 0.8f);
    }
    y += graphH + 6.0f;
    for (int i = 0; i < 10; ++i) {
      this->textCentered(std::format("{}", i + 1),
                         x + static_cast<float>(i) * slot + slot * 0.5f, y,
                         9.0f, listing::kContent2);
    }
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
  void circle(float cx, float cy, float r, skia::SkColor color) {
    skia::SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    fCanvas->drawCircle(cx, cy, r, p);
  }
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
    const float w = client::ui::fonts().measure(*fFont, s);
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
    client::ui::fonts().draw(fCanvas, *fFont, s, x, y, p);
    fFont->setEmbolden(false);
  }
  void textCentered(const std::string &s, float cx, float y, float size,
                    skia::SkColor color, bool bold = false) {
    this->text(s, cx - this->measure(s, size, bold) * 0.5f, y, size, color,
               bold);
  }

  long fSetId = -1;
  bool fOpen = false;
  int fSelected = 0;
  std::vector<Hit> fHits;
  skia::SkCanvas *fCanvas = nullptr;
  skia::SkFont *fFont = nullptr;
  float fMouseX = 0.0f, fMouseY = 0.0f;
  float fScroll = 0.0f, fScrollTarget = 0.0f, fMaxScroll = 0.0f;
};

} // namespace client::setpage
