export module client.results;

import std;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets.button;
import client.palette;

// lazer's ScorePanelList: a horizontal strip of score panels with one of them
// expanded. ScorePanel gives the sizes -- EXPANDED_WIDTH 360, CONTRACTED_WIDTH
// 130, CONTRACTED_HEIGHT 385 -- with 5px between panels and 15px extra either
// side of the expanded one.
//
// The strip does not know what a replay is, nor which beatmap it belongs to:
// it is handed the scores to show and says which of them was picked.
namespace paint = skiff::paint;

export namespace client::results {

// One panel's worth of score, whichever it came from.
struct Entry {
  bool fOwn = false;     // the run in hand, which has no file to play
  bool fHasScore = true; // a replay may carry no score in its header
  std::string fGrade;
  std::string fLabel; // difficulty_timestamp, as the file was saved
  std::uint64_t fTotal = 0;
  int f300 = 0, f100 = 0, f50 = 0, fMiss = 0, fCombo = 0;
  double fAccuracy = 1.0;
  // Hit error, UR and what the sliders did are only kept for this session,
  // so only the run in hand has them.
  bool fDetail = false;
  int fTickHit = 0, fTickTotal = 0, fTailHit = 0, fTailTotal = 0;
};

struct Ctx {
  skia::SkFont *fFont = nullptr;
  float fWidth = 0.0f;
  float fHeight = 0.0f;
  float fMouseX = 0.0f;
  float fMouseY = 0.0f;
  double fNowWall = 0.0;
  double fEnterWall = 0.0; // when the screen appeared; the score counts up
  double fDtMs = 16.0;
  // The beatmap every panel in the strip belongs to.
  std::string fTitle;
  std::string fArtist;
  std::string fVersion;
  double fStars = 0.0;
  bool fHasDifficulty = false;
  bool fOwnScore = false; // the strip leads with the run in hand
  // The run in hand, shown only on its own panel: an .osr stores none of
  // these three, so a replay's panel has nothing to put in their place.
  double fPp = 0.0;
  double fMean = 0.0;
  double fUr = 0.0;
};

// The actions below the score strip. They are a separate retained tree: the
// panels still contain score-specific drawing, while this row is ordinary UI
// whose flow, hover, hit routing and damage belong to widgets.
enum class Action : std::uint8_t {
  kNone,
  kRetry,
  kBack,
  kExport,
  kToggleRules,
};

struct ActionCtx {
  skia::SkFont *fFont = nullptr;
  float fWidth = 0.0f;
  float fHeight = 0.0f;
  float fMouseX = 0.0f;
  float fMouseY = 0.0f;
  double fNowMs = 0.0;
  bool fRulesAvailable = false;
  std::string fRulesLabel;
  bool fRulesEnabled = false;
};

namespace action_style {
struct Root;
struct Row;
struct Three;
struct Four;
struct Button;
} // namespace action_style

struct ActionTheme {
  static constexpr auto styles =
      skiff::scene::makeStyleSheet()
          .rule(skiff::scene::select<skiff::scene::Drawable,
                                     action_style::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = skiff::scene::Axes::kBoth})
          .rule(skiff::scene::select<skiff::nodes::FillFlow,
                                     action_style::Row>(),
                {.anchor = skiff::scene::Anchor::kBottomCentre,
                 .origin = skiff::scene::Anchor::kBottomCentre,
                 .y = -46.0f,
                 .height = 46.0f})
          .rule(skiff::scene::select<skiff::nodes::FillFlow,
                                     action_style::Three>(),
                {.width = 0.688f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .maxWidth = 808.0f})
          .rule(skiff::scene::select<skiff::nodes::FillFlow,
                                     action_style::Four>(),
                {.width = 0.922f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .maxWidth = 1082.0f})
          .rule(skiff::scene::select<skiff::widgets::Button,
                                     action_style::Button>(),
                {.height = 46.0f, .grow = skiff::scene::Axes::kX});
};

