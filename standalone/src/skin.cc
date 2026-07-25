export module skin;

import std;
import skia;
import osu;

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
  std::string lower(base);
  std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

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

inline skia::SkPaint tintedPaint(skia::SkColor tint, float alpha = 1.0f) {
  skia::SkPaint paint;
  paint.setAntiAlias(true);
  paint.setAlphaf(alpha);
  paint.setColorFilter(
      skia::SkColorFilters::Blend(tint, skia::SkBlendMode::kSrcIn));
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
  explicit Skin(std::filesystem::path root = {}) : fRoot(std::move(root)) {}

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
  [[nodiscard]] skia::Sp<skia::SkImage> hitBurst() { return image("hitburst"); }
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

  void drawHitCircle(skia::SkCanvas *canvas, osu::Vec2 pos, double time,
                     double now, double cs, double ar, int comboNumber,
                     std::size_t comboIndex, float alphaScale = 1.0f) {
    const double radius = detail::circleVisualRadius(cs);
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

    auto circle = this->hitcircle();
    auto disc = this->disc();
    if (circle) {
      skia::SkPaint paint;
      paint.setAntiAlias(true);
      paint.setAlphaf(alpha);
      detail::drawImageCentered(canvas, circle.get(), x, y,
                                static_cast<float>(radius * 2.0),
                                static_cast<float>(radius * 2.0), paint);
    } else if (disc) {
      skia::SkPaint paint = detail::tintedPaint(tint, alpha);
      detail::drawImageCentered(canvas, disc.get(), x, y,
                                0.5f * static_cast<float>(hitSpriteScale),
                                paint);
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
      detail::drawImageCentered(canvas, overlay.get(), x, y,
                                0.5f * static_cast<float>(hitSpriteScale),
                                paint);
    } else if (!circle && !disc) {
      skia::SkPaint ring;
      ring.setColor(skia::kWhite);
      ring.setStyle(skia::kStrokeStyle);
      ring.setStrokeWidth(3.0f);
      ring.setAntiAlias(true);
      ring.setAlphaf(alpha);
      canvas->drawCircle(x, y, static_cast<float>(radius), ring);
    }

    auto glow = this->ringGlow();
    if (glow) {
      skia::SkPaint paint = detail::tintedPaint(tint, alpha * 0.5f);
      paint.setBlendMode(skia::SkBlendMode::kPlus);
      detail::drawImageCentered(canvas, glow.get(), x, y,
                                0.46f * static_cast<float>(hitSpriteScale),
                                paint);
    }

    if (comboNumber >= 0) {
      this->drawComboNumber(canvas, comboNumber, x, y,
                            static_cast<float>(radius), alpha);
    }

    if (now <= time) {
      const double approachT = std::clamp((now - time) / preempt, -1.0, 0.0);
      const double approachScale = 1.0 - 3.0 * approachT;
      const double approachElapsed = now - (time - preempt);
      const double approachFadeT =
          std::clamp(approachElapsed / approachFadeIn, 0.0, 1.0);
      const float approachAlpha =
          static_cast<float>(approachFadeT) * alphaScale;

      skia::SkPaint paint = detail::tintedPaint(tint, approachAlpha);
      auto approach = this->approachCircle();
      if (approach) {
        detail::drawImageCentered(
            canvas, approach.get(), x, y,
            0.5f * static_cast<float>(hitSpriteScale * approachScale), paint);
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

  void drawSlider(skia::SkCanvas *canvas, const osu::Slider &s,
                  std::size_t index, const osu::SliderPath &path,
                  double spanDuration, double tickDistance, double now,
                  double cs, double ar, double od, int comboNumber,
                  std::size_t comboIndex, float alphaScale = 1.0f,
                  bool tracking = false) {
    const double duration = std::max(0.001, spanDuration);
    const double end = s.fTime + duration * s.fRepeat;
    const double radius = detail::circleVisualRadius(cs);
    const double total = s.fPixelLength;
    const auto points = path.points();
    const skia::SkColor tint = this->comboColor(comboIndex);

    skia::SkPathBuilder builder;
    if (!points.empty()) {
      builder.moveTo(static_cast<float>(points.front().fX),
                     static_cast<float>(points.front().fY));
      for (std::size_t i = 1; i < points.size(); ++i) {
        builder.lineTo(static_cast<float>(points[i].fX),
                       static_cast<float>(points[i].fY));
      }
    }
    skia::SkPath skPath = builder.detach();

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

    // Slider body: per-pixel distance-field shader over the full bounding
    // box. The shader computes the minimum distance from each pixel to all
    // centerline segments and uses it to index into the 1D gradient texture.
    // Overlaps are resolved naturally (no double-blending).
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
  float t = minDist / bodyRadius;
  if (t >= 1.0) return float4(0, 0, 0, 0);
  return gradientTex.eval(float2(t * 199.0, 0.5));
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

      if (!sBodyFx) {
        static bool once = false;
        if (!once) {
          once = true;
          std::println("Slider shader is null, falling back");
        }
        return;
      }

      // Build RGBA8 segment texture: 2 pixels per segment, kOpaque.
      // Cached segment texture and bbox uniforms.
      struct CachedSeg {
        skia::Sp<skia::SkImage> image;
        int nSegs;
        float boxMinX, boxMinY;
        float bwExt, bhExt;
      };
      const auto cacheKey = static_cast<std::uint64_t>(index) << 32 |
                            static_cast<std::uint64_t>(cs * 100.0);
      static std::unordered_map<std::uint64_t, CachedSeg> sBodySegCache;
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
      const float bwExt =
          static_cast<float>((maxX - radius) - boxMinX);
      const float bhExt =
          static_cast<float>((maxY - radius) - boxMinY);
      const float invW = bwExt > 0.0f ? 1.0f / bwExt : 1.0f;
      const float invH = bhExt > 0.0f ? 1.0f / bhExt : 1.0f;

      auto encodeSegs = [&](std::span<const int> segIndices)
          -> skia::Sp<skia::SkImage> {
        const int n = static_cast<int>(segIndices.size());
        if (n < 1) return nullptr;
        const int nPix = std::max(4, n * 2);
        skia::SkBitmap bmp;
        if (!bmp.tryAllocPixels(skia::SkImageInfo::Make(
                nPix, 1, skia::kRGBA_8888_SkColorType,
                skia::kOpaque_SkAlphaType)))
          return nullptr;
        bmp.eraseColor(0x00000000);
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
          *bmp.getAddr32(j * 2, 0) =
              (0xFFu << 24) | (static_cast<std::uint32_t>(ex) << 16) |
              (static_cast<std::uint32_t>(sy) << 8) | sx;
          *bmp.getAddr32(j * 2 + 1, 0) =
              (0xFFu << 24) | (0u << 16) | (0u << 8) | ey;
        }
        return skia::RasterFromBitmap(bmp);
      };

      auto drawTile = [&](float tx, float ty, float tw, float th,
                          std::span<const int> segIdx) {
        int nTile = static_cast<int>(segIdx.size());
        if (nTile < 1) return;
        auto tileImg = encodeSegs(segIdx);
        if (!tileImg) return;
        skia::SkRuntimeEffectBuilder b(sBodyFx);
        b.uniform("segCount") = static_cast<float>(nTile);
        b.uniform("bodyRadius") = static_cast<float>(radius);
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
        p.setAntiAlias(true);
        p.setAlphaf(bodyAlpha);
        canvas->save();
        canvas->translate(tx, ty);
        canvas->drawRect(
            skia::SkRect::MakeXYWH(0, 0, tw, th), p);
        canvas->restore();
      };

      constexpr int kMaxPerTile = 48;
      bool tiled = false;
      if (!cached && nSegs > kMaxPerTile) {
        const float tileW =
            std::max(64.0f, static_cast<float>(bw) * std::sqrt(
                static_cast<float>(kMaxPerTile) / static_cast<float>(nSegs)));
        const float tileH =
            std::max(64.0f, static_cast<float>(bh) * std::sqrt(
                static_cast<float>(kMaxPerTile) / static_cast<float>(nSegs)));
        std::vector<int> tileSegs;
        tileSegs.reserve(nSegs);
        for (float ty = static_cast<float>(minY);
             ty < static_cast<float>(maxY); ty += tileH) {
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
          if (!segImg) return;

          sBodySegCache[cacheKey] = CachedSeg{
              segImg, nSegs, boxMinX, boxMinY, bwExt, bhExt};
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
          bodyPaint.setAntiAlias(true);
          bodyPaint.setAlphaf(bodyAlpha);
          canvas->save();
          canvas->translate(static_cast<float>(minX), static_cast<float>(minY));
          canvas->drawRect(skia::SkRect::MakeXYWH(0, 0,
                                                   static_cast<float>(bw),
                                                   static_cast<float>(bh)),
                           bodyPaint);
          canvas->restore();
        }
      }
    }

    // Slider ticks.
    if (tickDistance > 1.0) {
      auto tickImg = this->sliderScorePoint();
      const float tickScale =
          tickImg ? 0.5f * static_cast<float>(radius / 60.0) : 0.15f;
      skia::SkPaint tickPaint;
      tickPaint.setAntiAlias(true);
      tickPaint.setAlphaf(bodyAlpha);
      for (int span = 0; span < s.fRepeat; ++span) {
        const bool reverse = (span & 1) != 0;
        for (double d = tickDistance; d < total - 1.0; d += tickDistance) {
          const double dist = reverse ? total - d : d;
          const osu::Vec2 p = path.positionAt(dist);
          const float px = static_cast<float>(p.fX);
          const float py = static_cast<float>(p.fY);
          if (tickImg) {
            detail::drawImageCentered(canvas, tickImg.get(), px, py, tickScale,
                                      tickPaint);
          } else {
            canvas->drawCircle(px, py, static_cast<float>(radius * 0.15),
                               tickPaint);
          }
        }
      }
    }

    // Reverse arrows at span ends (except the final end).
    if (s.fRepeat > 1) {
      auto arrowImg = this->reverseArrow();
      const float arrowScale =
          arrowImg ? 0.36f * static_cast<float>(radius / 60.0) : 0.5f;
      skia::SkPaint arrowPaint;
      arrowPaint.setAntiAlias(true);
      arrowPaint.setAlphaf(bodyAlpha);
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
          canvas->save();
          canvas->translate(static_cast<float>(p.fX), static_cast<float>(p.fY));
          canvas->rotate(static_cast<float>(angle * 180.0 / std::numbers::pi));
          detail::drawImageCentered(canvas, arrowImg.get(), 0.0f, 0.0f,
                                    arrowScale, arrowPaint);
          canvas->restore();
        } else {
          this->drawArrow(canvas, static_cast<float>(p.fX),
                          static_cast<float>(p.fY), angle,
                          static_cast<float>(radius * 0.5), arrowPaint);
        }
      }
    }

    this->drawHitCircle(canvas, s.fPos, s.fTime, now, cs, ar, comboNumber,
                        comboIndex, alphaScale);

    if (now >= s.fTime && now <= end) {
      const double local = now - s.fTime;
      const int spanIdx = static_cast<int>(local / duration);
      const double dInSpan = std::fmod(local, duration);
      const double dist =
          (spanIdx & 1) == 0
              ? std::min(dInSpan / duration, 1.0) * total
              : (1.0 - std::min(dInSpan / duration, 1.0)) * total;
      const osu::Vec2 ball = path.positionAt(dist);
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

      if (tracking) {
        auto follow = this->sliderFollowCircle();
        if (follow) {
          skia::SkPaint followPaint;
          followPaint.setAntiAlias(true);
          followPaint.setBlendMode(skia::SkBlendMode::kPlus);
          detail::drawImageCentered(canvas, follow.get(), bx, by,
                                    0.9f * static_cast<float>(radius / 60.0),
                                    followPaint);
        } else {
          skia::SkPaint ringPaint;
          ringPaint.setColor(skia::kRed);
          ringPaint.setStyle(skia::kStrokeStyle);
          ringPaint.setStrokeWidth(3.0f);
          ringPaint.setAntiAlias(true);
          ringPaint.setAlphaf(0.7f);
          canvas->drawCircle(bx, by, static_cast<float>(radius * 2.4),
                             ringPaint);
        }
      }
    }
  }

  void drawSpinner(skia::SkCanvas *canvas, double cx, double cy, double radius,
                   double progress) {
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
    if (prog) {
      const float size = static_cast<float>(radius * 2.0);
      canvas->drawArc(
          skia::SkRect::MakeXYWH(static_cast<float>(cx - radius),
                                 static_cast<float>(cy - radius), size, size),
          -90.0f, static_cast<float>(progress * 360.0), true, paint);
    } else {
      skia::SkPaint fill;
      fill.setColor(skia::kCyan);
      fill.setStyle(skia::kFillStyle);
      fill.setAntiAlias(true);
      fill.setAlphaf(0.4f);
      const float size = static_cast<float>(radius * 2.0);
      canvas->drawArc(skia::SkRect::MakeXYWH(static_cast<float>(cx - radius),
                                             static_cast<float>(cy - radius),
                                             size, size),
                      -90.0f, static_cast<float>(progress * 360.0), true, fill);
    }
    if (top) {
      detail::drawImageCentered(canvas, top.get(), static_cast<float>(cx),
                                static_cast<float>(cy),
                                static_cast<float>(radius * 2.0),
                                static_cast<float>(radius * 2.0), paint);
    }
  }

  void drawHitBurst(skia::SkCanvas *canvas, osu::Vec2 pos, double cs,
                    double age, std::size_t comboIndex) {
    auto burst = this->hitBurst();
    if (!burst)
      return;
    constexpr double kFlashFadeIn = 40.0;
    constexpr double kFlashFadeOut = 120.0;
    constexpr double kGlowFadeOut = 350.0;
    constexpr double kFlashMaxOpacity = 0.8;

    double alpha;
    double scale = 1.0;
    if (age < kFlashFadeIn) {
      alpha = age / kFlashFadeIn;
    } else {
      alpha = 1.0 - (age - kFlashFadeIn) / kFlashFadeOut;
      const double t = age / kGlowFadeOut;
      scale = 1.0 + 0.5 * t * (2.0 - t);
    }
    alpha = std::clamp(alpha, 0.0, 1.0) * kFlashMaxOpacity;
    if (alpha <= 0.0)
      return;

    const double hitSpriteScale = osu::circleRadius(cs) / 60.0;
    skia::SkPaint paint = detail::tintedPaint(this->comboColor(comboIndex),
                                              static_cast<float>(alpha));
    detail::drawImageCentered(canvas, burst.get(), static_cast<float>(pos.fX),
                              static_cast<float>(pos.fY),
                              static_cast<float>(scale * hitSpriteScale),
                              paint);
  }

  void drawFollowPoint(skia::SkCanvas *canvas, osu::Vec2 pos, double angle,
                       float alpha, double cs) {
    auto img = this->followPoint();
    if (!img)
      return;
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(alpha);
    paint.setBlendMode(skia::SkBlendMode::kPlus);
    const double hitSpriteScale = osu::circleRadius(cs) / 60.0;
    canvas->save();
    canvas->translate(static_cast<float>(pos.fX), static_cast<float>(pos.fY));
    canvas->rotate(static_cast<float>(angle * 180.0 / std::numbers::pi));
    detail::drawImageCentered(canvas, img.get(), 0.0f, 0.0f,
                              static_cast<float>(hitSpriteScale * 0.3), paint);
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
    if (!img)
      return;
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(alpha);
    paint.setBlendMode(skia::SkBlendMode::kPlus);
    detail::drawImageCentered(canvas, img.get(), static_cast<float>(pos.fX),
                              static_cast<float>(pos.fY), 0.35f * scale, paint);
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
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setAlphaf(alpha);
    for (std::size_t i = 0; i < digits.size(); ++i) {
      auto img = this->number(digits[i] - '0');
      const float w = widths[i];
      const float h = heights[i];
      if (img) {
        detail::drawImageCentered(canvas, img.get(), cx + w * 0.5f, cy, w, h,
                                  paint);
      } else {
        skia::SkPaint textPaint;
        textPaint.setColor(skia::kWhite);
        textPaint.setStyle(skia::kFillStyle);
        textPaint.setAntiAlias(true);
        textPaint.setAlphaf(alpha);
        canvas->drawRect(skia::SkRect::MakeXYWH(cx, cy - h * 0.5f, w, h),
                         textPaint);
      }
      cx += w;
    }
  }

private:
  std::filesystem::path fRoot;
  std::unordered_map<std::string, skia::Sp<skia::SkImage>> fImages;
  std::vector<skia::SkColor> fComboColors;
  std::unordered_map<skia::SkColor, skia::Sp<skia::SkImage>> fSliderTextures;

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
        skia::SkImageInfo::MakeN32Premul(kWidth, kGradH);
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
      const auto pix = skia::colorSetARGB(a, r, g, b);
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
