export module skiff.nodes:cached;

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

// A subtree rendered once into a texture and shown from it afterwards.
//
// Lists of cards, panels of settings and pages of metadata are expensive to
// draw and change rarely: hundreds of glyphs, rounded rectangles and images
// per frame that produce the same pixels every time. This draws them once
// into an offscreen surface and blits that until something inside changes.
// While the subtree is animating, caching would cost more than it saves, so
// it draws straight through.
class CachedContainer : public Drawable {
public:
  // The GPU context the cache surfaces are created on, handed over by the app
  // once the renderer exists.
  static void setContext(skia::GrDirectContext *context) {
    contextSlot() = context;
  }

  void invalidateCache() { fCacheValid = false; }

  void draw(skia::SkCanvas *canvas, float inheritedAlpha = 1.0f) override {
    if (!fVisible || fAlpha <= 0.001f) {
      return;
    }
    skia::GrDirectContext *context = contextSlot();
    const int width = static_cast<int>(std::ceil(fBounds.width()));
    const int height = static_cast<int>(std::ceil(fBounds.height()));
    if (context == nullptr || width <= 0 || height <= 0 ||
        this->animatingTree()) {
      Drawable::draw(canvas, inheritedAlpha);
      return;
    }

    if (!fCache || fCacheWidth != width || fCacheHeight != height) {
      fCache = skia::RenderTarget(
          context, skia::kNo,
          skia::SkImageInfo::Make(width, height, skia::kRGBA_8888_SkColorType,
                                  skia::kPremul_SkAlphaType));
      fCacheWidth = width;
      fCacheHeight = height;
      fCacheValid = false;
    }
    if (!fCache) {
      Drawable::draw(canvas, inheritedAlpha);
      return;
    }

    if (!fCacheValid || fCachedOrigin != fBounds.fLeft + fBounds.fTop) {
      auto *cacheCanvas = fCache->getCanvas();
      cacheCanvas->clear(skia::colorSetARGB(0, 0, 0, 0));
      const int saved = cacheCanvas->save();
      // The subtree is laid out in screen space; shift it into the cache.
      cacheCanvas->translate(-fBounds.fLeft, -fBounds.fTop);
      Drawable::draw(cacheCanvas, 1.0f);
      cacheCanvas->restoreToCount(saved);
      context->flushAndSubmit(fCache.get());
      fCacheValid = true;
      fCachedOrigin = fBounds.fLeft + fBounds.fTop;
    }

    skia::SkPaint paint;
    paint.setAlphaf(inheritedAlpha * fAlpha);
    auto image = fCache->makeImageSnapshot();
    if (image) {
      canvas->drawImage(image.get(), fBounds.fLeft, fBounds.fTop,
                        skia::SkSamplingOptions(), &paint);
    }
    fDrawnBounds = fBounds;
  }

protected:
  // Anything that moves inside invalidates what was captured.
  void layoutChildren() override {
    Drawable::layoutChildren();
    fCacheValid = false;
  }

private:
  static skia::GrDirectContext *&contextSlot() {
    static skia::GrDirectContext *context = nullptr;
    return context;
  }

  skia::Sp<skia::SkSurface> fCache;
  int fCacheWidth = 0;
  int fCacheHeight = 0;
  bool fCacheValid = false;
  float fCachedOrigin = 0.0f;
};

} // namespace skiff::nodes