class Actions {
public:
  void update(const ActionCtx &ctx) {
    if (ctx.fFont == nullptr) {
      return;
    }
    skiff::nodes::Text::setFont(ctx.fFont);
    bool rebuilt = false;
    if (!fScene || ctx.fRulesAvailable != fRulesAvailable) {
      fRulesAvailable = ctx.fRulesAvailable;
      fScene = this->build();
      rebuilt = true;
    }
    if (fRulesButton != nullptr) {
      fRulesButton->setLabel(ctx.fRulesLabel);
      fRulesButton->setAccent(
          ctx.fRulesEnabled ? palette::kAccent2
                            : skia::colorSetARGB(255, 120, 120, 130));
    }

    fScene->updateTree(ctx.fNowMs);
    fScene->layoutIfNeeded(
        skia::SkRect::MakeWH(ctx.fWidth, ctx.fHeight));
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

  [[nodiscard]] skiff::scene::FrameResult finishFrame() {
    return fScene ? fScene->finishFrame() : skiff::scene::FrameResult{};
  }
  [[nodiscard]] skiff::scene::Drawable *sceneRoot() noexcept {
    return fScene.get();
  }

  [[nodiscard]] Action click(float x, float y) {
    fPressed = Action::kNone;
    if (fScene) {
      fScene->dispatchPointer(skiff::scene::PointerAction::kDown, x, y);
    }
    return fPressed;
  }

private:
  inline static const skiff::widgets::Theme kButtonTheme = {
      .fSurface = palette::kCardBg,
      .fSurfaceHover = palette::kCardSel,
      .fSurfaceActive = palette::kCardSel,
      .fText = skia::kWhite,
      .fLabel = skia::kWhite,
      .fTextDim = skia::kWhite,
      .fTextFaint = skia::kWhite,
      .fAccent = palette::kAccent2,
      .fOnAccent = skia::kWhite,
      .fCorner = 12.0f,
      .fFontSize = 20.0f,
      .fRowHeight = 46.0f,
      .fPaddingX = 12.0f,
  };

  skiff::widgets::Button *addButton(skiff::nodes::FillFlow &row,
                                    std::string label, skia::SkColor accent,
                                    Action action) {
    auto *button = row.add<skiff::widgets::Button>(
        {.roles = {skiff::scene::role<action_style::Button>}},
        std::move(label), [this, action] { fPressed = action; });
    button->setTheme(kButtonTheme);
    button->setAccent(accent);
    button->setOutlined(true);
    return button;
  }

  [[nodiscard]] std::unique_ptr<skiff::scene::Drawable> build() {
    fRulesButton = nullptr;
    auto root = skiff::scene::make<skiff::scene::Drawable>(
        {.roles = {skiff::scene::role<action_style::Root>}});
    auto *row = root->add<skiff::nodes::FillFlow>(
        {.roles = {skiff::scene::role<action_style::Row>,
                   fRulesAvailable
                       ? skiff::scene::role<action_style::Four>
                       : skiff::scene::role<action_style::Three>}},
        skiff::nodes::FillFlow::Direction::kHorizontal, 14.0f, 0.0f);
    row->setWrap(false);
    this->addButton(*row, "retry", skia::colorSetARGB(255, 255, 204, 102),
                    Action::kRetry);
    this->addButton(*row, "back to song select", palette::kAccent2,
                    Action::kBack);
    this->addButton(*row, "export video",
                    skia::colorSetARGB(255, 170, 102, 255), Action::kExport);
    if (fRulesAvailable) {
      fRulesButton = this->addButton(*row, "", palette::kAccent2,
                                     Action::kToggleRules);
    }
    root->setStyleSheet<ActionTheme>();
    return root;
  }

  bool fRulesAvailable = false;
  Action fPressed = Action::kNone;
  std::unique_ptr<skiff::scene::Drawable> fScene;
  skiff::widgets::Button *fRulesButton = nullptr;
};

class Panels {
public:
  // What a press did. The client owns what follows from it: re-reading the
  // rules a replay was recorded under, or playing it back.
  enum class Hit : std::uint8_t {
    kNone,      // not over the strip
    kTaken,     // the strip used it, nothing else to do
    kSelected,  // a different panel is expanded now
    kActivated, // the expanded panel was pressed again
  };

