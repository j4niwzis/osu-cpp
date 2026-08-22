import std;
import gtest;
import skia;
import skiff.nodes;
import skiff.scene;

#include "gtest/gtest-macros.h"

namespace {

using skiff::nodes::Box;
using skiff::nodes::Text;
using namespace skiff::scene;

struct Card;
struct Panel;
struct OwnText;
struct Moved;
struct Widget;
struct Probe;

inline constexpr skia::SkColor kOriginal = 0xff102030;
inline constexpr skia::SkColor kCard = 0xff405060;
inline constexpr skia::SkColor kSelected = 0xff708090;
inline constexpr skia::SkColor kInheritedText = 0xffa0b0c0;
inline constexpr skia::SkColor kOwnText = 0xffd0e0f0;

struct CardTheme {
  static constexpr auto styles =
      makeStyleSheet()
          .rule(selectAny(), {.alpha = 0.9f})
          .rule(select<Box, Card>(),
                {.width = 20.0f,
                 .height = 5.0f,
                 .backgroundColour = kCard})
          .rule(select<Box, Card>().when(StyleState::kSelected),
                {.width = 30.0f, .backgroundColour = kSelected})
          .rule(select<Box, Card>().when(StyleState::kHover),
                {.scale = 1.2f})
          .rule(select<Box, Card>().when(StyleState::kDisabled),
                {.alpha = 0.25f})
          .rule(selectAny<Moved>(), {.y = 42.0f})
          .rule(select<Box, Card>().atMostWidth(500.0f), {.height = 12.0f});
};

struct TextTheme {
  static constexpr auto styles =
      makeStyleSheet()
          .rule(selectAny<Panel>(),
                {.colour = kInheritedText,
                 .fontSize = 18.0f,
                 .fontBold = true})
          .rule(select<Text, OwnText>(),
                {.colour = kOwnText, .fontBold = false});
};

class WidgetBox : public TypedDrawable<WidgetBox, Box> {
public:
  using TypedDrawable::TypedDrawable;
};

class StyleProbe : public TypedDrawable<StyleProbe> {
public:
  [[nodiscard]] int applications() const noexcept { return fApplications; }

protected:
  void applyNodeStyle(const Style &, bool) override { ++fApplications; }

private:
  int fApplications = 0;
};

class InputProbe : public TypedDrawable<InputProbe> {
public:
  InputProbe(std::vector<std::string> *events, std::string name,
             bool capture = false)
      : fEvents(events), fName(std::move(name)), fCapture(capture) {}

  int fPointerEvents = 0;

protected:
  bool acceptsInput() const override { return true; }

  void onPointerEvent(PointerEvent &event) override {
    ++fPointerEvents;
    if (fEvents != nullptr) {
      fEvents->push_back(std::format("{}:{}", fName,
                                    static_cast<int>(event.fPhase)));
    }
    if (fCapture && event.fPhase == EventPhase::kTarget &&
        event.fAction == PointerAction::kDown) {
      event.capturePointer();
      event.requestFocus();
      event.handle();
    } else if (fCapture && event.fPhase == EventPhase::kTarget &&
               event.fAction == PointerAction::kMove) {
      event.handle();
    }
  }

  void onKeyEvent(KeyEvent &event) override {
    if (fEvents != nullptr) {
      fEvents->push_back(std::format("{}:key:{}", fName,
                                    static_cast<int>(event.fPhase)));
    }
  }

  [[nodiscard]] Semantics semantics() const override {
    Semantics out;
    out.fRole = SemanticRole::kButton;
    out.fLabel = fName;
    return out;
  }

private:
  std::vector<std::string> *fEvents;
  std::string fName;
  bool fCapture;
};

struct WidgetTheme {
  static constexpr auto styles =
      makeStyleSheet().rule(select<WidgetBox, Widget>(),
                            {.width = 64.0f, .backgroundColour = kCard});
};

struct ProbeTheme {
  static constexpr auto styles =
      makeStyleSheet().rule(select<StyleProbe, Probe>(), {.alpha = 0.9f});
};

TEST(Style, ResolvesTypedRolesStatesAndViewport) {
  auto root = make<Box>({.fill = true}, kOriginal);
  auto *card = root->add<Box>({.width = 10.0f,
                              .height = 6.0f,
                              .roles = {role<Card>}},
                             kOriginal);

  root->setStyleSheet<CardTheme>();
  root->layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));

  EXPECT_FLOAT_EQ(card->width(), 20.0f);
  EXPECT_FLOAT_EQ(card->height(), 12.0f);
  EXPECT_FLOAT_EQ(card->alpha(), 0.9f);
  EXPECT_EQ(card->colour(), kCard);

  card->setSelected(true);
  EXPECT_FLOAT_EQ(card->width(), 30.0f);
  EXPECT_EQ(card->colour(), kSelected);

  card->setDisabled(true);
  EXPECT_FLOAT_EQ(card->alpha(), 0.25f);

  card->setDisabled(false);
  card->setSelected(false);
  root->layoutIfNeeded(skia::SkRect::MakeWH(800.0f, 300.0f));
  EXPECT_FLOAT_EQ(card->height(), 5.0f);

  root->setHover(1.0f, 1.0f);
  EXPECT_TRUE(card->hovered());
  EXPECT_FLOAT_EQ(card->scale(), 1.2f);

  root->clearStyleSheet();
  EXPECT_FLOAT_EQ(card->width(), 10.0f);
  EXPECT_FLOAT_EQ(card->height(), 6.0f);
  EXPECT_FLOAT_EQ(card->alpha(), 1.0f);
  EXPECT_FLOAT_EQ(card->scale(), 1.0f);
  EXPECT_EQ(card->colour(), kOriginal);
}

