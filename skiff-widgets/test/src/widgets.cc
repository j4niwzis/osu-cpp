import std;
import gtest;
import skia;
import skiff.scene;
import skiff.widgets.button;
import skiff.widgets.dropdown;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;

#include "gtest/gtest-macros.h"

namespace {

namespace scene = skiff::scene;
namespace widgets = skiff::widgets;

TEST(RangeSlider, OwnsHandleSelectionDragAndMinimumSpan) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  auto *slider = root->add<widgets::RangeSlider>(
      {.x = 10.0f,
       .y = 13.0f,
       .width = 100.0f,
       .height = 14.0f,
       .relativeSize = scene::Axes::kNone});
  slider->setMinimumSpan(0.1f);
  int changes = 0;
  slider->fOnSet = [&changes](float, float) { ++changes; };
  root->layoutIfNeeded(skia::SkRect::MakeWH(200.0f, 40.0f));

  // Half way is equally close to both ends, so the lower handle wins.
  EXPECT_TRUE(root->click(60.0f, 20.0f));
  EXPECT_TRUE(slider->dragging());
  EXPECT_FLOAT_EQ(slider->low(), 0.5f);
  EXPECT_FLOAT_EQ(slider->high(), 1.0f);

  slider->dragTo(200.0f);
  EXPECT_NEAR(slider->low(), 0.9f, 0.0001f);
  EXPECT_FLOAT_EQ(slider->high(), 1.0f);
  EXPECT_EQ(changes, 2);

  slider->endDrag();
  EXPECT_FALSE(slider->dragging());
}

TEST(Dropdown, ButtonAndRowsRouteTheirOwnClicks) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  int opened = 0;
  auto *button = root->add<widgets::DropdownButton>(
      {.width = 120.0f, .height = 30.0f}, "Sort", "Title");
  button->fOnOpen = [&opened] { ++opened; };

  int chosen = -1;
  auto *list = root->add<widgets::DropdownList>(
      {.y = 34.0f, .width = 120.0f});
  list->setOptions({"Artist", "Title"});
  list->setCurrent(1);
  list->fOnChoose = [&chosen](int index) { chosen = index; };
  root->layoutIfNeeded(skia::SkRect::MakeWH(200.0f, 120.0f));

  EXPECT_TRUE(list->expanded());
  EXPECT_TRUE(root->click(20.0f, 15.0f));
  EXPECT_EQ(opened, 1);
  EXPECT_TRUE(root->click(20.0f, 64.0f));
  EXPECT_EQ(chosen, 1);

  list->setExpanded(false);
  EXPECT_FALSE(list->expanded());
  EXPECT_FALSE(root->click(20.0f, 64.0f));
}

TEST(Button, PrimaryAndEnabledStateOwnDamageAndInput) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  int clicks = 0;
  auto *button = root->add<widgets::Button>(
      {.width = 100.0f, .height = 30.0f}, "Render", [&clicks] { ++clicks; });
  root->layoutIfNeeded(skia::SkRect::MakeWH(120.0f, 50.0f));
  (void)root->finishFrame();

  button->setPrimary(true);
  EXPECT_TRUE(button->primary());
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());
  button->setOutlined(true);
  EXPECT_TRUE(button->outlined());
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());
  button->setAccent(0xff123456);
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());
  EXPECT_TRUE(root->click(20.0f, 15.0f));
  EXPECT_EQ(clicks, 1);

  button->setEnabled(false);
  EXPECT_FALSE(button->enabled());
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());
  EXPECT_FALSE(root->click(20.0f, 15.0f));
  EXPECT_EQ(clicks, 1);

  root->setHover(-1.0f, -1.0f);
  (void)root->finishFrame();
  root->setHover(20.0f, 15.0f);
  EXPECT_TRUE(root->finishFrame().fDamage.isEmpty());
}

TEST(Hover, OnlyVisibleHoverChangesCauseDamage) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  auto *slider = root->add<widgets::SliderBar>(
      {.width = 100.0f, .height = 14.0f});
  slider->fOnSet = [](float) {};
  root->add<widgets::Button>(
      {.y = 20.0f, .width = 100.0f, .height = 30.0f}, "Apply", [] {});
  root->layoutIfNeeded(skia::SkRect::MakeWH(120.0f, 60.0f));
  (void)root->finishFrame();

  root->setHover(20.0f, 7.0f);
  EXPECT_TRUE(slider->hovered());
  EXPECT_TRUE(root->finishFrame().fDamage.isEmpty());

  root->setHover(20.0f, 35.0f);
  EXPECT_FALSE(slider->hovered());
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());
}

