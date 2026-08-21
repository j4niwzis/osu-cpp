export module skiff.nodes:box;

import std;
import skia;
import skiff.paint;
import skiff.scene;

namespace skiff::nodes {
using skiff::scene::Anchor;
using skiff::scene::Axes;
using skiff::scene::Drawable;
using skiff::scene::Easing;
using skiff::scene::Margin;
using skiff::scene::Spec;
} // namespace skiff::nodes

export namespace skiff::nodes {

// A filled rectangle, optionally rounded. The framework's Box.
class Box : public Drawable {
public:
  explicit Box(skia::SkColor colour) : fColour(colour) {}

  void setColour(skia::SkColor colour) { fColour = colour; }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fColour);
    paint.setAlphaf(alpha);
    if (fCornerRadius > 0.0f) {
      canvas->drawRRect(
          skia::SkRRect::MakeRectXY(fBounds, fCornerRadius, fCornerRadius),
          paint);
    } else {
      canvas->drawRect(fBounds, paint);
    }
  }

private:
  skia::SkColor fColour;
};

} // namespace skiff::nodes
