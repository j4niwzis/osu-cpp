export module client.ui;

import std;
import skia;
import skiff.paint;

// The client's own palette, laid over the framework's painting. Everything
// declared here is osu!'s: the OverlayColourProvider shades, the judgement
// colours, the star-rating ramp. The names the screens already draw through
// -- Painter, fonts(), the easings -- are pulled in from skiff::paint, so a
// screen still writes client::ui:: for all of it and nothing had to change
// when the painting moved out.
export namespace client::ui {

using skiff::paint::approach;
using skiff::paint::combinedAlpha;
using skiff::paint::FontStack;
using skiff::paint::fonts;
using skiff::paint::outElasticHalf;
using skiff::paint::outQuint;
using skiff::paint::Painter;

// osu!lazer's OverlayColourProvider palette (purple scheme) plus the accents
// used across song select and the menus. Kept in one place so screens do not
// each invent their own shade.
inline constexpr skia::SkColor kAccent = skia::colorSetARGB(255, 255, 102, 170);
inline constexpr skia::SkColor kAccent2 = skia::colorSetARGB(255, 102, 204, 255);
inline constexpr skia::SkColor kBackground3 = skia::colorSetARGB(255, 51, 41, 66);
inline constexpr skia::SkColor kBackground4 = skia::colorSetARGB(255, 40, 33, 48);
inline constexpr skia::SkColor kBackground5 = skia::colorSetARGB(255, 31, 25, 40);
inline constexpr skia::SkColor kBackground6 = skia::colorSetARGB(255, 23, 19, 30);
inline constexpr skia::SkColor kCardBg = kBackground4;
inline constexpr skia::SkColor kCardSel = skia::colorSetARGB(255, 64, 48, 70);
inline constexpr skia::SkColor kPanelBg = skia::colorSetARGB(215, 22, 18, 28);

// Judgement colours, matching lazer's OsuColour.
inline constexpr skia::SkColor kGreat = skia::colorSetARGB(255, 102, 204, 255);
inline constexpr skia::SkColor kGood = skia::colorSetARGB(255, 179, 217, 68);
inline constexpr skia::SkColor kMeh = skia::colorSetARGB(255, 255, 204, 34);
inline constexpr skia::SkColor kMiss = skia::colorSetARGB(255, 237, 17, 33);

// Star-rating colour ramp (OsuColour.ForStarDifficulty).
[[nodiscard]] inline skia::SkColor starColor(double stars) {
  if (stars < 2.0) {
    return skia::colorSetARGB(255, 102, 204, 102);
  }
  if (stars < 2.7) {
    return skia::colorSetARGB(255, 102, 204, 255);
  }
  if (stars < 4.0) {
    return skia::colorSetARGB(255, 255, 204, 102);
  }
  if (stars < 5.3) {
    return skia::colorSetARGB(255, 255, 102, 170);
  }
  if (stars < 6.5) {
    return skia::colorSetARGB(255, 170, 102, 255);
  }
  return skia::colorSetARGB(255, 90, 90, 110);
}
} // namespace client::ui
