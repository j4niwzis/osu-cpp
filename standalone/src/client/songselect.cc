export module client.songselect;

import std;
import osu;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets.button;
import skiff.widgets.dropdown;
import client.palette;

export namespace client::songselect {

enum class Action : std::uint8_t {
  kNone,
  kTaken,
  kBack,
  kMods,
  kRandom,
  kImport,
  kBrowse,
  kReplays,
  kDelete,
  kSettings,
};

struct Ctx {
  skia::SkFont *fFont = nullptr;
  float fWidth = 0.0f;
  float fHeight = 0.0f;
  float fMouseX = 0.0f;
  float fMouseY = 0.0f;
  double fNowMs = 0.0;
};

namespace style {
struct Root;
struct Bar;
struct Back;
struct Actions;
struct ActionButton;
struct Options;
struct WedgeRoot;
struct WedgePlate;
struct Title;
struct Artist;
struct Mapper;
struct Stats;
struct Length;
struct Difficulties;
struct Difficulty;
} // namespace style

namespace detail {

inline constexpr float kWedgeTop = 32.0f;
inline constexpr float kWedgePlateHeight = 168.0f;
inline constexpr float kWedgeShear = 22.0f;
inline constexpr float kWedgeContentHeight = 202.0f;

class WedgePlate : public skiff::scene::TypedDrawable<WedgePlate> {
protected:
  void measure(const skia::SkRect &parent) override {
    fWidth = std::min(560.0f, parent.width() * 0.44f) + kWedgeShear;
  }

  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    const float bottom = fBounds.fTop + kWedgePlateHeight;
    skia::SkPathBuilder path;
    path.moveTo(fBounds.fLeft, fBounds.fTop);
    path.lineTo(fBounds.fRight, fBounds.fTop);
    path.lineTo(fBounds.fRight - kWedgeShear, bottom);
    path.lineTo(fBounds.fLeft, bottom);
    path.close();
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(palette::kPanelBg);
    paint.setAlphaf(skiff::paint::combinedAlpha(palette::kPanelBg, alpha));
    canvas->drawPath(path.detach(), paint);
  }
};

class DifficultyDot : public skiff::scene::TypedDrawable<DifficultyDot> {
public:
  explicit DifficultyDot(skia::SkColor colour) : fColour(colour) {}

  void setColour(skia::SkColor colour) {
    if (colour == fColour) {
      return;
    }
    fColour = colour;
    this->markDamaged();
  }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    const float radius = this->selected() ? 9.0f : 6.0f;
    const float centreX = fBounds.fLeft + 12.0f;
    skia::SkPaint dot;
    dot.setAntiAlias(true);
    dot.setColor(fColour);
    dot.setAlphaf(skiff::paint::combinedAlpha(fColour, alpha));
    canvas->drawCircle(centreX, fBounds.centerY(), radius, dot);
    if (!this->selected()) {
      return;
    }
    skia::SkPaint ring;
    ring.setAntiAlias(true);
    ring.setStyle(skia::kStrokeStyle);
    ring.setStrokeWidth(2.0f);
    ring.setColor(skia::kWhite);
    ring.setAlphaf(skiff::paint::combinedAlpha(skia::kWhite, alpha));
    canvas->drawCircle(centreX, fBounds.centerY(), 12.0f, ring);
  }

private:
  skia::SkColor fColour;
};

} // namespace detail

struct InfoWedgeTheme {
  static constexpr auto styles =
      skiff::scene::makeStyleSheet()
          .rule(skiff::scene::select<skiff::scene::Drawable,
                                     style::WedgeRoot>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = skiff::scene::Axes::kBoth})
          .rule(skiff::scene::select<detail::WedgePlate,
                                     style::WedgePlate>(),
                {.y = detail::kWedgeTop,
                 .height = detail::kWedgeContentHeight,
                 .padding = skiff::scene::Margin{
                     0.0f, 28.0f + detail::kWedgeShear, 0.0f, 28.0f}})
          .rule(skiff::scene::select<skiff::nodes::Text, style::Title>(),
                {.y = 14.0f,
                 .width = 1.0f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .colour = skia::kWhite,
                 .fontSize = 32.0f})
          .rule(skiff::scene::select<skiff::nodes::Text, style::Artist>(),
                {.y = 58.0f,
                 .width = 1.0f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .alpha = 0.8f,
                 .colour = skia::kWhite,
                 .fontSize = 18.0f})
          .rule(skiff::scene::select<skiff::nodes::Text, style::Mapper>(),
                {.y = 85.0f,
                 .width = 1.0f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .alpha = 0.9f,
                 .colour = palette::kAccent2,
                 .fontSize = 15.0f})
          .rule(skiff::scene::select<skiff::nodes::Text, style::Stats>(),
                {.y = 113.0f,
                 .width = 1.0f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .fontSize = 15.0f})
          .rule(skiff::scene::select<skiff::nodes::Text, style::Length>(),
                {.y = 138.0f,
                 .width = 1.0f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .alpha = 0.7f,
                 .colour = skia::kWhite,
                 .fontSize = 14.0f})
          .rule(skiff::scene::select<skiff::nodes::FillFlow,
                                     style::Difficulties>(),
                {.x = -6.0f,
                 .y = 178.0f,
                 .width = 1.0f,
                 .height = 24.0f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .margin = skiff::scene::Margin{0.0f, -18.0f, 0.0f, 0.0f},
                 .masking = true})
          .rule(skiff::scene::select<detail::DifficultyDot,
                                     style::Difficulty>(),
                {.width = 22.0f, .height = 24.0f})
          .rule(skiff::scene::select<detail::DifficultyDot,
                                     style::Difficulty>()
                    .when(skiff::scene::StyleState::kSelected),
                {.width = 30.0f});
};

