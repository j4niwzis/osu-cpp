import std;
import gtest;
import skia;
import skiff.nodes;
import skiff.paint;
import skiff.scene;

#include "gtest/gtest-macros.h"

namespace {

using skiff::nodes::Box;
using skiff::nodes::FillFlow;
using skiff::nodes::ScrollContainer;
using skiff::nodes::Text;
using namespace skiff::scene;

TEST(Animation, DoesNotSettleBeforeApproachReachesItsTarget) {
  float value = 0.0015f;
  EXPECT_FALSE(skiff::paint::settled(value, 0.0f));

  int frames = 0;
  while (!skiff::paint::settled(value, 0.0f)) {
    value = skiff::paint::approach(value, 0.0f, 110.0f, 16.0);
    ASSERT_LT(++frames, 20);
  }

  EXPECT_FLOAT_EQ(value, 0.0f);
}

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

class LayoutProbe : public TypedDrawable<LayoutProbe> {
public:
  [[nodiscard]] int layouts() const noexcept { return fLayouts; }

protected:
  void measure(const skia::SkRect &) override { ++fLayouts; }

private:
  int fLayouts = 0;
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
    out.fActions = {SemanticAction::kFocus, SemanticAction::kActivate};
    return out;
  }

private:
  std::vector<std::string> *fEvents;
  std::string fName;
  bool fCapture;
};

struct FocusTheme {
  static constexpr auto styles =
      makeStyleSheet().rule(select<InputProbe>().when(StyleState::kFocus),
                            {.alpha = 0.65f});
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

  const auto behindTree = behind->semanticsTree();
  ASSERT_EQ(behindTree.size(), 1u);
  SemanticActionEvent activate;
  EXPECT_TRUE(router.semantic(behindTree[0].fId, activate));
  EXPECT_EQ(behind->fPointerEvents, 0);
}

TEST(Input, ModalScopeCancelsCoveredCaptureAndRestoresPriorFocus) {
  std::vector<std::string> events;
  auto behind = make<InputProbe>({.fill = true}, &events, "behind", true);
  auto modal = make<Drawable>({.fill = true});
  behind->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  modal->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  InputRouter router;
  const std::array<InputRouter::Layer, 1> base = {
      InputRouter::Layer{behind.get(), false}};

  PointerEvent down;
  down.fAction = PointerAction::kDown;
  down.fX = 10.0f;
  down.fY = 10.0f;
  // Some client wrappers dispatch the press directly so they can read their
  // callback immediately. The router adopts that capture on its next layer
  // refresh and still cancels it when a modal covers the scene.
  EXPECT_TRUE(behind->dispatchPointer(down));
  EXPECT_EQ(behind->capturedNode(), behind.get());
  EXPECT_EQ(behind->focusedNode(), behind.get());

  const std::array<InputRouter::Layer, 2> covered = {
      InputRouter::Layer{behind.get(), false},
      InputRouter::Layer{modal.get(), true}};
  router.setLayers(covered);
  EXPECT_EQ(behind->capturedNode(), nullptr);

  router.setLayers(base);
  EXPECT_EQ(behind->focusedNode(), behind.get());
}

TEST(Input, TabTraversesAcrossSceneRoots) {
  std::vector<std::string> events;
  auto first = make<InputProbe>({.fill = true}, &events, "first");
  auto second = make<InputProbe>({.fill = true}, &events, "second");
  first->setStyleSheet<FocusTheme>();
  second->setStyleSheet<FocusTheme>();
  first->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  second->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  const std::array<InputRouter::Layer, 2> layers = {
      InputRouter::Layer{first.get(), false},
      InputRouter::Layer{second.get(), false}};
  InputRouter router;
  router.setLayers(layers);

  KeyEvent tab;
  tab.fKey = Key::kTab;
  EXPECT_TRUE(router.key(tab));
  EXPECT_EQ(first->focusedNode(), first.get());
  EXPECT_EQ(second->focusedNode(), nullptr);
  EXPECT_FLOAT_EQ(first->alpha(), 0.65f);

  EXPECT_TRUE(router.key(tab));
  EXPECT_EQ(first->focusedNode(), nullptr);
  EXPECT_EQ(second->focusedNode(), second.get());
  EXPECT_FLOAT_EQ(first->alpha(), 1.0f);
  EXPECT_FLOAT_EQ(second->alpha(), 0.65f);

  tab.fShift = true;
  EXPECT_TRUE(router.key(tab));
  EXPECT_EQ(first->focusedNode(), first.get());
}

