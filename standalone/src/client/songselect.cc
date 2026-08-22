export module client.songselect;

import std;
import skia;
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
} // namespace style

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