class InfoWedge {
public:
  struct Ctx {
    skia::SkFont *fFont = nullptr;
    float fWidth = 0.0f;
    float fHeight = 0.0f;
    int fSet = -1;
    int fDifficulty = -1;
    bool fRankedStars = false;
    std::span<const osu::BeatmapInfo> fInfos;
  };

  void update(const Ctx &ctx) {
    if (ctx.fFont == nullptr || ctx.fInfos.empty()) {
      return;
    }
    skiff::nodes::Text::setFont(ctx.fFont);
    bool rebuilt = false;
    if (!fScene) {
      fScene = this->build();
      rebuilt = true;
    }

    const int selected = std::clamp(
        ctx.fDifficulty, 0, static_cast<int>(ctx.fInfos.size()) - 1);
    const DataKey key{ctx.fSet, selected, ctx.fRankedStars, ctx.fInfos.data(),
                      ctx.fInfos.size()};
    if (key != fDataKey) {
      this->setData(ctx.fInfos, selected, ctx.fRankedStars);
      fDataKey = key;
    }

    fScene->layoutIfNeeded(skia::SkRect::MakeWH(ctx.fWidth, ctx.fHeight));
    // A viewport restyle reapplies the static text declarations. The rating
    // colour is data, so restore it after that cascade has run.
    fStats->setColour(fStatsColour);
    if (rebuilt) {
      fScene->markDamaged();
    }
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] skia::SkRect takeDamage() {
    return fScene ? fScene->takeDamage() : skia::SkRect::MakeEmpty();
  }

private:
  struct DataKey {
    int fSet = -1;
    int fDifficulty = -1;
    bool fRankedStars = false;
    const osu::BeatmapInfo *fInfos = nullptr;
    std::size_t fCount = 0;
    [[nodiscard]] bool operator==(const DataKey &) const = default;
  };

  [[nodiscard]] static double shownStars(const osu::BeatmapInfo &info,
                                         bool ranked) {
    return ranked ? info.fStarsRanked : info.fStars;
  }

  void setData(std::span<const osu::BeatmapInfo> infos, int selected,
               bool ranked) {
    const auto &info = infos[static_cast<std::size_t>(selected)];
    const auto &meta = infos.front().fMeta;
    const double stars = shownStars(info, ranked);
    fTitle->setText(meta.fTitleUnicode.empty() ? meta.fTitle
                                               : meta.fTitleUnicode);
    fArtist->setText(meta.fArtistUnicode.empty() ? meta.fArtist
                                                 : meta.fArtistUnicode);
    fMapper->setText(std::format("mapped by {}", info.fMeta.fCreator));
    fStats->setText(std::format(
        "{}   {:.2f}*   CS {:.1f}  AR {:.1f}  OD {:.1f}  HP {:.1f}",
        info.fMeta.fVersion, stars, info.fDiff.fCs, info.fDiff.fAr,
        info.fDiff.fOd, info.fDiff.fHp));
    fStatsColour = palette::starColor(stars);
    fStats->setColour(fStatsColour);
    const auto seconds = static_cast<std::int64_t>(
        std::max(0.0, info.fLengthMs) / 1000.0);
    fLength->setText(std::format("{} objects   {}:{:02}", info.fObjectCount,
                                 seconds / 60, seconds % 60));

    if (fDots.size() != infos.size()) {
      fDifficulties->clear();
      fDots.clear();
      fDots.reserve(infos.size());
      for (const auto &difficulty : infos) {
        fDots.push_back(fDifficulties->add<detail::DifficultyDot>(
            {.roles = {skiff::scene::role<style::Difficulty>}},
            palette::starColor(shownStars(difficulty, ranked))));
      }
      fDifficulties->invalidateLayout();
    }
    for (std::size_t i = 0; i < infos.size(); ++i) {
      fDots[i]->setColour(palette::starColor(shownStars(infos[i], ranked)));
      fDots[i]->setSelected(static_cast<int>(i) == selected);
    }
  }