  void setEntries(std::vector<Entry> entries) {
    fEntries = std::move(entries);
    fSelected = std::clamp(fSelected, 0,
                           std::max(0, static_cast<int>(fEntries.size()) - 1));
  }

  [[nodiscard]] int selected() const { return fSelected; }
  void select(int index) {
    fSelected = index;
    fFreeScroll = false;
  }
  [[nodiscard]] const skia::SkRect &band() const { return fBand; }
  [[nodiscard]] bool scrolling() const {
    return !paint::settled(fScroll, fScrollTarget);
  }
  // Where the strip was when it was last drawn. A drag sets the position
  // outright, so the target says nothing about whether it moved.
  [[nodiscard]] bool movedSinceDrawn() const { return fScroll != fDrawnScroll; }
  void noteDrawn() { fDrawnScroll = fScroll; }
  void scrollBy(float delta) {
    fScrollTarget -= delta;
    fFreeScroll = true;
  }
  void clear() {
    fHits.clear();
    fBand = skia::SkRect::MakeEmpty();
  }

  void render(skia::SkCanvas *canvas, const Ctx &ctx) {
    const paint::Painter p(canvas, *ctx.fFont);
    const float sw = ctx.fWidth;
    const float sh = ctx.fHeight;
    fHits.clear();

    // Native size unless the window cannot fit it; the panels are meant to be
    // read at a glance, so they scale up on a large window rather than sit
    // tiny in the middle of it.
    const float scale =
        std::clamp(std::min((sh - 250.0f) / (kPanelContractedH + 60.0f),
                            sw / 1280.0f * 1.35f),
                   0.6f, 1.6f);
    const float contractedW = kPanelContractedW * scale;
    const float expandedW = kPanelExpandedW * scale;
    const float panelH = kPanelContractedH * scale;
    const float expandedH = panelH + 70.0f * scale;
    const float spacing = kPanelSpacing * scale;
    const float expandedGap = kExpandedSpacing * scale;
    const float cy = sh * 0.47f;
    fBand = skia::SkRect::MakeLTRB(0.0f, cy - expandedH * 0.5f - 10.0f, sw,
                                   cy + expandedH * 0.5f + 10.0f);

    const int count = static_cast<int>(fEntries.size());
    if (count == 0) {
      fBand = skia::SkRect::MakeEmpty();
      p.textCentered("no replays saved for this difficulty", sw * 0.5f,
                     sh * 0.5f, 18.0f, skia::kWhite, 0.6f);
      return;
    }
    fSelected = std::clamp(fSelected, 0, count - 1);

    // Lay the strip out, then scroll so the expanded panel sits centred --
    // unless the user has dragged or wheeled away from it.
    std::vector<float> widths(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      widths[static_cast<std::size_t>(i)] =
          i == fSelected ? expandedW : contractedW;
    }
    float centreOffset = 0.0f;
    for (int i = 0; i < fSelected; ++i) {
      centreOffset += widths[static_cast<std::size_t>(i)] + spacing;
    }
    centreOffset += expandedGap + expandedW * 0.5f;
    if (!fFreeScroll) {
      fScrollTarget = centreOffset - sw * 0.5f;
    }
    fScroll = paint::approach(fScroll, fScrollTarget, 70.0f, ctx.fDtMs);

    float x = -fScroll;
    for (int i = 0; i < count; ++i) {
      const bool expanded = i == fSelected;
      if (expanded) {
        x += expandedGap;
      }
      const float w = widths[static_cast<std::size_t>(i)];
      const float h = expanded ? expandedH : panelH;
      const skia::SkRect r = skia::SkRect::MakeXYWH(x, cy - h * 0.5f, w, h);
      const Entry &entry = fEntries[static_cast<std::size_t>(i)];
      if (r.fRight > -60.0f && r.fLeft < sw + 60.0f) {
        if (expanded) {
          this->drawExpanded(canvas, p, ctx, r, scale, entry);
        } else {
          this->drawContracted(p, ctx, r, entry, scale);
        }
      }
      fHits.push_back({r, i});
      x += w + spacing + (expanded ? expandedGap : 0.0f);
    }

    p.textCentered(ctx.fOwnScore
                       ? "click a panel to view it    drag to scroll"
                       : "click to select, click again to watch    Esc closes",
                   sw * 0.5f, sh - 44.0f, 13.0f, skia::kWhite, 0.6f);
  }

