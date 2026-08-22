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

struct WidgetTheme {
  static constexpr auto styles =
      makeStyleSheet().rule(select<WidgetBox, Widget>(),
                            {.width = 64.0f, .backgroundColour = kCard});
};

TEST(Style, ResolvesTypedRolesStatesAndViewport) {
  auto root = make<Box>({.fill = true}, kOriginal);
  auto *card = root->add<Box>({.width = 10.0f,
                              .height = 6.0f,
                              .roles = {role<Card>}},
                             kOriginal);

  root->setStyleSheet<CardTheme>();
  root->layoutIfNeeded(skia::SkRect::MakeWH(400.0f, 300.0f));

  EXPECT_FLOAT_EQ(card->fWidth, 20.0f);
  EXPECT_FLOAT_EQ(card->fHeight, 12.0f);
  EXPECT_FLOAT_EQ(card->fAlpha, 0.9f);
  EXPECT_EQ(card->colour(), kCard);

  card->setSelected(true);
  EXPECT_FLOAT_EQ(card->fWidth, 30.0f);
  EXPECT_EQ(card->colour(), kSelected);

  card->setDisabled(true);
  EXPECT_FLOAT_EQ(card->fAlpha, 0.25f);

  card->setDisabled(false);
  card->setSelected(false);
  root->layoutIfNeeded(skia::SkRect::MakeWH(800.0f, 300.0f));
  EXPECT_FLOAT_EQ(card->fHeight, 5.0f);

  root->setHover(1.0f, 1.0f);
  EXPECT_TRUE(card->hovered());
  EXPECT_FLOAT_EQ(card->fScale, 1.2f);

  root->clearStyleSheet();
  EXPECT_FLOAT_EQ(card->fWidth, 10.0f);
  EXPECT_FLOAT_EQ(card->fHeight, 6.0f);
  EXPECT_FLOAT_EQ(card->fAlpha, 1.0f);
  EXPECT_FLOAT_EQ(card->fScale, 1.0f);
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

  EXPECT_FLOAT_EQ(card->fWidth, 30.0f);
  EXPECT_FLOAT_EQ(card->fHeight, 5.0f);
  EXPECT_FLOAT_EQ(card->fAlpha, 0.9f);
  EXPECT_EQ(card->colour(), kSelected);

  // A state change restyles the node, but y is application-owned: no rule
  // mentions it, so the cascade must not undo a run-time move.
  card->fY = 17.0f;
  card->setDisabled(true);
  EXPECT_FLOAT_EQ(card->fY, 17.0f);

  card->addStyleRole<Moved>();
  EXPECT_FLOAT_EQ(card->fY, 42.0f);
  card->removeStyleRole<Moved>();
  EXPECT_FLOAT_EQ(card->fY, 17.0f);
}

TEST(Style, TypesAnExistingDrawableBaseWithoutRtti) {
  auto root = make<Box>({.fill = true}, kOriginal);
  auto *widget = root->add<WidgetBox>({.roles = {role<Widget>}}, kOriginal);

  root->setStyleSheet<WidgetTheme>();

  EXPECT_FLOAT_EQ(widget->fWidth, 64.0f);
  EXPECT_EQ(widget->colour(), kCard);
}

} // namespace