  [[nodiscard]] std::unique_ptr<skiff::scene::Drawable> build() {
    auto root = skiff::scene::make<skiff::scene::Drawable>(
        {.roles = {skiff::scene::role<style::WedgeRoot>}});
    auto *plate = root->add<detail::WedgePlate>(
        {.roles = {skiff::scene::role<style::WedgePlate>}});
    fTitle = plate->add<skiff::nodes::Text>(
        {.roles = {skiff::scene::role<style::Title>}}, "", 32.0f,
        skia::kWhite);
    fArtist = plate->add<skiff::nodes::Text>(
        {.roles = {skiff::scene::role<style::Artist>}}, "", 18.0f,
        skia::kWhite);
    fMapper = plate->add<skiff::nodes::Text>(
        {.roles = {skiff::scene::role<style::Mapper>}}, "", 15.0f,
        palette::kAccent2);
    fStats = plate->add<skiff::nodes::Text>(
        {.roles = {skiff::scene::role<style::Stats>}}, "", 15.0f,
        skia::kWhite);
    fLength = plate->add<skiff::nodes::Text>(
        {.roles = {skiff::scene::role<style::Length>}}, "", 14.0f,
        skia::kWhite);
    fDifficulties = plate->add<skiff::nodes::FillFlow>(
        {.roles = {skiff::scene::role<style::Difficulties>}},
        skiff::nodes::FillFlow::Direction::kHorizontal, 0.0f, 0.0f);
    fDifficulties->fWrap = false;
    root->setStyleSheet<InfoWedgeTheme>();
    return root;
  }

  DataKey fDataKey;
  std::unique_ptr<skiff::scene::Drawable> fScene;
  skiff::nodes::Text *fTitle = nullptr;
  skiff::nodes::Text *fArtist = nullptr;
  skiff::nodes::Text *fMapper = nullptr;
  skiff::nodes::Text *fStats = nullptr;
  skiff::nodes::Text *fLength = nullptr;
  skia::SkColor fStatsColour = skia::kWhite;
  skiff::nodes::FillFlow *fDifficulties = nullptr;
  std::vector<detail::DifficultyDot *> fDots;
};

struct FooterTheme {
  static constexpr auto styles =
      skiff::scene::makeStyleSheet()
          .rule(skiff::scene::select<skiff::scene::Drawable, style::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = skiff::scene::Axes::kBoth})
          .rule(skiff::scene::select<skiff::nodes::Box, style::Bar>(),
                {.anchor = skiff::scene::Anchor::kBottomLeft,
                 .origin = skiff::scene::Anchor::kBottomLeft,
                 .width = 1.0f,
                 .height = 60.0f,
                 .relativeSize = skiff::scene::Axes::kX,
                 .backgroundColour = palette::kBackground5})
          .rule(skiff::scene::select<skiff::widgets::Button, style::Back>(),
                {.anchor = skiff::scene::Anchor::kBottomLeft,
                 .origin = skiff::scene::Anchor::kBottomLeft,
                 .x = 24.0f,
                 .y = -12.0f,
                 .width = 100.0f,
                 .height = 34.0f})
          .rule(skiff::scene::select<skiff::nodes::FillFlow,
                                     style::Actions>(),
                {.anchor = skiff::scene::Anchor::kBottomCentre,
                 .origin = skiff::scene::Anchor::kBottomCentre,
                 .y = -12.0f,
                 .width = 440.0f,
                 .height = 36.0f})
          .rule(skiff::scene::select<skiff::widgets::Button,
                                     style::ActionButton>(),
                {.height = 36.0f, .grow = skiff::scene::Axes::kX})
          .rule(skiff::scene::select<skiff::widgets::DropdownList,
                                     style::Options>(),
                {.anchor = skiff::scene::Anchor::kTopCentre,
                 .origin = skiff::scene::Anchor::kBottomCentre,
                 .y = -20.0f,
                 .width = 220.0f,
                 .depth = 10.0f});
};