  // Press starts a drag anywhere over the strip; release either resolves the
  // drag or, if the pointer barely moved, counts as a click on a panel.
  [[nodiscard]] Hit click(float x, float y, bool pressed) {
    if (pressed) {
      if (!fBand.contains(x, y)) {
        return Hit::kNone;
      }
      fDragging = true;
      fDragged = false;
      fDragOrigin = x;
      fScrollOrigin = fScroll;
      return Hit::kTaken;
    }
    if (!fDragging) {
      return Hit::kNone;
    }
    const bool dragged = fDragged;
    fDragging = false;
    fDragged = false;
    if (dragged) {
      return Hit::kTaken;
    }
    for (const auto &hit : fHits) {
      if (!hit.fRect.contains(x, y)) {
        continue;
      }
      if (hit.fIndex == fSelected) {
        // The expanded panel is already selected; activating it plays the
        // replay it stands for. The score in hand has none to play.
        return Hit::kActivated;
      }
      fSelected = hit.fIndex;
      fFreeScroll = false; // re-centre on the new selection
      return Hit::kSelected;
    }
    return Hit::kTaken; // a press that landed on the strip is ours either way
  }

  void drag(float x) {
    const float delta = fDragOrigin - x;
    if (std::abs(delta) > 4.0f) {
      fDragged = true;
      fFreeScroll = true;
    }
    fScrollTarget = fScrollOrigin + delta;
    fScroll = fScrollTarget;
  }

  [[nodiscard]] bool dragging() const { return fDragging; }

  // Which panel the pointer is over, or -1. The strip lights nothing itself;
  // the client repaints the band when the answer changes.
  [[nodiscard]] int hot(float x, float y) const {
    for (const auto &hit : fHits) {
      if (hit.fRect.contains(x, y)) {
        return hit.fIndex;
      }
    }
    return -1;
  }

  [[nodiscard]] std::size_t count() const { return fEntries.size(); }

private:
  // ScorePanel's own sizes.
  static constexpr float kPanelExpandedW = 360.0f;
  static constexpr float kPanelContractedW = 130.0f;
  static constexpr float kPanelContractedH = 385.0f;
  static constexpr float kPanelSpacing = 5.0f;
  static constexpr float kExpandedSpacing = 15.0f;

  struct PanelHit {
    skia::SkRect fRect;
    int fIndex;
  };