TEST(Style, InheritsTextPropertiesAndAllowsOverrides) {
  auto root = make<Box>({.fill = true, .roles = {role<Panel>}}, kOriginal);
  auto *inherited =
      root->add<Text>({}, "Inherited", 11.0f, kOriginal, false);
  auto *overridden = root->add<Text>({.roles = {role<OwnText>}}, "Own", 12.0f,
                                    kOriginal, true);

  root->setStyleSheet<TextTheme>();

  EXPECT_EQ(inherited->colour(), kInheritedText);
  EXPECT_FLOAT_EQ(inherited->fontSize(), 18.0f);
  EXPECT_TRUE(inherited->bold());
  EXPECT_EQ(overridden->colour(), kOwnText);
  EXPECT_FLOAT_EQ(overridden->fontSize(), 18.0f);
  EXPECT_FALSE(overridden->bold());

  root->clearStyleSheet();
  EXPECT_EQ(inherited->colour(), kOriginal);
  EXPECT_FLOAT_EQ(inherited->fontSize(), 11.0f);
  EXPECT_FALSE(inherited->bold());
  EXPECT_EQ(overridden->colour(), kOriginal);
  EXPECT_FLOAT_EQ(overridden->fontSize(), 12.0f);
  EXPECT_TRUE(overridden->bold());
}

TEST(Style, StylesNodesAddedAfterTheSheetIsInstalled) {
  auto root = make<Box>({.fill = true}, kOriginal);
  root->layoutIfNeeded(skia::SkRect::MakeWH(800.0f, 300.0f));
  root->setStyleSheet<CardTheme>();

  auto *card = root->add<Box>({.roles = {role<Card>}, .selected = true},
                              kOriginal);

  EXPECT_FLOAT_EQ(card->width(), 30.0f);
  EXPECT_FLOAT_EQ(card->height(), 5.0f);
  EXPECT_FLOAT_EQ(card->alpha(), 0.9f);
  EXPECT_EQ(card->colour(), kSelected);

  // A state change restyles the node, but y is application-owned: no rule
  // mentions it, so the cascade must not undo a run-time move.
  card->setPosition(card->x(), 17.0f);
  card->setDisabled(true);
  EXPECT_FLOAT_EQ(card->y(), 17.0f);

  card->addStyleRole<Moved>();
  EXPECT_FLOAT_EQ(card->y(), 42.0f);
  card->removeStyleRole<Moved>();
  EXPECT_FLOAT_EQ(card->y(), 17.0f);
}

TEST(Style, TypesAnExistingDrawableBaseWithoutRtti) {
  auto root = make<Box>({.fill = true}, kOriginal);
  auto *widget = root->add<WidgetBox>({.roles = {role<Widget>}}, kOriginal);

  root->setStyleSheet<WidgetTheme>();

  EXPECT_FLOAT_EQ(widget->width(), 64.0f);
  EXPECT_EQ(widget->colour(), kCard);
}

TEST(State, DamagesDrawablesWithoutStateStyleRules) {
  auto root = make<Box>({.fill = true}, kOriginal);
  auto *child = root->add<Box>(
      {.x = 12.0f, .y = 8.0f, .width = 40.0f, .height = 20.0f}, kCard);
  root->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  (void)root->finishFrame();

  child->setSelected(true);
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());

  child->setDisabled(true);
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());
}