class Footer {
public:
  void update(const Ctx &ctx) {
    if (ctx.fFont == nullptr) {
      return;
    }
    skiff::nodes::Text::setFont(ctx.fFont);
    bool rebuilt = false;
    if (!fScene) {
      fScene = this->build();
      rebuilt = true;
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

  [[nodiscard]] skia::SkRect takeDamage() {
    return fScene ? fScene->takeDamage() : skia::SkRect::MakeEmpty();
  }

  [[nodiscard]] Action click(float x, float y) {
    fPending = Action::kNone;
    const bool wasOpen = this->optionsOpen();
    if (fScene) {
      (void)fScene->click(x, y);
    }
    // A row sets an action and closes itself. Any other click while the
    // popover was open dismisses it, including its small padding gaps.
    if (wasOpen && fPending == Action::kNone) {
      this->setOptionsOpen(false);
    }
    return fPending;
  }

  [[nodiscard]] bool optionsOpen() const noexcept {
    return fOptions != nullptr && fOptions->expanded();
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
      .fAccent = palette::kAccent,
      .fOnAccent = skia::kWhite,
      .fCorner = 18.0f,
      .fFontSize = 14.0f,
      .fRowHeight = 36.0f,
      .fPaddingX = 12.0f,
  };

  inline static const skiff::widgets::Theme kOptionsTheme = {
      .fSurface = palette::kBackground4,
      .fSurfaceHover = palette::kCardSel,
      .fSurfaceActive = palette::kCardSel,
      .fText = skia::kWhite,
      .fLabel = skia::kWhite,
      .fTextDim = skia::kWhite,
      .fTextFaint = skia::kWhite,
      .fAccent = skia::colorSetARGB(255, 170, 102, 255),
      .fOnAccent = skia::kWhite,
      .fCorner = 8.0f,
      .fFontSize = 14.0f,
      .fRowHeight = 38.0f,
      .fPaddingX = 14.0f,
  };

  skiff::widgets::Button *addButton(skiff::nodes::FillFlow &row,
                                    std::string label, skia::SkColor accent,
                                    Action action) {
    auto *button = row.add<skiff::widgets::Button>(
        {.roles = {skiff::scene::role<style::ActionButton>}},
        std::move(label), [this, action] {
          this->setOptionsOpen(false);
          fPending = action;
        });
    button->fTheme = kButtonTheme;
    button->setAccent(accent);
    button->setOutlined(true);
    return button;
  }

  void setOptionsOpen(bool open) {
    if (fOptions == nullptr) {
      return;
    }
    fOptions->setExpanded(open);
  }

  [[nodiscard]] std::unique_ptr<skiff::scene::Drawable> build() {
    auto root = skiff::scene::make<skiff::scene::Drawable>(
        {.roles = {skiff::scene::role<style::Root>}});
    auto *bar = root->add<skiff::nodes::Box>(
        {.roles = {skiff::scene::role<style::Bar>}}, palette::kBackground5);

    auto *back = bar->add<skiff::widgets::Button>(
        {.roles = {skiff::scene::role<style::Back>}}, "back", [this] {
          this->setOptionsOpen(false);
          fPending = Action::kBack;
        });
    back->fTheme = kButtonTheme;

    auto *actions = bar->add<skiff::nodes::FillFlow>(
        {.roles = {skiff::scene::role<style::Actions>}},
        skiff::nodes::FillFlow::Direction::kHorizontal, 10.0f, 0.0f);
    actions->fWrap = false;
    this->addButton(*actions, "mods", palette::kAccent, Action::kMods);
    this->addButton(*actions, "random",
                    skia::colorSetARGB(255, 102, 204, 255), Action::kRandom);
    fOptionsButton = actions->add<skiff::widgets::Button>(
        {.roles = {skiff::scene::role<style::ActionButton>}}, "options",
        [this] {
          this->setOptionsOpen(!this->optionsOpen());
          fPending = Action::kTaken;
        });
    fOptionsButton->fTheme = kButtonTheme;
    fOptionsButton->setAccent(skia::colorSetARGB(255, 170, 102, 255));
    fOptionsButton->setOutlined(true);

    fOptions = root->add<skiff::widgets::DropdownList>(
        {.roles = {skiff::scene::role<style::Options>}});
    fOptions->fTheme = kOptionsTheme;
    fOptions->fRowHeight = 38.0f;
    fOptions->fFontSize = 14.0f;
    fOptions->fPlateRadius = 10.0f;
    fOptions->fRowRadius = 8.0f;
    fOptions->fTextInset = 14.0f;
    fOptions->fFollow = fOptionsButton;
    fOptions->setOptions({"import .osz", "browse beatmaps", "replays",
                          "delete beatmap", "settings"});
    fOptions->setCurrent(-1);
    fOptions->fOnChoose = [this](int index) {
      static constexpr std::array kActions = {
          Action::kImport, Action::kBrowse, Action::kReplays, Action::kDelete,
          Action::kSettings};
      if (index >= 0 && index < static_cast<int>(kActions.size())) {
        fPending = kActions[static_cast<std::size_t>(index)];
      }
      this->setOptionsOpen(false);
    };
    fOptions->setExpanded(false);

    root->setStyleSheet<FooterTheme>();
    return root;
  }

  Action fPending = Action::kNone;
  std::unique_ptr<skiff::scene::Drawable> fScene;
  skiff::widgets::Button *fOptionsButton = nullptr;
  skiff::widgets::DropdownList *fOptions = nullptr;
};

} // namespace client::songselect