  void drawContracted(const paint::Painter &p, const Ctx &ctx,
                      const skia::SkRect &r, const Entry &entry, float scale) {
    const bool hover = r.contains(ctx.fMouseX, ctx.fMouseY);
    const float h = r.height();
    p.fillRounded(r, 10.0f * scale,
                  hover ? palette::kCardSel : palette::kBackground4);

    // ContractedPanelMiddleContent: rank, total score, accuracy, combo, then
    // the date the score was set at the bottom.
    if (entry.fHasScore) {
      p.textCentered(entry.fGrade, r.centerX(), r.fTop + h * 0.17f,
                     46.0f * scale, palette::kAccent);
      p.textCentered(std::format("{}", entry.fTotal), r.centerX(),
                     r.fTop + h * 0.32f, 19.0f * scale, skia::kWhite);
      p.textCentered(std::format("{:.2f}%", entry.fAccuracy * 100.0),
                     r.centerX(), r.fTop + h * 0.40f, 14.0f * scale,
                     palette::kAccent2, 0.95f);
      p.textCentered(std::format("{}x", entry.fCombo), r.centerX(),
                     r.fTop + h * 0.47f, 14.0f * scale, skia::kWhite, 0.75f);
    } else {
      p.textCentered("replay", r.centerX(), r.fTop + h * 0.20f, 18.0f * scale,
                     palette::kAccent2, 0.9f);
      p.textCentered("no score stored", r.centerX(), r.fTop + h * 0.28f,
                     11.0f * scale, skia::kWhite, 0.5f);
    }

    if (entry.fOwn) {
      p.textCentered("this play", r.centerX(), r.fBottom - 18.0f * scale,
                     11.0f * scale, palette::kAccent2, 0.8f);
      return;
    }

    // The stem carries the difficulty and the timestamp it was saved with.
    const auto underscore = entry.fLabel.rfind('_');
    const std::string diff = underscore == std::string::npos
                                 ? entry.fLabel
                                 : entry.fLabel.substr(0, underscore);
    const std::string when = underscore == std::string::npos
                                 ? std::string{}
                                 : entry.fLabel.substr(underscore + 1);
    p.textClipped(diff, r.fLeft + 8.0f * scale, r.fTop + h * 0.66f,
                  r.width() - 16.0f * scale, 13.0f * scale, skia::kWhite,
                  0.95f);
    p.textCentered(when, r.centerX(), r.fBottom - 18.0f * scale, 11.0f * scale,
                   skia::kWhite, 0.55f);
  }