TEST(Input, PointerFocusHasOneOwnerAcrossSceneRoots) {
  std::vector<std::string> events;
  auto firstRoot = make<Drawable>({.fill = true});
  auto *first = firstRoot->add<InputProbe>(
      {.width = 40.0f, .height = 40.0f}, &events, "first", true);
  auto secondRoot = make<Drawable>({.fill = true});
  auto *second = secondRoot->add<InputProbe>(
      {.x = 60.0f, .width = 40.0f, .height = 40.0f}, &events, "second", true);
  firstRoot->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  secondRoot->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  const std::array<InputRouter::Layer, 2> layers = {
      InputRouter::Layer{firstRoot.get(), false},
      InputRouter::Layer{secondRoot.get(), false}};
  InputRouter router;
  router.setLayers(layers);

  PointerEvent down;
  down.fAction = PointerAction::kDown;
  down.fX = 10.0f;
  down.fY = 10.0f;
  EXPECT_TRUE(router.pointer(down));
  EXPECT_EQ(firstRoot->focusedNode(), first);
  PointerEvent up = down;
  up.fAction = PointerAction::kUp;
  EXPECT_FALSE(router.pointer(up));

  down.fX = 70.0f;
  EXPECT_TRUE(router.pointer(down));
  EXPECT_EQ(firstRoot->focusedNode(), nullptr);
  EXPECT_EQ(secondRoot->focusedNode(), second);

  const auto firstSemantics = firstRoot->semanticsTree();
  ASSERT_EQ(firstSemantics.size(), 1u);
  SemanticActionEvent focus;
  focus.fAction = SemanticAction::kFocus;
  EXPECT_TRUE(router.semantic(firstSemantics[0].fId, focus));
  EXPECT_EQ(firstRoot->focusedNode(), first);
  EXPECT_EQ(secondRoot->focusedNode(), nullptr);
}

