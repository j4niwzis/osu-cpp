import std;
import gtest;
import skia;
import skiff.scene;
import skiff.widgets.dropdown;
import skiff.widgets.sliderbar;

#include "gtest/gtest-macros.h"

namespace {

namespace scene = skiff::scene;
namespace widgets = skiff::widgets;

TEST(RangeSlider, OwnsHandleSelectionDragAndMinimumSpan) {
  auto root = scene::make<scene::Drawable>({.fill = true});
  auto *slider = root->add<widgets::RangeSlider>(
      {.x = 10.0f, .y = 13.0f, .width = 100.0f, .height = 14.0f});
  slider->fMinSpan = 0.1f;
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

  EXPECT_TRUE(root->click(20.0f, 15.0f));
  EXPECT_EQ(opened, 1);
  EXPECT_TRUE(root->click(20.0f, 64.0f));
  EXPECT_EQ(chosen, 1);

  list->setExpanded(false);
  EXPECT_FALSE(root->click(20.0f, 64.0f));
}

} // namespace
