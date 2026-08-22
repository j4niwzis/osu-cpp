export module client.deletedialog;

import std;
import skia;
import skiff.scene;
import skiff.nodes;
import skiff.widgets.button;
import client.palette;

export namespace client {

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;

namespace delete_dialog_style {
struct Root;
struct Panel;
struct Column;
struct Prompt;
struct Title;
struct Detail;
struct Buttons;
struct Button;
} // namespace delete_dialog_style

struct DeleteDialogTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::select<nodes::Box, delete_dialog_style::Root>(),
                {.width = 1.0f,
                 .height = 1.0f,
                 .relativeSize = scene::Axes::kBoth,
                 .backgroundColour = skia::colorSetARGB(200, 8, 6, 12)})
          .rule(scene::select<nodes::Box, delete_dialog_style::Panel>(),
                {.anchor = scene::Anchor::kCentre,
                 .origin = scene::Anchor::kCentre,
                 .width = 560.0f,
                 .height = 240.0f,
                 .padding = scene::Margin::all(20.0f),
                 .cornerRadius = 12.0f,
                 .backgroundColour = palette::kBackground5})
          .rule(scene::select<nodes::FillFlow,
                              delete_dialog_style::Column>(),
                {.width = 1.0f,
                 .relativeSize = scene::Axes::kX,
                 .autoSize = scene::Axes::kY})
          .rule(scene::select<nodes::Text, delete_dialog_style::Prompt>(),
                {.anchor = scene::Anchor::kTopCentre,
                 .origin = scene::Anchor::kTopCentre,
                 .maxWidth = 520.0f,
                 .alpha = 0.75f,
                 .colour = skia::kWhite,
                 .fontSize = 16.0f})
          .rule(scene::select<nodes::Text, delete_dialog_style::Title>(),
                {.anchor = scene::Anchor::kTopCentre,
                 .origin = scene::Anchor::kTopCentre,
                 .maxWidth = 520.0f,
                 .colour = skia::kWhite,
                 .fontSize = 20.0f,
                 .fontBold = true})
          .rule(scene::select<nodes::Text, delete_dialog_style::Detail>(),
                {.anchor = scene::Anchor::kTopCentre,
                 .origin = scene::Anchor::kTopCentre,
                 .maxWidth = 520.0f,
                 .alpha = 0.6f,
                 .colour = skia::kWhite,
                 .fontSize = 13.0f})
          .rule(scene::select<nodes::FillFlow,
                              delete_dialog_style::Buttons>(),
                {.anchor = scene::Anchor::kBottomCentre,
                 .origin = scene::Anchor::kBottomCentre,
                 .autoSize = scene::Axes::kBoth})
          .rule(scene::select<skiff::widgets::Button,
                              delete_dialog_style::Button>(),
                {.width = 240.0f, .height = 46.0f});
};

// The view owns presentation and modal input. Its caller owns the destructive
// operation: click() reports the choice only after scene dispatch has
// returned, so closing the dialog never destroys a node from its own callback.
class DeleteDialog {
public:
  enum class Choice : std::uint8_t { kNone, kDelete, kCancel };

  [[nodiscard]] bool open() const noexcept { return fScene != nullptr; }

  void show(std::string title, std::size_t difficultyCount) {
    fChoice = Choice::kNone;
    fScene = this->build(std::move(title), difficultyCount);
  }

  void close() {
    fChoice = Choice::kNone;
    fScene.reset();
  }

  void update(skia::SkFont &font, int screenWidth, int screenHeight,
              float mouseX, float mouseY, double nowMs) {
    if (!fScene) {
      return;
    }
    nodes::Text::setFont(&font);
    fScene->updateTree(nowMs);
    fScene->layoutIfNeeded(skia::SkRect::MakeWH(
        static_cast<float>(screenWidth), static_cast<float>(screenHeight)));
    fScene->setHover(mouseX, mouseY);
  }

  void render(skia::SkCanvas *canvas) {
    if (fScene && canvas != nullptr) {
      fScene->draw(canvas);
    }
  }

  [[nodiscard]] scene::FrameResult finishFrame() {
    return fScene ? fScene->finishFrame() : scene::FrameResult{};
  }

  [[nodiscard]] scene::Drawable *sceneRoot() noexcept { return fScene.get(); }

  [[nodiscard]] Choice click(float x, float y) {
    if (!fScene) {
      return Choice::kNone;
    }
    fChoice = Choice::kNone;
    fScene->dispatchPointer(scene::PointerAction::kDown, x, y);
    const Choice choice = fChoice;
    if (choice != Choice::kNone) {
      this->close();
    }
    return choice;
  }

private:
  [[nodiscard]] static std::unique_ptr<skiff::widgets::Button>
  button(std::string label, skia::SkColor accent,
         std::function<void()> action) {
    auto result = scene::make<skiff::widgets::Button>(
        {.roles = {scene::role<delete_dialog_style::Button>}},
        std::move(label), std::move(action));
    auto theme = skiff::widgets::theme();
    theme.fSurface = palette::kCardBg;
    theme.fSurfaceHover = palette::kBackground4;
    theme.fText = accent;
    theme.fCorner = 10.0f;
    theme.fFontSize = 15.0f;
    result->setTheme(theme);
    return result;
  }

  [[nodiscard]] std::unique_ptr<scene::Drawable>
  build(std::string title, std::size_t difficultyCount) {
    auto root = scene::make<nodes::Box>(
        {.roles = {scene::role<delete_dialog_style::Root>}},
        skia::colorSetARGB(200, 8, 6, 12));
    root->setStyleSheet<DeleteDialogTheme>();

    auto *panel = root->add<nodes::Box>(
        {.y = 20.0f,
         .alpha = 0.0f,
         .roles = {scene::role<delete_dialog_style::Panel>}},
        palette::kBackground5);
    panel->fadeTo(1.0f, 200.0, scene::Easing::kOutQuint);
    panel->moveToY(0.0f, 400.0, scene::Easing::kOutQuint);

    auto *column = panel->add<nodes::FillFlow>(
        {.roles = {scene::role<delete_dialog_style::Column>}},
        nodes::FillFlow::Direction::kVertical);
    column->setSpacing(0.0f, 8.0f);
    column->add<nodes::Text>(
        {.roles = {scene::role<delete_dialog_style::Prompt>}},
        "Confirm deletion of", 16.0f, skia::kWhite, false);
    column->add<nodes::Text>(
        {.roles = {scene::role<delete_dialog_style::Title>}}, std::move(title),
        20.0f, skia::kWhite, true);
    column->add<nodes::Text>(
        {.roles = {scene::role<delete_dialog_style::Detail>}},
        std::format("{} difficulties will be removed from disk",
                    difficultyCount),
        13.0f, skia::kWhite, false);

    auto *buttons = panel->add<nodes::FillFlow>(
        {.roles = {scene::role<delete_dialog_style::Buttons>}},
        nodes::FillFlow::Direction::kHorizontal);
    buttons->setSpacing(20.0f, 0.0f);
    buttons->setWrap(false);
    buttons->add(this->button("Yes. Totally. Delete it.",
                              skia::colorSetARGB(255, 255, 110, 110),
                              [this] { fChoice = Choice::kDelete; }));
    buttons->add(this->button("Cancel", palette::kAccent2,
                              [this] { fChoice = Choice::kCancel; }));
    return root;
  }

  Choice fChoice = Choice::kNone;
  std::unique_ptr<scene::Drawable> fScene;
};

} // namespace client
