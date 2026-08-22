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
using skiff::scene::Style;
} // namespace skiff::nodes

export namespace skiff::nodes {

// A filled rectangle, optionally rounded. The framework's Box.
class Box : public skiff::scene::TypedDrawable<Box> {
public:
  explicit Box(skia::SkColor colour) : fColour(colour) {}

  void setColour(skia::SkColor colour) {
    if (colour == fColour) {
      return;
    }
    fColour = colour;
    this->markDamaged();
  }

protected:
  void applyNodeStyle(const Style &style, bool active) override {
    if (!active && !fNodeStyleActive) {
      return;
    }
    if (active && !fNodeStyleActive) {
      fBaseColour = fColour;
    }
    const skia::SkColor target =
        active ? style.backgroundColour.value_or(fBaseColour) : fBaseColour;
    if (target != fColour) {
      fColour = target;
      this->markDamaged();
    }
    fNodeStyleActive = active;
  }

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
  skia::SkColor fBaseColour = 0;
  bool fNodeStyleActive = false;
};

} // namespace skiff::nodes