TEST(TextBox, TextAndSelectionOwnDamage) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  auto *box = root->add<widgets::TextBox>(
      {.width = 100.0f, .height = 30.0f}, "Size");
  root->layoutIfNeeded(skia::SkRect::MakeWH(120.0f, 50.0f));
  (void)root->finishFrame();

  box->setText("1920x1080");
  EXPECT_EQ(box->text(), "1920x1080");
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());

  box->setSelected(true);
  EXPECT_TRUE(box->selected());
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());
}

TEST(RangeSlider, RoutedDragKeepsCaptureOutsideItsBounds) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  auto *slider = root->add<widgets::RangeSlider>(
      {.x = 10.0f,
       .y = 13.0f,
       .width = 100.0f,
       .height = 14.0f,
       .relativeSize = scene::Axes::kNone});
  slider->setMinimumSpan(0.1f);
  slider->fOnSet = [](float, float) {};
  root->layoutIfNeeded(skia::SkRect::MakeWH(200.0f, 40.0f));

  scene::PointerEvent down;
  down.fAction = scene::PointerAction::kDown;
  down.fX = 60.0f;
  down.fY = 20.0f;
  EXPECT_TRUE(root->dispatchPointer(down));
  EXPECT_EQ(root->capturedNode(), slider);
  EXPECT_FLOAT_EQ(slider->low(), 0.5f);

  scene::PointerEvent move;
  move.fAction = scene::PointerAction::kMove;
  move.fX = 200.0f;
  move.fY = 100.0f;
  EXPECT_TRUE(root->dispatchPointer(move));
  EXPECT_NEAR(slider->low(), 0.9f, 0.0001f);

  scene::PointerEvent up;
  up.fAction = scene::PointerAction::kUp;
  up.fX = 200.0f;
  up.fY = 100.0f;
  EXPECT_TRUE(root->dispatchPointer(up));
  EXPECT_EQ(root->capturedNode(), nullptr);
  EXPECT_FALSE(slider->dragging());
}

TEST(Button, TabFocusAndEnterActivate) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  int clicks = 0;
  auto *button = root->add<widgets::Button>(
      {.width = 100.0f, .height = 30.0f}, "Apply", [&clicks] { ++clicks; });
  root->layoutIfNeeded(skia::SkRect::MakeWH(120.0f, 50.0f));
  (void)root->finishFrame();

  scene::KeyEvent tab;
  tab.fKey = scene::Key::kTab;
  EXPECT_TRUE(root->dispatchKey(tab));
  EXPECT_EQ(root->focusedNode(), button);
  EXPECT_FALSE(root->finishFrame().fDamage.isEmpty());

  scene::KeyEvent enter;
  enter.fKey = scene::Key::kEnter;
  EXPECT_TRUE(root->dispatchKey(enter));
  EXPECT_EQ(clicks, 1);
}

TEST(TextBox, RoutedUtf8AndCompositionUseFocus) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  auto *box = root->add<widgets::TextBox>(
      {.width = 100.0f, .height = 30.0f}, "Search");
  std::string changed;
  box->fOnChanged = [&changed](std::string_view text) { changed = text; };
  root->layoutIfNeeded(skia::SkRect::MakeWH(120.0f, 50.0f));

  scene::PointerEvent down;
  down.fAction = scene::PointerAction::kDown;
  down.fX = 10.0f;
  down.fY = 10.0f;
  EXPECT_TRUE(root->dispatchPointer(down));
  EXPECT_EQ(root->focusedNode(), box);

  scene::TextInputEvent composing;
  composing.fComposition = "ka";
  composing.fCommit = false;
  EXPECT_TRUE(root->dispatchText(composing));
  EXPECT_TRUE(box->text().empty());

  scene::TextInputEvent commit;
  commit.fText = "か";
  EXPECT_TRUE(root->dispatchText(commit));
  EXPECT_EQ(box->text(), "か");
  EXPECT_EQ(changed, "か");

  scene::KeyEvent backspace;
  backspace.fKey = scene::Key::kBackspace;
  EXPECT_TRUE(root->dispatchKey(backspace));
  EXPECT_TRUE(box->text().empty());

  const auto semantics = root->semanticsTree();
  ASSERT_EQ(semantics.size(), 1u);
  EXPECT_EQ(semantics[0].fRole, scene::SemanticRole::kTextBox);
  EXPECT_TRUE(semantics[0].fFocused);
}

} // namespace