  void drawExpanded(skia::SkCanvas *canvas, const paint::Painter &p,
                    const Ctx &ctx, const skia::SkRect &panel, float scale,
                    const Entry &entry) {
    p.fillRounded(panel, 20.0f * scale, palette::kBackground5);

    float y = panel.fTop + 34.0f * scale;
    p.textCenteredClipped(ctx.fTitle, panel.centerX(), y,
                          panel.width() - 32.0f * scale, 20.0f * scale,
                          skia::kWhite);
    y += 24.0f * scale;
    p.textCenteredClipped(ctx.fArtist, panel.centerX(), y,
                          panel.width() - 32.0f * scale, 14.0f * scale,
                          skia::kWhite, 0.8f);
    y += 30.0f * scale;

    const float circleR = 100.0f * scale;
    const float ccy = y + circleR;
    this->drawAccuracyCircle(canvas, ctx, panel.centerX(), ccy, circleR,
                             entry.fAccuracy, entry.fGrade);
    y = ccy + circleR + 24.0f * scale;

    // The score counts up as the panel appears; a replay's stored score is
    // shown outright.
    std::uint64_t shownScore = entry.fTotal;
    if (entry.fOwn) {
      const float countUp = paint::outQuint(
          static_cast<float>((ctx.fNowWall - ctx.fEnterWall) / 900.0));
      shownScore = static_cast<std::uint64_t>(
          static_cast<double>(entry.fTotal) * countUp);
    }
    p.textCentered(std::format("{:07}", shownScore), panel.centerX(), y,
                   40.0f * scale, skia::kWhite);
    y += 24.0f * scale;

    if (ctx.fHasDifficulty) {
      const skia::SkRect chip = skia::SkRect::MakeXYWH(
          panel.centerX() - 88.0f * scale, y - 11.0f * scale, 60.0f * scale,
          22.0f * scale);
      p.fillRounded(chip, 11.0f * scale, palette::starColor(ctx.fStars));
      p.textCentered(std::format("{:.2f}", ctx.fStars), chip.centerX(),
                     chip.centerY() + 5.0f * scale, 12.0f * scale,
                     skia::colorSetARGB(255, 20, 16, 26));
      p.textClipped(ctx.fVersion, chip.fRight + 10.0f * scale, y + 5.0f * scale,
                    150.0f * scale, 13.0f * scale, skia::kWhite, 0.9f);
      y += 30.0f * scale;
    }

    struct Stat {
      const char *fLabel;
      std::string fValue;
      skia::SkColor fColor;
    };
    const Stat top[] = {
        {"300", std::format("{}", entry.f300), palette::kGreat},
        {"100", std::format("{}", entry.f100), palette::kGood},
        {"50", std::format("{}", entry.f50), palette::kMeh},
        {"miss", std::format("{}", entry.fMiss), palette::kMiss},
    };
    const float cellW = (panel.width() - 32.0f * scale) / 4.0f;
    float cx = panel.fLeft + 16.0f * scale;
    for (const auto &st : top) {
      p.textCentered(st.fLabel, cx + cellW * 0.5f, y, 11.0f * scale, st.fColor);
      p.textCentered(st.fValue, cx + cellW * 0.5f, y + 20.0f * scale,
                     18.0f * scale, skia::kWhite);
      cx += cellW;
    }
    y += 44.0f * scale;

    std::vector<Stat> bottom{
        {"combo", std::format("{}x", entry.fCombo), skia::kWhite},
        {"accuracy", std::format("{:.2f}%", entry.fAccuracy * 100.0),
         skia::kWhite},
    };
    if (entry.fDetail) {
      bottom.push_back(
          {"pp", std::format("{:.0f}", ctx.fPp), palette::kAccent});
      bottom.push_back(
          {"hit error", std::format("{:+.1f}ms", ctx.fMean), skia::kWhite});
      bottom.push_back({"UR", std::format("{:.0f}", ctx.fUr), skia::kWhite});
      // What the sliders did, which lazer keeps out of the 300/100/50 counts
      // and reports on its own.
      if (entry.fTickTotal > 0) {
        bottom.push_back(
            {"slider ticks",
             std::format("{}/{}", entry.fTickHit, entry.fTickTotal),
             entry.fTickHit == entry.fTickTotal ? skia::kWhite
                                                : palette::kMiss});
      }
      if (entry.fTailTotal > 0) {
        bottom.push_back(
            {"slider ends",
             std::format("{}/{}", entry.fTailHit, entry.fTailTotal),
             entry.fTailHit == entry.fTailTotal ? skia::kWhite
                                                : palette::kMiss});
      }
    } else if (!entry.fOwn) {
      // A stored replay keeps no hit statistics, only when it was played.
      const auto underscore = entry.fLabel.rfind('_');
      bottom.push_back({"played",
                        underscore == std::string::npos
                            ? std::string{"-"}
                            : entry.fLabel.substr(underscore + 1),
                        skia::kWhite});
    }
    const float bottomW =
        (panel.width() - 32.0f * scale) / static_cast<float>(bottom.size());
    cx = panel.fLeft + 16.0f * scale;
    for (const auto &st : bottom) {
      p.textCentered(st.fLabel, cx + bottomW * 0.5f, y, 11.0f * scale,
                     skia::kWhite, 0.55f);
      p.textCentered(st.fValue, cx + bottomW * 0.5f, y + 18.0f * scale,
                     15.0f * scale, st.fColor);
      cx += bottomW;
    }

    if (!entry.fOwn) {
      p.textCentered("click to watch this replay", panel.centerX(),
                     panel.fBottom - 16.0f * scale, 12.0f * scale,
                     palette::kAccent2, 0.85f);
    }
  }

