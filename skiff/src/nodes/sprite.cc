export module skiff.nodes:sprite;

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

// An image, cropped to fill its box rather than squashed into it.
class Sprite : public Drawable {
public:
  explicit Sprite(skia::Sp<skia::SkImage> image) : fImage(std::move(image)) {}

  void setImage(skia::Sp<skia::SkImage> image) { fImage = std::move(image); }

protected:
  void drawSelf(skia::SkCanvas *canvas, float alpha) override {
    skiff::paint::imageFilled(canvas, fImage.get(), fBounds, alpha);
  }

private:
  skia::Sp<skia::SkImage> fImage;
};

} // namespace skiff::nodes
