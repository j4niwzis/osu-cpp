export module skiff.widgets:theme;

import std;
import skia;
import skiff.paint;
import skiff.scene;

namespace skiff::widgets {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::widgets

export namespace skiff::widgets {

struct Theme {
  skia::SkColor fSurface = skia::colorSetARGB(255, 46, 53, 56);
  skia::SkColor fSurfaceHover = skia::colorSetARGB(255, 57, 66, 70);
  skia::SkColor fSurfaceActive = skia::colorSetARGB(255, 69, 79, 84);
  skia::SkColor fText = skia::colorSetARGB(255, 255, 255, 255);
  // A caption beside a control, as distinct from the control's own text.
  skia::SkColor fLabel = skia::colorSetARGB(255, 219, 233, 240);
  // Secondary -- icons, unselected tabs -- and fainter still, placeholders.
  skia::SkColor fTextDim = skia::colorSetARGB(255, 178, 190, 196);
  skia::SkColor fTextFaint = skia::colorSetARGB(255, 143, 156, 163);
  skia::SkColor fAccent = skia::colorSetARGB(255, 102, 204, 255);
  skia::SkColor fOnAccent = skia::colorSetARGB(255, 23, 26, 28);

  float fCorner = 5.0f;
  float fFontSize = 16.0f;
  float fRowHeight = 40.0f;
  float fPaddingX = 12.0f;
};

// The one every widget starts from. Set it once, before any tree is built.
inline Theme &theme() {
  static Theme t;
  return t;
}

} // namespace skiff::widgets
