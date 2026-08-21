export module skin;

import std;
import skia;
import osu;
import client.util;

namespace client {

namespace detail {

inline skia::Sp<skia::SkImage> decodeImage(skia::Sp<skia::SkData> data) {
  if (!data || data->isEmpty()) {
    return nullptr;
  }
  std::unique_ptr<skia::SkCodec> codec =
      skia::SkCodec::MakeFromData(std::move(data));
  if (!codec) {
    return nullptr;
  }
  const skia::SkImageInfo info = codec->getInfo()
                                     .makeColorType(skia::kN32_SkColorType)
                                     .makeAlphaType(skia::kPremul_SkAlphaType);
  skia::SkBitmap bitmap;
  if (!bitmap.tryAllocPixels(info) ||
      codec->getPixels(info, bitmap.getPixels(), bitmap.rowBytes()) !=
          skia::SkCodec::kSuccess) {
    return nullptr;
  }
  return skia::RasterFromBitmap(bitmap);
}

inline skia::Sp<skia::SkImage> decodeImage(const std::filesystem::path &path) {
  return decodeImage(skia::SkData::MakeFromFileName(path.c_str()));
}

inline skia::Sp<skia::SkImage>
decodeImage(std::span<const std::uint8_t> bytes) {
  return decodeImage(skia::SkData::MakeWithoutCopy(bytes.data(), bytes.size()));
}

inline std::filesystem::path findFile(const std::filesystem::path &root,
                                      std::string_view base) {
  if (root.empty()) {
    return {};
  }
  std::string lower = detail::toLower(base);

  const std::vector<std::string_view> exts{"", ".png", ".jpg", ".jpeg"};
  for (const auto &ext : exts) {
    auto candidate = root / (lower + std::string(ext));
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

inline void drawImageCentered(skia::SkCanvas *canvas, skia::SkImage *image,
                              float x, float y, float width, float height,
                              const skia::SkPaint &paint) {
  if (image == nullptr)
    return;
  const skia::SkRect dst = skia::SkRect::MakeXYWH(
      x - width * 0.5f, y - height * 0.5f, width, height);
  canvas->drawImageRect(
      image, dst, skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
}

inline void drawImageCentered(skia::SkCanvas *canvas, skia::SkImage *image,
                              float x, float y, float scale,
                              const skia::SkPaint &paint) {
  if (image == nullptr)
    return;
  const float width = static_cast<float>(image->width()) * scale;
  const float height = static_cast<float>(image->height()) * scale;
  drawImageCentered(canvas, image, x, y, width, height, paint);
}

inline skia::SkPaint tintedPaint(const skia::Sp<skia::SkColorFilter> &filter,
                                 float alpha = 1.0f) {
  skia::SkPaint paint;
  paint.setAntiAlias(true);
  paint.setAlphaf(alpha);
  paint.setColorFilter(filter);
  return paint;
}

// Extra scale applied to circle/slider sprites so they look bigger while the
// playfield margin stays fixed.
inline constexpr double kCircleVisualScale = 1.05;

[[nodiscard]] inline double circleVisualRadius(double cs) noexcept {
  return osu::circleRadius(cs) * kCircleVisualScale;
}

} // namespace detail

export class Skin {
public:
  explicit Skin(std::filesystem::path root = {}) : fRoot(std::move(root)) {
    fComboPaint.setAntiAlias(true);
    fComboFallbackPaint.setColor(skia::kWhite);
    fComboFallbackPaint.setStyle(skia::kFillStyle);
    fComboFallbackPaint.setAntiAlias(true);
  }

  [[nodiscard]] const std::filesystem::path &root() const noexcept {
    return fRoot;
  }

  void load(std::filesystem::path root) {
    fRoot = std::move(root);
    fImages.clear();
  }

  [[nodiscard]] skia::Sp<skia::SkImage> image(std::string_view name) {
    const std::string key(name);
    if (auto it = fImages.find(key); it != fImages.end()) {
      return it->second;
    }
    auto img = detail::decodeImage(detail::findFile(fRoot, name));
    fImages[key] = img;
    return img;
  }

  [[nodiscard]] skia::Sp<skia::SkImage> hitcircle() {
    return image("hitcircle");
  }

  // Judgement sprites. osu! skins name them hit300 / hit100 / hit50 / hit0,
  // which is what stable and web-osu2 draw instead of words.
  [[nodiscard]] skia::Sp<skia::SkImage> judgement(int value) {
    switch (value) {
    case 300:
      return image("hit300");
    case 100:
      return image("hit100");
    case 50:
      return image("hit50");
    default:
      return image("hit0");
    }
  }
  [[nodiscard]] skia::Sp<skia::SkImage> disc() { return image("disc"); }
  [[nodiscard]] skia::Sp<skia::SkImage> hitcircleOverlay() {
    return image("hitcircleoverlay");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> ringGlow() {
    return image("ring-glow");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> approachCircle() {
    return image("approachcircle");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> sliderB() {
    auto img = image("sliderb0");
    if (!img)
      img = image("sliderb");
    return img;
  }
  [[nodiscard]] skia::Sp<skia::SkImage> sliderFollowCircle() {
    return image("sliderfollowcircle");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> sliderScorePoint() {
    return image("sliderscorepoint");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> reverseArrow() {
    return image("reversearrow");
  }
  // SkinnableLighting is a SkinnableSprite named "lighting", so this is the
  // element osu! skins provide it under. It used to look for "hitburst",
  // which is webosu-2's name for a different thing entirely -- a hard-edged
  // disc where osu!'s is a soft radial glow. A skin with no lighting of its
  // own gets none, which is what lazer does with one.
  [[nodiscard]] skia::Sp<skia::SkImage> hitBurst() { return image("lighting"); }
  [[nodiscard]] skia::Sp<skia::SkImage> followPoint() {
    return image("followpoint");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> cursor() { return image("cursor"); }
  [[nodiscard]] skia::Sp<skia::SkImage> cursorTrail() {
    return image("cursortrail");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> spinnerBase() {
    return image("spinnerbase");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> spinnerProgress() {
    return image("spinnerprogress");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> spinnerTop() {
    return image("spinnertop");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> hpBarLeft() {
    return image("hpbarleft");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> hpBarMid() { return image("hpbarmid"); }
  [[nodiscard]] skia::Sp<skia::SkImage> hpBarRight() {
    return image("hpbarright");
  }
  [[nodiscard]] skia::Sp<skia::SkImage> number(int digit) {
    if (digit < 0 || digit > 9)
      return nullptr;
    auto img = image("score-" + std::to_string(digit));
    if (!img)
      img = image("default-" + std::to_string(digit));
    return img;
  }

  void setComboColors(std::span<const std::array<std::uint8_t, 3>> colors) {
    fComboColors.clear();
    fComboColors.reserve(colors.size());
    for (const auto &c : colors) {
      fComboColors.push_back(skia::colorSetARGB(255, c[0], c[1], c[2]));
    }
  }

  void setDisableGlow(bool v) noexcept { fDisableGlow = v; }

  [[nodiscard]] const skia::Sp<skia::SkColorFilter> &
  tintFilter(skia::SkColor tint) {
    auto it = fTintFilters.find(tint);
    if (it != fTintFilters.end())
      return it->second;
    auto [ins, _] = fTintFilters.emplace(
        tint, skia::SkColorFilters::Blend(tint, skia::SkBlendMode::kSrcIn));
    return ins->second;
  }

  [[nodiscard]] skia::SkColor comboColor(std::size_t index) const {
    if (!fComboColors.empty()) {
      return fComboColors[index % fComboColors.size()];
    }
    constexpr std::array<skia::SkColor, 4> kColors{
        skia::colorSetARGB(255, 96, 159, 159),
        skia::colorSetARGB(255, 192, 192, 192),
        skia::colorSetARGB(255, 128, 255, 255),
        skia::colorSetARGB(255, 139, 191, 222),
    };
    return kColors[index % kColors.size()];
  }

  // `sizeScale` is the 1 -> 1.4 the circle grows by as it fades out after a
  // hit (LegacyMainCirclePiece).
  void drawHitCircle(skia::SkCanvas *canvas, osu::Vec2 pos, double time,
                     double now, double cs, double ar, int comboNumber,
                     std::size_t comboIndex, float alphaScale = 1.0f,
                     float sizeScale = 1.0f) {
    const double radius = detail::circleVisualRadius(cs) * sizeScale;
    const float x = static_cast<float>(pos.fX);
    const float y = static_cast<float>(pos.fY);

    const double preempt = osu::preemptTime(ar);
    const double fadeIn = osu::fadeInTime(ar);
    const double approachFadeIn = osu::approachFadeInTime(ar);
    const double elapsed = now - (time - preempt);
    const double fadeT = std::clamp(elapsed / fadeIn, 0.0, 1.0);
    const float alpha = static_cast<float>(fadeT) * alphaScale;

    const skia::SkColor tint = this->comboColor(comboIndex);
    const double hitSpriteScale = radius / 60.0;
    const auto &tintFilter = this->tintFilter(tint);

    const float hs = static_cast<float>(hitSpriteScale);
    const float r2 = static_cast<float>(radius * 2.0);
    const float hs05 = 0.5f * hs;

    auto circle = this->hitcircle();
    auto disc = this->disc();
    if (circle) {
      skia::SkPaint paint;
      paint.setAntiAlias(true);
      paint.setAlphaf(alpha);
      detail::drawImageCentered(canvas, circle.get(), x, y, r2, r2, paint);
    } else if (disc) {
      detail::drawImageCentered(canvas, disc.get(), x, y, hs05,
                                detail::tintedPaint(tintFilter, alpha));
    } else {
      skia::SkPaint fill;
      fill.setColor(skia::kCyan);
      fill.setStyle(skia::kFillStyle);
      fill.setAntiAlias(true);
      fill.setAlphaf(alpha);
      canvas->drawCircle(x, y, static_cast<float>(radius), fill);
    }

    auto overlay = this->hitcircleOverlay();
    if (overlay) {
      skia::SkPaint paint;
      paint.setAntiAlias(true);
      paint.setAlphaf(alpha);
      detail::drawImageCentered(canvas, overlay.get(), x, y, hs05, paint);
    } else if (!circle && !disc) {
      skia::SkPaint ring;
      ring.setColor(skia::kWhite);
      ring.setStyle(skia::kStrokeStyle);
      ring.setStrokeWidth(3.0f);
      ring.setAntiAlias(true);
      ring.setAlphaf(alpha);
      canvas->drawCircle(x, y, static_cast<float>(radius), ring);
    }

    if (!fDisableGlow) {
      auto glow = this->ringGlow();
      if (glow) {
        skia::SkPaint gp = detail::tintedPaint(tintFilter, alpha * 0.5f);
        gp.setBlendMode(skia::SkBlendMode::kPlus);
        detail::drawImageCentered(canvas, glow.get(), x, y, 0.46f * hs, gp);
      }
    }

    if (comboNumber >= 0) {
      this->drawComboNumber(canvas, comboNumber, x, y,
                            static_cast<float>(radius), alpha);
    }

    // DrawableHitCircle: the approach circle scales 4 -> 1 linearly over the
    // whole preempt, fades to 0.9 rather than to 1, and at the object's start
    // time fades out over 50ms instead of vanishing -- it stays on screen for
    // that long even once the circle has been hit.
    constexpr double kApproachFadeOut = 50.0;
    if (now <= time + kApproachFadeOut) {
      const double approachT = std::clamp((now - time) / preempt, -1.0, 0.0);
      const double approachScale = 1.0 - 3.0 * approachT;
      const double approachElapsed = now - (time - preempt);
      const double approachFadeT =
          std::clamp(approachElapsed / approachFadeIn, 0.0, 1.0);
      double approachOpacity = 0.9 * approachFadeT;
      if (now > time) {
        approachOpacity *= std::clamp(1.0 - (now - time) / kApproachFadeOut,
                                      0.0, 1.0);
      }
      const float approachAlpha =
          static_cast<float>(approachOpacity) * alphaScale;

      auto approach = this->approachCircle();
      if (approach) {
        detail::drawImageCentered(
            canvas, approach.get(), x, y,
            0.5f * static_cast<float>(hitSpriteScale * approachScale),
            detail::tintedPaint(tintFilter, approachAlpha));
      } else {
        skia::SkPaint stroke;
        stroke.setColor(skia::kWhite);
        stroke.setStyle(skia::kStrokeStyle);
        stroke.setStrokeWidth(2.0f);
        stroke.setAntiAlias(true);
        stroke.setAlphaf(approachAlpha);
        canvas->drawCircle(x, y, static_cast<float>(radius * approachScale),
                           stroke);
      }
    }
  }

  void precomputeSliderBodies(const osu::Beatmap &map,
                              const osu::ComboInfo &comboInfo, float scale,
                              skia::GrDirectContext *grContext) {
    fPrecomputedBodies.clear();
    fBodySegCache.clear();
    for (std::size_t i = 0; i < map.fObjects.size(); ++i) {
      if (auto *s = std::get_if<osu::Slider>(&map.fObjects[i])) {
        precomputeSliderBody(*s, i, map.fSliderPaths[i],
                             map.sliderSpanDuration(*s),
                             map.sliderTickDistance(*s), map.fDiff.fCs,
                             comboInfo.fIndices[i], scale, grContext);
      }
    }
  }

  // Moves the precomputed bodies out of GPU memory, once, after they have
  // been computed there. The shapes are built with SkSL, which a software GL
  // stack JITs better than a CPU rasteriser draws, but drawing the result on
  // a CPU canvas would read it back every frame -- so it is read back here
  // instead, at load.
  void flattenBodiesToRaster(skia::GrDirectContext *grContext) {
    for (auto &[key, body] : fPrecomputedBodies) {
      if (!body.image || !body.image->isTextureBacked()) {
        continue;
      }
      if (auto raster = body.image->makeRasterImage(grContext)) {
        body.image = std::move(raster);
      }
    }
  }

  void drawSlider(skia::SkCanvas *canvas, const osu::Slider &s,
                  std::size_t index, const osu::SliderPath &path,
                  double spanDuration, double tickDistance, double now,
                  double cs, double ar, double od, int comboNumber,
                  std::size_t comboIndex, float alphaScale = 1.0f,
                  float followScale = 0.0f, float followAlpha = 0.0f) {
    const double duration = std::max(0.001, spanDuration);
    const double end = s.fTime + duration * s.fRepeat;
    const double radius = detail::circleVisualRadius(cs);
    const double total = s.fPixelLength;
    const auto points = path.points();
    const skia::SkColor tint = this->comboColor(comboIndex);

    // Slider body alpha follows the same rule as the head circle.
    const double preempt = osu::preemptTime(ar);
    const double objectFadeIn = osu::fadeInTime(ar);
    const double elapsed = now - (s.fTime - preempt);
    const double noteFullAppear = preempt - objectFadeIn;
    const double diff = s.fTime - now;
    float bodyAlpha = 0.0f;
    if (diff <= preempt && diff > noteFullAppear) {
      bodyAlpha =
          static_cast<float>(std::clamp(elapsed / objectFadeIn, 0.0, 1.0));
    } else if (diff <= noteFullAppear) {
      bodyAlpha = 1.0f;
    }
    bodyAlpha *= alphaScale;

    if (bodyAlpha <= 0.0f)
      return;

    const auto bodyCacheKey = static_cast<std::uint64_t>(index) << 32 |
                              static_cast<std::uint64_t>(cs * 100.0);
    if (auto it = fPrecomputedBodies.find(bodyCacheKey);
        it != fPrecomputedBodies.end()) {
      skia::SkPaint bodyPaint;
      // No AA on these blits: adjacent tile rects with AA'd edges produce
      // half-coverage seams (a dark grid at tile boundaries). Edge smoothing
      // lives in the texture itself (SDF coverage ramp) and in linear
      // sampling; the rect edges sit in the transparent margin anyway.
      bodyPaint.setAntiAlias(false);
      bodyPaint.setAlphaf(bodyAlpha);
      const auto &body = it->second;
      if (!body.tiles.empty()) {
        const float xScale =
            body.width / static_cast<float>(body.image->width());
        const float yScale =
            body.height / static_cast<float>(body.image->height());
        for (const auto &tile : body.tiles) {
          canvas->drawImageRect(
              body.image.get(),
              skia::SkRect::MakeXYWH(static_cast<float>(tile.fLeft),
                                     static_cast<float>(tile.fTop),
                                     static_cast<float>(tile.width()),
                                     static_cast<float>(tile.height())),
              skia::SkRect::MakeXYWH(
                  body.originX + static_cast<float>(tile.fLeft) * xScale,
                  body.originY + static_cast<float>(tile.fTop) * yScale,
                  static_cast<float>(tile.width()) * xScale,
                  static_cast<float>(tile.height()) * yScale),
              skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &bodyPaint,
              skia::SkCanvas::kFast_SrcRectConstraint);
        }
      } else {
        canvas->drawImageRect(
            body.image.get(),
            skia::SkRect::MakeXYWH(body.originX, body.originY, body.width,
                                   body.height),
            skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &bodyPaint);
      }
    } else {
      throw std::runtime_error(
          std::format("Slider precomputed body not found for index {}", index));
    }

    this->drawHitCircle(canvas, s.fPos, s.fTime, now, cs, ar, comboNumber,
                        comboIndex, alphaScale);

    if (now >= s.fTime && now <= end) {
      const osu::Vec2 ball =
          osu::sliderBallPosition(path, now - s.fTime, duration, total);
      const float bx = static_cast<float>(ball.fX);
      const float by = static_cast<float>(ball.fY);

      auto ballImg = this->sliderB();
      skia::SkPaint paint;
      paint.setAntiAlias(true);
      if (ballImg) {
        detail::drawImageCentered(canvas, ballImg.get(), bx, by,
                                  0.5f * static_cast<float>(radius / 60.0),
                                  paint);
      } else {
        skia::SkPaint ballPaint;
        ballPaint.setColor(skia::kRed);
        ballPaint.setStyle(skia::kFillStyle);
        ballPaint.setAntiAlias(true);
        canvas->drawCircle(bx, by, static_cast<float>(radius), ballPaint);
      }

      // The follow circle's size and opacity are animated by the caller, which
      // is the only place that knows when tracking started, when a tick was
      // hit and when one was dropped. `followScale` is in the same units
      // LegacyFollowCircle works in, where 2 is the steady tracking size.
      if (followAlpha > 0.0f && followScale > 0.0f) {
        auto follow = this->sliderFollowCircle();
        const float rel = followScale * 0.5f;
        if (follow && !fDisableGlow) {
          skia::SkPaint followPaint;
          followPaint.setAntiAlias(true);
          followPaint.setAlphaf(followAlpha);
          followPaint.setBlendMode(skia::SkBlendMode::kPlus);
          detail::drawImageCentered(
              canvas, follow.get(), bx, by,
              0.9f * static_cast<float>(radius / 60.0) * rel, followPaint);
        } else if (follow) {
          skia::SkPaint followPaint;
          followPaint.setAntiAlias(true);
          followPaint.setAlphaf(followAlpha);
          detail::drawImageCentered(
              canvas, follow.get(), bx, by,
              0.9f * static_cast<float>(radius / 60.0) * rel, followPaint);
        } else {
          skia::SkPaint ringPaint;
          ringPaint.setColor(skia::kRed);
          ringPaint.setStyle(skia::kStrokeStyle);
          ringPaint.setStrokeWidth(3.0f);
          ringPaint.setAntiAlias(true);
          ringPaint.setAlphaf(0.7f * followAlpha);
          canvas->drawCircle(bx, by,
                             static_cast<float>(radius * 2.4) * rel, ringPaint);
        }
      }
    }
  }

  void precomputeSliderBody(const osu::Slider &s, std::size_t index,
                            const osu::SliderPath &path, double spanDuration,
                            double tickDistance, double cs,
                            std::size_t comboIndex, float scale,
                            skia::GrDirectContext *grContext) {
    const double radius = detail::circleVisualRadius(cs);
    const double total = s.fPixelLength;
    const auto points = path.points();
    const skia::SkColor tint = this->comboColor(comboIndex);

    if (auto texture = this->sliderTexture(tint); texture && !points.empty()) {
      std::vector<osu::Vec2> curve;
      curve.reserve(points.size());
      for (std::size_t i = 0; i < points.size(); ++i) {
        if (i == 0 || std::abs(points[i].fX - points[i - 1].fX) > 0.00001 ||
            std::abs(points[i].fY - points[i - 1].fY) > 0.00001) {
          curve.push_back(points[i]);
        }
      }
      if (curve.size() < 2)
        curve.assign(points.begin(), points.end());
      if (curve.size() < 2)
        return;

      const int nSegs = static_cast<int>(curve.size()) - 1;
      constexpr int kMaxSegs = 512;
      if (nSegs < 1 || nSegs > kMaxSegs)
        return;

      double minX = curve[0].fX, maxX = curve[0].fX;
      double minY = curve[0].fY, maxY = curve[0].fY;
      for (const auto &p : curve) {
        minX = std::min(minX, p.fX);
        maxX = std::max(maxX, p.fX);
        minY = std::min(minY, p.fY);
        maxY = std::max(maxY, p.fY);
      }
      minX -= radius;
      maxX += radius;
      minY -= radius;
      maxY += radius;
      const double bw = maxX - minX;
      const double bh = maxY - minY;
      if (bw <= 0.0 || bh <= 0.0)
        return;

      const int imgW = static_cast<int>(std::ceil(bw * scale));
      const int imgH = static_cast<int>(std::ceil(bh * scale));
      if (imgW <= 0 || imgH <= 0)
        return;
      // Create offscreen surface — GPU if available, raster fallback.
      skia::SkBitmap rasterBmp;
      skia::Sp<skia::SkSurface> gpuSurface;
      std::unique_ptr<skia::SkCanvas> rasterCanvas;
      skia::SkCanvas *oc = nullptr;
      if (grContext) {
        gpuSurface = skia::RenderTarget(
            grContext, skia::kNo,
            skia::SkImageInfo::Make(imgW, imgH, skia::kRGBA_8888_SkColorType,
                                    skia::kPremul_SkAlphaType));
      }
      if (gpuSurface) {
        oc = gpuSurface->getCanvas();
      } else {
        if (!rasterBmp.tryAllocPixels(skia::SkImageInfo::Make(
                imgW, imgH, skia::kRGBA_8888_SkColorType,
                skia::kPremul_SkAlphaType)))
          return;
        rasterCanvas = std::make_unique<skia::SkCanvas>(rasterBmp);
        oc = rasterCanvas.get();
      }
      oc->clear(0x00000000);
      oc->scale(scale, scale);
      oc->translate(static_cast<float>(-minX), static_cast<float>(-minY));

      static const skia::Sp<skia::SkRuntimeEffect> sBodyFx =
          []() -> skia::Sp<skia::SkRuntimeEffect> {
        constexpr const char *kSL = R"(
uniform shader gradientTex;
uniform shader segTex;
uniform float segCount;
uniform float bboxOriginX;
uniform float bboxOriginY;
uniform float bboxSizeX;
uniform float bboxSizeY;
uniform float bodyRadius;
uniform float aaPx;
uniform float originX;
uniform float originY;

float distToSegment(float2 p, float2 a, float2 b) {
  float2 ab  = b - a;
  float2 ap  = p - a;
  float  t   = clamp(dot(ap, ab) / dot(ab, ab), 0.0, 1.0);
  return distance(p, a + t * ab);
}

float4 main(float2 coords) {
  float2 p = coords + float2(originX, originY);
  float minDist   = 1e10;
  int   count     = int(segCount);
  float2 bbOrigin = float2(bboxOriginX, bboxOriginY);
  float2 bbSize   = float2(bboxSizeX, bboxSizeY);
  for (int i = 0; i < 512; i++) {
    if (i >= count) break;
    float2 uv1 = float2(float(i * 2) + 0.5, 0.5);
    float2 uv2 = float2(float(i * 2 + 1) + 0.5, 0.5);
    float4 sg1 = segTex.eval(uv1);
    float4 sg2 = segTex.eval(uv2);
    float2 st = bbOrigin + sg1.rg * bbSize;
    float2 en = bbOrigin + float2(sg1.b, sg2.r) * bbSize;
    float2 lo = min(st, en) - bodyRadius;
    float2 hi = max(st, en) + bodyRadius;
    if (all(greaterThanEqual(p, lo)) && all(lessThanEqual(p, hi)))
      minDist = min(minDist, distToSegment(p, st, en));
  }
  // Smooth the outer edge over ~one device pixel instead of a hard cut:
  // coverage ramps from 1 at (radius - aaPx) to 0 at radius. The gradient
  // texture is premultiplied, so scaling by coverage fades correctly.
  float cov = clamp((bodyRadius - minDist) / aaPx, 0.0, 1.0);
  if (cov <= 0.0) return float4(0, 0, 0, 0);
  float t = min(minDist / bodyRadius, 1.0);
  return gradientTex.eval(float2(t * 199.0, 0.5)) * cov;
}
)";
        auto [effect, err] =
            skia::SkRuntimeEffect::MakeForShader(skia::SkString(kSL));
        if (!effect) {
          static bool once = false;
          if (!once) {
            once = true;
            std::println("Slider shader compile error: {}", err.c_str());
          }
        }
        return effect;
      }();

      if (!sBodyFx)
        return;

      const auto cacheKey = static_cast<std::uint64_t>(index) << 32 |
                            static_cast<std::uint64_t>(cs * 100.0);
      auto &sBodySegCache = fBodySegCache;
      auto *cached = [&]() -> CachedSeg * {
        auto it = sBodySegCache.find(cacheKey);
        if (it != sBodySegCache.end())
          return &it->second;
        return nullptr;
      }();

      skia::Sp<skia::SkImage> segImg;
      float segBwExt, segBhExt, segBoxMinX, segBoxMinY;
      int segNSegs;

      std::vector<int> allIndices(nSegs);
      for (int i = 0; i < nSegs; ++i)
        allIndices[i] = i;

      if (cached) {
        segImg = cached->image;
        segBwExt = cached->bwExt;
        segBhExt = cached->bhExt;
        segBoxMinX = cached->boxMinX;
        segBoxMinY = cached->boxMinY;
        segNSegs = cached->nSegs;
      }

      const float boxMinX = static_cast<float>(minX + radius);
      const float boxMinY = static_cast<float>(minY + radius);
      const float bwExt = static_cast<float>((maxX - radius) - boxMinX);
      const float bhExt = static_cast<float>((maxY - radius) - boxMinY);
      const float invW = bwExt > 0.0f ? 1.0f / bwExt : 1.0f;
      const float invH = bhExt > 0.0f ? 1.0f / bhExt : 1.0f;

      auto encodeSegs =
          [&](std::span<const int> segIndices) -> skia::Sp<skia::SkImage> {
        const int n = static_cast<int>(segIndices.size());
        if (n < 1)
          return nullptr;
        const int nPix = std::max(4, n * 2);
        skia::SkBitmap segBmp;
        if (!segBmp.tryAllocPixels(
                skia::SkImageInfo::Make(nPix, 1, skia::kRGBA_8888_SkColorType,
                                        skia::kOpaque_SkAlphaType)))
          return nullptr;
        segBmp.eraseColor(0x00000000);
        for (int j = 0; j < n; ++j) {
          int i = segIndices[j];
          const std::uint8_t sx = static_cast<std::uint8_t>(std::clamp(
              (static_cast<float>(curve[i].fX) - boxMinX) * invW * 255.f + 0.5f,
              0.f, 255.f));
          const std::uint8_t sy = static_cast<std::uint8_t>(std::clamp(
              (static_cast<float>(curve[i].fY) - boxMinY) * invH * 255.f + 0.5f,
              0.f, 255.f));
          const std::uint8_t ex = static_cast<std::uint8_t>(std::clamp(
              (static_cast<float>(curve[i + 1].fX) - boxMinX) * invW * 255.f +
                  0.5f,
              0.f, 255.f));
          const std::uint8_t ey = static_cast<std::uint8_t>(std::clamp(
              (static_cast<float>(curve[i + 1].fY) - boxMinY) * invH * 255.f +
                  0.5f,
              0.f, 255.f));
          *segBmp.getAddr32(j * 2, 0) =
              (0xFFu << 24) | (static_cast<std::uint32_t>(ex) << 16) |
              (static_cast<std::uint32_t>(sy) << 8) | sx;
          *segBmp.getAddr32(j * 2 + 1, 0) =
              (0xFFu << 24) | (0u << 16) | (0u << 8) | ey;
        }
        return skia::RasterFromBitmap(segBmp);
      };

      auto drawTile = [&](float tx, float ty, float tw, float th,
                          std::span<const int> segIdx) {
        int nTile = static_cast<int>(segIdx.size());
        if (nTile < 1)
          return;
        auto tileImg = encodeSegs(segIdx);
        if (!tileImg)
          return;
        skia::SkRuntimeEffectBuilder b(sBodyFx);
        b.uniform("segCount") = static_cast<float>(nTile);
        b.uniform("bodyRadius") = static_cast<float>(radius);
        b.uniform("aaPx") = 1.0f / std::max(scale, 0.001f);
        b.uniform("bboxOriginX") = boxMinX;
        b.uniform("bboxOriginY") = boxMinY;
        b.uniform("bboxSizeX") = bwExt;
        b.uniform("bboxSizeY") = bhExt;
        b.uniform("originX") = tx;
        b.uniform("originY") = ty;
        b.child("gradientTex") = texture->makeShader(
            skia::SkTileMode::kClamp, skia::SkTileMode::kClamp,
            skia::SkSamplingOptions(skia::SkFilterMode::kLinear));
        b.child("segTex") = tileImg->makeShader(
            skia::SkTileMode::kClamp, skia::SkTileMode::kClamp,
            skia::SkSamplingOptions(skia::SkFilterMode::kNearest));
        skia::SkPaint p;
        p.setShader(b.makeShader());
        // No AA: adjacent tile rects share edges; AA'd edges blend at half
        // coverage twice and bake a dark seam grid into the texture. The
        // body edge is smoothed by the shader's coverage ramp instead.
        p.setAntiAlias(false);
        p.setAlphaf(1.0f);
        oc->save();
        oc->translate(tx, ty);
        oc->drawRect(skia::SkRect::MakeXYWH(0, 0, tw, th), p);
        oc->restore();
      };

      constexpr int kMaxPerTile = 48;
      bool tiled = false;
      if (!cached && nSegs > kMaxPerTile) {
        const float tileW =
            std::max(64.0f, static_cast<float>(bw) *
                                std::sqrt(static_cast<float>(kMaxPerTile) /
                                          static_cast<float>(nSegs)));
        const float tileH =
            std::max(64.0f, static_cast<float>(bh) *
                                std::sqrt(static_cast<float>(kMaxPerTile) /
                                          static_cast<float>(nSegs)));
        std::vector<int> tileSegs;
        tileSegs.reserve(nSegs);
        for (float ty = static_cast<float>(minY); ty < static_cast<float>(maxY);
             ty += tileH) {
          for (float tx = static_cast<float>(minX);
               tx < static_cast<float>(maxX); tx += tileW) {
            const float tex = tx + tileW;
            const float tey = ty + tileH;
            tileSegs.clear();
            for (int i = 0; i < nSegs; ++i) {
              float sx = static_cast<float>(curve[i].fX);
              float sy = static_cast<float>(curve[i].fY);
              float ex2 = static_cast<float>(curve[i + 1].fX);
              float ey2 = static_cast<float>(curve[i + 1].fY);
              float sloX = std::min(sx, ex2) - static_cast<float>(radius);
              float shiX = std::max(sx, ex2) + static_cast<float>(radius);
              float sloY = std::min(sy, ey2) - static_cast<float>(radius);
              float shiY = std::max(sy, ey2) + static_cast<float>(radius);
              if (sloX < tex && shiX > tx && sloY < tey && shiY > ty)
                tileSegs.push_back(i);
            }
            drawTile(tx, ty, std::min(tex, static_cast<float>(maxX)) - tx,
                     std::min(tey, static_cast<float>(maxY)) - ty, tileSegs);
          }
        }
        tiled = true;
      }

      if (!tiled) {
        if (!cached) {
          segImg = encodeSegs(allIndices);
          if (!segImg)
            return;

          sBodySegCache[cacheKey] =
              CachedSeg{segImg, nSegs, boxMinX, boxMinY, bwExt, bhExt};
          segBwExt = bwExt;
          segBhExt = bhExt;
          segBoxMinX = boxMinX;
          segBoxMinY = boxMinY;
          segNSegs = nSegs;
        }

        {
          skia::SkRuntimeEffectBuilder builder(sBodyFx);
          builder.uniform("segCount") = static_cast<float>(segNSegs);
          builder.uniform("bodyRadius") = static_cast<float>(radius);
          builder.uniform("aaPx") = 1.0f / std::max(scale, 0.001f);
          builder.uniform("bboxOriginX") = segBoxMinX;
          builder.uniform("bboxOriginY") = segBoxMinY;
          builder.uniform("bboxSizeX") = segBwExt;
          builder.uniform("bboxSizeY") = segBhExt;
          builder.uniform("originX") = static_cast<float>(minX);
          builder.uniform("originY") = static_cast<float>(minY);
          builder.child("gradientTex") = texture->makeShader(
              skia::SkTileMode::kClamp, skia::SkTileMode::kClamp,
              skia::SkSamplingOptions(skia::SkFilterMode::kLinear));
          builder.child("segTex") = segImg->makeShader(
              skia::SkTileMode::kClamp, skia::SkTileMode::kClamp,
              skia::SkSamplingOptions(skia::SkFilterMode::kNearest));

          skia::SkPaint bodyPaint;
          bodyPaint.setShader(builder.makeShader());
          bodyPaint.setAntiAlias(false);
          bodyPaint.setAlphaf(1.0f);
          oc->save();
          oc->translate(static_cast<float>(minX), static_cast<float>(minY));
          oc->drawRect(skia::SkRect::MakeXYWH(0, 0, static_cast<float>(bw),
                                              static_cast<float>(bh)),
                       bodyPaint);
          oc->restore();
        }
      }

      if (tickDistance > 1.0) {
        auto tickImg = this->sliderScorePoint();
        const float tickScale =
            tickImg ? 0.5f * static_cast<float>(radius / 60.0) : 0.15f;
        skia::SkPaint tickPaint;
        tickPaint.setAntiAlias(true);
        tickPaint.setAlphaf(1.0f);
        for (int span = 0; span < s.fRepeat; ++span) {
          const bool reverse = (span & 1) != 0;
          for (double d = tickDistance; d < total - 1.0; d += tickDistance) {
            const double dist = reverse ? total - d : d;
            const osu::Vec2 p = path.positionAt(dist);
            const float px = static_cast<float>(p.fX);
            const float py = static_cast<float>(p.fY);
            if (tickImg) {
              detail::drawImageCentered(oc, tickImg.get(), px, py, tickScale,
                                        tickPaint);
            } else {
              oc->drawCircle(px, py, static_cast<float>(radius * 0.15),
                             tickPaint);
            }
          }
        }
      }

      if (s.fRepeat > 1) {
        auto arrowImg = this->reverseArrow();
        const float arrowScale =
            arrowImg ? 0.36f * static_cast<float>(radius / 60.0) : 0.5f;
        skia::SkPaint arrowPaint;
        arrowPaint.setAntiAlias(true);
        arrowPaint.setAlphaf(1.0f);
        if (arrowImg)
          arrowPaint.setColorFilter(
              skia::SkColorFilters::Blend(tint, skia::SkBlendMode::kSrcIn));
        for (int span = 0; span < s.fRepeat - 1; ++span) {
          const bool reverse = (span & 1) != 0;
          const double dist = reverse ? 0.0 : total;
          const osu::Vec2 p = path.positionAt(dist);
          const double tangentDist = reverse ? 5.0 : total - 5.0;
          const osu::Vec2 t =
              path.positionAt(std::clamp(tangentDist, 0.0, total));
          const double angle = std::atan2(t.fY - p.fY, t.fX - p.fX) +
                               (reverse ? std::numbers::pi : 0.0);
          if (arrowImg) {
            oc->save();
            oc->translate(static_cast<float>(p.fX), static_cast<float>(p.fY));
            oc->rotate(static_cast<float>(angle * 180.0 / std::numbers::pi));
            detail::drawImageCentered(oc, arrowImg.get(), 0.0f, 0.0f,
                                      arrowScale, arrowPaint);
            oc->restore();
          } else {
            this->drawArrow(oc, static_cast<float>(p.fX),
                            static_cast<float>(p.fY), angle,
                            static_cast<float>(radius * 0.5), arrowPaint);
          }
        }
      }

      rasterCanvas.reset();
      auto image = gpuSurface ? gpuSurface->makeImageSnapshot()
                              : skia::RasterFromBitmap(rasterBmp);
      if (!image)
        return;

      constexpr int kTileSize = 64;
      std::vector<skia::SkIRect> tileRects;
      for (int ty = 0; ty < imgH; ty += kTileSize) {
        for (int tx = 0; tx < imgW; tx += kTileSize) {
          const int texx = std::min(tx + kTileSize, imgW);
          const int texy = std::min(ty + kTileSize, imgH);
          tileRects.push_back(skia::SkIRect::MakeLTRB(tx, ty, texx, texy));
        }
      }

      fPrecomputedBodies[cacheKey] = {image,
                                      static_cast<float>(minX),
                                      static_cast<float>(minY),
                                      static_cast<float>(bw),
                                      static_cast<float>(bh),
                                      std::move(tileRects)};
    }
  }

  // `radius` is already the animated outer radius; `fill` is how much of it
  // the progress disc covers, `fillAlpha` its opacity (idle or tracking), and
  // `rotation` the ambient spin, in degrees.
  void drawSpinner(skia::SkCanvas *canvas, double cx, double cy, double radius,
                   double progress, double fill = 1.0, float fillAlpha = 0.4f,
                   double rotation = 0.0, double centreScale = 0.5) {
    if (radius <= 0.0) {
      return;
    }
    canvas->save();
    canvas->translate(static_cast<float>(cx), static_cast<float>(cy));
    canvas->rotate(static_cast<float>(rotation));
    canvas->translate(-static_cast<float>(cx), -static_cast<float>(cy));
    this->drawSpinnerPieces(canvas, cx, cy, radius, progress, fill, fillAlpha,
                            centreScale);
    canvas->restore();
  }

  void drawSpinnerPieces(skia::SkCanvas *canvas, double cx, double cy,
                         double radius, double progress, double fill,
                         float fillAlpha, double centreScale) {
    auto base = this->spinnerBase();
    auto prog = this->spinnerProgress();
    auto top = this->spinnerTop();
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    if (base) {
      detail::drawImageCentered(canvas, base.get(), static_cast<float>(cx),
                                static_cast<float>(cy),
                                static_cast<float>(radius * 2.0),
                                static_cast<float>(radius * 2.0), paint);
    }
    // SpinnerFill is a disc that grows with progress rather than a pie: it
    // starts at a fifth of the disc and reaches the whole of it when the
    // spinner is cleared. Its opacity is the caller's, being 0.2 while idle
    // and 0.4 while the spinner is being turned.
    const double fillRadius = radius * std::clamp(fill, 0.0, 1.0);
    if (prog) {
      skia::SkPaint fp = paint;
      fp.setAlphaf(fillAlpha);
      const float size = static_cast<float>(fillRadius * 2.0);
      detail::drawImageCentered(canvas, prog.get(), static_cast<float>(cx),
                                static_cast<float>(cy), size, size, fp);
    } else {
      skia::SkPaint fillPaint;
      fillPaint.setColor(skia::colorSetARGB(255, 51, 88, 152));
      fillPaint.setStyle(skia::kFillStyle);
      fillPaint.setAntiAlias(true);
      fillPaint.setAlphaf(fillAlpha);
      canvas->drawCircle(static_cast<float>(cx), static_cast<float>(cy),
                         static_cast<float>(fillRadius), fillPaint);
    }
    // SpinnerCentreLayer, which sits at its own scale on top.
    if (centreScale > 0.0) {
      skia::SkPaint centre;
      centre.setColor(skia::kWhite);
      centre.setStyle(skia::kStrokeStyle);
      centre.setStrokeWidth(2.0f);
      centre.setAntiAlias(true);
      centre.setAlphaf(0.8f);
      canvas->drawCircle(static_cast<float>(cx), static_cast<float>(cy),
                         static_cast<float>(radius * centreScale), centre);
    }
    static_cast<void>(progress);
    if (top) {
      detail::drawImageCentered(canvas, top.get(), static_cast<float>(cx),
                                static_cast<float>(cy),
                                static_cast<float>(radius * 2.0),
                                static_cast<float>(radius * 2.0), paint);
    }
  }

  void drawHitBurst(skia::SkCanvas *canvas, osu::Vec2 pos, double cs,
                    double age, skia::SkColor tint) {
    auto burst = this->hitBurst();
    if (!burst)
      return;
    // DrawableOsuJudgement.ApplyHitAnimations: the lighting grows from 0.8 to
    // 1.2 over 600ms eased out, fades in over 200, holds for 200 and fades
    // out over a full second. It lives for 1400ms, far longer than the
    // judgement text it sits behind.
    constexpr double kFadeIn = 200.0;
    constexpr double kHold = 200.0;
    constexpr double kFadeOut = 1000.0;
    constexpr double kGrow = 600.0;

    const double growT = std::clamp(age / kGrow, 0.0, 1.0);
    const double scale = 0.8 + 0.4 * growT * (2.0 - growT); // Easing.Out
    double alpha;
    if (age < kFadeIn) {
      alpha = age / kFadeIn;
    } else if (age < kFadeIn + kHold) {
      alpha = 1.0;
    } else {
      alpha = 1.0 - (age - kFadeIn - kHold) / kFadeOut;
    }
    alpha = std::clamp(alpha, 0.0, 1.0);
    if (alpha <= 0.0)
      return;

    const double hitSpriteScale = osu::circleRadius(cs) / 60.0;
    // SkinnableLighting.updateColour tints by the judgement's own colour, not
    // by the combo colour, which is what this was using.
    skia::SkPaint paint =
        detail::tintedPaint(this->tintFilter(tint), static_cast<float>(alpha));
    // DrawableOsuJudgement sets Blending = BlendingParameters.Additive on the
    // lighting. Without it this is a solid disc laid over the playfield --
    // which is what it looked like once the alpha stopped being capped at 0.8
    // and started holding at full for 400ms.
    if (!fDisableGlow) {
      paint.setBlendMode(skia::SkBlendMode::kPlus);
    }
    detail::drawImageCentered(canvas, burst.get(), static_cast<float>(pos.fX),
                              static_cast<float>(pos.fY),
                              static_cast<float>(scale * hitSpriteScale),
                              paint);
  }

  // `scaleMul` is the 1.5 -> 1 pop a follow point does as it fades in.
  void drawFollowPoint(skia::SkCanvas *canvas, osu::Vec2 pos, double angle,
                       float alpha, double cs, float scaleMul = 1.0f) {
    auto img = this->followPoint();
    if (!img)
      return;
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(alpha);
    if (!fDisableGlow)
      paint.setBlendMode(skia::SkBlendMode::kPlus);
    const double hitSpriteScale = osu::circleRadius(cs) / 60.0;
    canvas->save();
    canvas->translate(static_cast<float>(pos.fX), static_cast<float>(pos.fY));
    canvas->rotate(static_cast<float>(angle * 180.0 / std::numbers::pi));
    detail::drawImageCentered(
        canvas, img.get(), 0.0f, 0.0f,
        static_cast<float>(hitSpriteScale * 0.3) * scaleMul, paint);
    canvas->restore();
  }

  void drawArrow(skia::SkCanvas *canvas, float x, float y, double angle,
                 float radius, const skia::SkPaint &paint) {
    const float ax = x + radius * static_cast<float>(std::cos(angle));
    const float ay = y + radius * static_cast<float>(std::sin(angle));
    const float lx =
        x + radius * 0.6f * static_cast<float>(std::cos(angle + 2.5));
    const float ly =
        y + radius * 0.6f * static_cast<float>(std::sin(angle + 2.5));
    const float rx =
        x + radius * 0.6f * static_cast<float>(std::cos(angle - 2.5));
    const float ry =
        y + radius * 0.6f * static_cast<float>(std::sin(angle - 2.5));
    skia::SkPathBuilder builder;
    builder.moveTo(ax, ay);
    builder.lineTo(lx, ly);
    builder.lineTo(rx, ry);
    canvas->drawPath(builder.detach(), paint);
  }

  void drawCursor(skia::SkCanvas *canvas, osu::Vec2 pos, float scale) {
    auto cursorImg = this->cursor();
    const float x = static_cast<float>(pos.fX);
    const float y = static_cast<float>(pos.fY);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    if (cursorImg) {
      const float s = 0.35f * scale;
      detail::drawImageCentered(canvas, cursorImg.get(), x, y, s, paint);
    } else {
      skia::SkPaint fill;
      fill.setColor(skia::kRed);
      fill.setStyle(skia::kFillStyle);
      fill.setAntiAlias(true);
      canvas->drawCircle(x, y, 4.0f * scale, fill);
    }
  }

  void drawCursorTrail(skia::SkCanvas *canvas, osu::Vec2 pos, float scale,
                       float alpha) {
    auto img = this->cursorTrail();
    const float x = static_cast<float>(pos.fX);
    const float y = static_cast<float>(pos.fY);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(alpha);
    if (img) {
      if (!fDisableGlow)
        paint.setBlendMode(skia::SkBlendMode::kPlus);
      detail::drawImageCentered(canvas, img.get(), x, y, 0.35f * scale, paint);
    } else {
      paint.setColor(skia::kWhite);
      paint.setStyle(skia::kFillStyle);
      float r = 4.0f * scale * (1.0f - alpha * 0.5f);
      if (r > 0.0f) {
        canvas->drawCircle(x, y, r, paint);
      }
    }
  }

  void drawComboNumber(skia::SkCanvas *canvas, int number, float x, float y,
                       float radius, float alpha = 1.0f) {
    std::string digits = std::to_string(number);
    if (digits.empty())
      return;

    float totalWidth = 0.0f;
    std::vector<float> widths;
    std::vector<float> heights;
    widths.reserve(digits.size());
    heights.reserve(digits.size());
    for (std::size_t i = 0; i < digits.size(); ++i) {
      auto img = this->number(digits[i] - '0');
      float w = img ? static_cast<float>(img->width()) : radius * 0.6f;
      float h = img ? static_cast<float>(img->height()) : radius * 0.6f;
      const float aspect = h > 0.0f ? w / h : 1.0f;
      const float scaleMul = (i == 0) ? 0.4f : 0.35f;
      const float targetH = radius * scaleMul;
      w = targetH * aspect;
      h = targetH;
      widths.push_back(w);
      heights.push_back(h);
      totalWidth += w;
    }

    float cx = x - totalWidth * 0.5f;
    const float cy = y;
    fComboPaint.setAlphaf(alpha);
    for (std::size_t i = 0; i < digits.size(); ++i) {
      auto img = this->number(digits[i] - '0');
      const float w = widths[i];
      const float h = heights[i];
      if (img) {
        detail::drawImageCentered(canvas, img.get(), cx + w * 0.5f, cy, w, h,
                                  fComboPaint);
      } else {
        fComboFallbackPaint.setAlphaf(alpha);
        canvas->drawRect(skia::SkRect::MakeXYWH(cx, cy - h * 0.5f, w, h),
                         fComboFallbackPaint);
      }
      cx += w;
    }
  }

private:
  std::filesystem::path fRoot;
  std::unordered_map<std::string, skia::Sp<skia::SkImage>> fImages;
  std::vector<skia::SkColor> fComboColors;
  std::unordered_map<skia::SkColor, skia::Sp<skia::SkImage>> fSliderTextures;
  std::unordered_map<skia::SkColor, skia::Sp<skia::SkColorFilter>> fTintFilters;
  bool fDisableGlow = false;
  skia::SkPaint fComboPaint;
  skia::SkPaint fComboFallbackPaint;

  struct PrecomputedBody {
    skia::Sp<skia::SkImage> image;
    float originX, originY;
    float width, height;
    std::vector<skia::SkIRect> tiles;
  };
  std::unordered_map<std::uint64_t, PrecomputedBody> fPrecomputedBodies;
  // Per-map cache of encoded segment textures; keyed by object index, so
  // it MUST be cleared when a different map is precomputed (same index ->
  // different slider). Used to be a function-local static: harmless with
  // one map per process, wrong the moment song select exists.
  struct CachedSeg {
    skia::Sp<skia::SkImage> image;
    int nSegs;
    float boxMinX, boxMinY;
    float bwExt, bhExt;
  };
  std::unordered_map<std::uint64_t, CachedSeg> fBodySegCache;

  [[nodiscard]] skia::Sp<skia::SkImage> sliderTexture(skia::SkColor tint) {
    if (auto it = fSliderTextures.find(tint); it != fSliderTextures.end()) {
      return it->second;
    }
    constexpr int kWidth = 200;
    constexpr double kBorderFraction = 0.128;
    constexpr double kInnerPortion = 1.0 - kBorderFraction;
    constexpr double kEdgeOpacity = 0.8;
    constexpr double kCenterOpacity = 0.3;
    constexpr double kBlurRate = 0.015;

    const double innerR = static_cast<double>((tint >> 16) & 0xFF) / 255.0;
    const double innerG = static_cast<double>((tint >> 8) & 0xFF) / 255.0;
    const double innerB = static_cast<double>(tint & 0xFF) / 255.0;
    constexpr double innerA = 1.0;
    constexpr double borderR = 1.0;
    constexpr double borderG = 1.0;
    constexpr double borderB = 1.0;
    constexpr double borderA = 1.0;

    skia::SkBitmap bitmap;
    constexpr int kGradH = 4;
    const skia::SkImageInfo info =
        skia::SkImageInfo::Make(kWidth, kGradH, skia::kRGBA_8888_SkColorType,
                                skia::kPremul_SkAlphaType);
    if (!bitmap.tryAllocPixels(info))
      return nullptr;

    for (int x = 0; x < kWidth; ++x) {
      const double position = static_cast<double>(x) / kWidth;
      double R, G, B, A;
      if (position >= kInnerPortion) {
        R = borderR;
        G = borderG;
        B = borderB;
        A = borderA;
      } else {
        R = innerR;
        G = innerG;
        B = innerB;
        A = innerA *
            ((kEdgeOpacity - kCenterOpacity) * position / kInnerPortion +
             kCenterOpacity);
      }
      R *= A;
      G *= A;
      B *= A;
      if (1.0 - position < kBlurRate) {
        const double f = (1.0 - position) / kBlurRate;
        R *= f;
        G *= f;
        B *= f;
        A *= f;
      }
      if (kInnerPortion - position > 0.0 &&
          kInnerPortion - position < kBlurRate) {
        const double mu = (kInnerPortion - position) / kBlurRate;
        R = mu * R + (1.0 - mu) * borderR * borderA;
        G = mu * G + (1.0 - mu) * borderG * borderA;
        B = mu * B + (1.0 - mu) * borderB * borderA;
        A = mu * innerA + (1.0 - mu) * borderA;
      }
      const std::uint8_t a =
          static_cast<std::uint8_t>(std::clamp(A * 255.0, 0.0, 255.0));
      const std::uint8_t r =
          static_cast<std::uint8_t>(std::clamp(R * 255.0, 0.0, 255.0));
      const std::uint8_t g =
          static_cast<std::uint8_t>(std::clamp(G * 255.0, 0.0, 255.0));
      const std::uint8_t b =
          static_cast<std::uint8_t>(std::clamp(B * 255.0, 0.0, 255.0));
      const auto pix = (static_cast<std::uint32_t>(a) << 24) |
                       (static_cast<std::uint32_t>(b) << 16) |
                       (static_cast<std::uint32_t>(g) << 8) |
                       static_cast<std::uint32_t>(r);
      for (int y = 0; y < kGradH; ++y)
        *bitmap.getAddr32(x, y) = pix;
    }

    skia::Sp<skia::SkImage> image = skia::RasterFromBitmap(bitmap);
    fSliderTextures[tint] = image;
    return image;
  }
};

export [[nodiscard]] inline skia::Sp<skia::SkImage>
loadImage(const std::filesystem::path &path) {
  return detail::decodeImage(path);
}

export [[nodiscard]] inline skia::Sp<skia::SkImage>
loadImage(std::span<const std::uint8_t> bytes) {
  return detail::decodeImage(bytes);
}

} // namespace client