TEST(Input, DestroyedSceneRootMakesRetainedLayerInert) {
  std::vector<std::string> events;
  InputRouter router;
  {
    auto root = make<InputProbe>({.fill = true}, &events, "temporary", true);
    root->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
    const std::array<InputRouter::Layer, 1> layers = {
        InputRouter::Layer{root.get(), false}};
    router.setLayers(layers);

    PointerEvent down;
    down.fAction = PointerAction::kDown;
    down.fX = 10.0f;
    down.fY = 10.0f;
    EXPECT_TRUE(router.pointer(down));
    EXPECT_EQ(root->capturedNode(), root.get());
  }

  PointerEvent move;
  move.fAction = PointerAction::kMove;
  move.fX = 20.0f;
  move.fY = 20.0f;
  EXPECT_FALSE(router.pointer(move));
  EXPECT_TRUE(router.semantics().empty());

  KeyEvent tab;
  tab.fKey = Key::kTab;
  EXPECT_FALSE(router.key(tab));
  router.setLayers({});

  auto behind = make<InputProbe>({.fill = true}, &events, "behind", true);
  behind->layoutIfNeeded(skia::SkRect::MakeWH(100.0f, 60.0f));
  {
    auto modal = make<Drawable>({.fill = true});
    const std::array<InputRouter::Layer, 2> layers = {
        InputRouter::Layer{behind.get(), false},
        InputRouter::Layer{modal.get(), true}};
    router.setLayers(layers);
  }

  // An expired modal is not an invisible input shield over live layers.
  EXPECT_EQ(router.semantics().size(), 1u);
  PointerEvent down;
  down.fAction = PointerAction::kDown;
  down.fX = 10.0f;
  down.fY = 10.0f;
  EXPECT_TRUE(router.pointer(down));
  EXPECT_EQ(behind->capturedNode(), behind.get());
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

TEST(Layout, SkipsCleanSiblingSubtrees) {
  auto root = make<Drawable>({.fill = true});
  auto *dirtyBranch =
      root->add<Drawable>({.width = 50.0f, .height = 60.0f});
  auto *dirty =
      dirtyBranch->add<LayoutProbe>({.width = 20.0f, .height = 10.0f});
  auto *cleanBranch = root->add<Drawable>(
      {.x = 50.0f, .width = 50.0f, .height = 60.0f});
  auto *clean =
      cleanBranch->add<LayoutProbe>({.width = 20.0f, .height = 10.0f});
  const auto viewport = skia::SkRect::MakeWH(100.0f, 60.0f);

  EXPECT_TRUE(root->layoutIfNeeded(viewport));
  EXPECT_EQ(dirty->layouts(), 1);
  EXPECT_EQ(clean->layouts(), 1);

  dirty->setSize(25.0f, 10.0f);
  EXPECT_TRUE(root->layoutIfNeeded(viewport));
  EXPECT_EQ(dirty->layouts(), 2);
  EXPECT_EQ(clean->layouts(), 1);

  EXPECT_FALSE(root->layoutIfNeeded(viewport));
  EXPECT_EQ(dirty->layouts(), 2);
  EXPECT_EQ(clean->layouts(), 1);
}

TEST(Layout, PaintOnlyAnimationDoesNotDirtyLayout) {
  auto root = make<Drawable>({.fill = true});
  auto *child =
      root->add<LayoutProbe>({.width = 20.0f, .height = 10.0f});
  const auto viewport = skia::SkRect::MakeWH(100.0f, 60.0f);
  root->layoutIfNeeded(viewport);
  ASSERT_EQ(child->layouts(), 1);

  child->apply({.alpha = 0.8f});
  EXPECT_FALSE(root->layoutIfNeeded(viewport));
  EXPECT_EQ(child->layouts(), 1);

  child->fadeTo(0.5f, 100.0);
  root->updateTree(10.0);
  EXPECT_FALSE(root->layoutIfNeeded(viewport));
  EXPECT_EQ(child->layouts(), 1);
  EXPECT_TRUE(root->finishFrame().fWantsAnotherFrame);

  child->moveToX(30.0f, 100.0);
  root->updateTree(20.0);
  EXPECT_TRUE(root->layoutIfNeeded(viewport));
  EXPECT_EQ(child->layouts(), 2);
}

TEST(Layout, FlowRearrangesOnlyChildrenWhosePositionChanged) {
  auto root = make<FillFlow>({.fill = true}, FillFlow::Direction::kVertical);
  auto *first =
      root->add<LayoutProbe>({.width = 20.0f, .height = 10.0f});
  auto *second =
      root->add<LayoutProbe>({.width = 20.0f, .height = 10.0f});
  auto *third =
      root->add<LayoutProbe>({.width = 20.0f, .height = 10.0f});
  const auto viewport = skia::SkRect::MakeWH(100.0f, 60.0f);
  root->layoutIfNeeded(viewport);
  ASSERT_FLOAT_EQ(second->bounds().fTop, 10.0f);
  ASSERT_FLOAT_EQ(third->bounds().fTop, 20.0f);
  const int firstLayouts = first->layouts();
  const int secondLayouts = second->layouts();
  const int thirdLayouts = third->layouts();

  first->setSize(20.0f, 20.0f);
  root->layoutIfNeeded(viewport);
  EXPECT_FLOAT_EQ(second->bounds().fTop, 20.0f);
  EXPECT_FLOAT_EQ(third->bounds().fTop, 30.0f);
  EXPECT_EQ(first->layouts(), firstLayouts + 1);
  EXPECT_EQ(second->layouts(), secondLayouts + 1);
  EXPECT_EQ(third->layouts(), thirdLayouts + 1);

  const int movedSecondLayouts = second->layouts();
  const int movedThirdLayouts = third->layouts();
  first->setSize(25.0f, 20.0f);
  root->layoutIfNeeded(viewport);
  EXPECT_EQ(first->layouts(), firstLayouts + 2);
  EXPECT_EQ(second->layouts(), movedSecondLayouts);
  EXPECT_EQ(third->layouts(), movedThirdLayouts);
}

TEST(Layout, ProgrammaticScrollInvalidatesItsSubtree) {
  auto root = make<ScrollContainer>({.fill = true});
  auto *content =
      root->add<LayoutProbe>({.width = 100.0f, .height = 200.0f});
  const auto viewport = skia::SkRect::MakeWH(100.0f, 60.0f);
  root->layoutIfNeeded(viewport);
  ASSERT_FLOAT_EQ(content->bounds().fTop, 0.0f);

  root->setCurrent(20.0f);
  EXPECT_TRUE(root->layoutIfNeeded(viewport));
  EXPECT_FLOAT_EQ(content->bounds().fTop, -20.0f);
}

} // namespace