  // AccuracyCircle: a grey backing ring, the graded arcs (D/C/B/A/S/SS at
  // their accuracy cutoffs), the achieved accuracy drawn over them, and the
  // rank letter in the middle. Cutoffs are lazer's standard ones.
  void drawAccuracyCircle(skia::SkCanvas *canvas, const Ctx &ctx, float cx,
                          float cy, float r, double accuracy,
                          const std::string &grade) {
    const paint::Painter p(canvas, *ctx.fFont);
    const float thickness = r * 0.2f; // accuracy_circle_radius
    const skia::SkRect bounds =
        skia::SkRect::MakeXYWH(cx - r, cy - r, r * 2.0f, r * 2.0f);

    skia::SkPaint arc;
    arc.setAntiAlias(true);
    arc.setStyle(skia::kStrokeStyle);
    arc.setStrokeWidth(thickness);
    arc.setStrokeCap(skia::kButtCap);

    // Backing ring, OsuColour.Gray(47).
    arc.setColor(skia::colorSetARGB(255, 47, 47, 47));
    canvas->drawArc(bounds, -90.0f, 360.0f, false, arc);

    // Graded segments: each rank owns the span from its cutoff to the next.
    struct Grade {
      double fFrom, fTo;
      skia::SkColor fColor;
    };
    const Grade grades[] = {
        {0.0, 0.60, skia::colorSetARGB(255, 0xff, 0x54, 0x5a)},  // D
        {0.60, 0.70, skia::colorSetARGB(255, 0xff, 0xa0, 0x55)}, // C
        {0.70, 0.80, skia::colorSetARGB(255, 0xff, 0xdd, 0x55)}, // B
        {0.80, 0.90, skia::colorSetARGB(255, 0x88, 0xdd, 0x20)}, // A
        {0.90, 0.95, skia::colorSetARGB(255, 0x02, 0xb8, 0xd7)}, // S
        {0.95, 1.00, skia::colorSetARGB(255, 0xde, 0x31, 0xae)}, // SS
    };
    skia::SkPaint graded;
    graded.setAntiAlias(true);
    graded.setStyle(skia::kStrokeStyle);
    graded.setStrokeWidth(thickness * 0.45f);
    graded.setStrokeCap(skia::kButtCap);
    const float gradedR = r - thickness * 0.78f;
    const skia::SkRect gradedBounds = skia::SkRect::MakeXYWH(
        cx - gradedR, cy - gradedR, gradedR * 2.0f, gradedR * 2.0f);
    for (const auto &g : grades) {
      graded.setColor(g.fColor);
      const float from = -90.0f + static_cast<float>(g.fFrom) * 360.0f;
      const float sweep = static_cast<float>(g.fTo - g.fFrom) * 360.0f - 1.5f;
      canvas->drawArc(gradedBounds, from, sweep, false, graded);
    }

    // Achieved accuracy, animated in with the panel.
    const float progress = paint::outQuint(
        static_cast<float>((ctx.fNowWall - ctx.fEnterWall) / 1400.0));
    arc.setColor(skia::kWhite);
    canvas->drawArc(bounds, -90.0f,
                    static_cast<float>(accuracy) * 360.0f * progress, false,
                    arc);

    // Rank badge in the middle.
    p.textCentered(grade, cx, cy + r * 0.28f, r * 0.72f, palette::kAccent);
    p.textCentered(std::format("{:.2f}%", accuracy * 100.0), cx, cy + r * 0.62f,
                   r * 0.16f, skia::kWhite, 0.85f);
  }

  std::vector<Entry> fEntries;
  std::vector<PanelHit> fHits;
  skia::SkRect fBand = skia::SkRect::MakeEmpty();
  int fSelected = 0;
  float fScroll = 0.0f;
  float fScrollTarget = 0.0f;
  float fScrollOrigin = 0.0f;
  float fDrawnScroll = 0.0f;
  float fDragOrigin = 0.0f;
  bool fDragging = false;
  bool fDragged = false;
  bool fFreeScroll = false; // user dragged or wheeled away from centre
};

} // namespace client::results