TEST(State, HoverOnlyRestylesNodesWithHoverRules) {
  auto root = make<Drawable>({.fill = true});
  auto *probe = root->add<StyleProbe>(
      {.width = 40.0f, .height = 20.0f, .roles = {role<Probe>}});
  root->setStyleSheet<ProbeTheme>();
  root->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  const int applications = probe->applications();
  (void)root->finishFrame();

  root->setHover(10.0f, 10.0f);
  EXPECT_TRUE(probe->hovered());
  EXPECT_EQ(probe->applications(), applications);
  EXPECT_TRUE(root->finishFrame().fDamage.isEmpty());
}

TEST(TextLayout, RelativeWidthIsOwnedByLayout) {
  skia::SkFont font;
  Text::setFont(&font);
  auto root = make<Drawable>({.fill = true});
  auto *text = root->add<Text>(
      {.width = 0.5f, .relativeSize = Axes::kX}, "short", 14.0f,
      skia::kWhite);

  root->layoutIfNeeded(skia::SkRect::MakeWH(240.0f, 80.0f));

  EXPECT_FLOAT_EQ(text->bounds().width(), 120.0f);
}

TEST(Input, PropagatesCaptureTargetAndBubbleInOrder) {
  std::vector<std::string> events;
  auto root = make<InputProbe>({.fill = true}, &events, "root");
  root->add<InputProbe>({.width = 40.0f, .height = 20.0f}, &events, "child");
  root->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));

  PointerEvent down;
  down.fAction = PointerAction::kDown;
  down.fX = 10.0f;
  down.fY = 10.0f;
  EXPECT_FALSE(root->dispatchPointer(down));
  EXPECT_EQ(events, (std::vector<std::string>{"root:0", "child:1", "root:2"}));

  events.clear();
  KeyEvent key;
  EXPECT_FALSE(root->dispatchKey(key));
  EXPECT_EQ(events,
            (std::vector<std::string>{"root:key:0", "child:key:1",
                                      "root:key:2"}));
}

TEST(Input, PointerCaptureSurvivesLeavingTheControl) {
  std::vector<std::string> events;
  auto root = make<Drawable>({.fill = true});
  auto *child = root->add<InputProbe>(
      {.width = 40.0f, .height = 20.0f}, &events, "drag", true);
  root->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));

  PointerEvent down;
  down.fAction = PointerAction::kDown;
  down.fX = 10.0f;
  down.fY = 10.0f;
  EXPECT_TRUE(root->dispatchPointer(down));
  EXPECT_EQ(root->capturedNode(), child);
  EXPECT_EQ(root->focusedNode(), child);

  PointerEvent move;
  move.fAction = PointerAction::kMove;
  move.fX = 500.0f;
  move.fY = 500.0f;
  EXPECT_TRUE(root->dispatchPointer(move));
  EXPECT_EQ(child->fPointerEvents, 2);

  PointerEvent up;
  up.fAction = PointerAction::kUp;
  up.fX = 500.0f;
  up.fY = 500.0f;
  (void)root->dispatchPointer(up);
  EXPECT_EQ(root->capturedNode(), nullptr);
}

TEST(Input, ModalLayerBlocksPointerAndAccessibilityBehindIt) {
  std::vector<std::string> events;
  auto behind = make<InputProbe>({.fill = true}, &events, "behind");
  auto modal = make<Drawable>({.fill = true});
  behind->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  modal->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  const std::array<InputRouter::Layer, 2> layers = {
      InputRouter::Layer{behind.get(), false},
      InputRouter::Layer{modal.get(), true}};
  InputRouter router;
  router.setLayers(layers);

  PointerEvent down;
  down.fAction = PointerAction::kDown;
  down.fX = 10.0f;
  down.fY = 10.0f;
  EXPECT_TRUE(router.pointer(down));
  EXPECT_EQ(behind->fPointerEvents, 0);
  EXPECT_TRUE(router.semantics().empty());
}

TEST(Frame, RuntimePropertiesInvalidateAndReportContinuationTogether) {
  auto root = make<Drawable>({.fill = true});
  auto *child = root->add<Drawable>({.width = 20.0f, .height = 10.0f});
  root->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  (void)root->finishFrame();

  child->setPosition(30.0f, 5.0f);
  root->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  const FrameResult moved = root->finishFrame();
  EXPECT_FALSE(moved.fDamage.isEmpty());
  EXPECT_FALSE(moved.fWantsAnotherFrame);

  child->apply({.alpha = 0.5f});
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());

  child->moveToX(60.0f, 100.0);
  const FrameResult animated = root->finishFrame();
  EXPECT_TRUE(animated.fWantsAnotherFrame);
}

} // namespace
