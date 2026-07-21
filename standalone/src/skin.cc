export module skin;

import std;
import skia;
import osu;

namespace client {

namespace detail {

inline skia::Sp<skia::SkImage> decodeImage(const std::filesystem::path &path) {
  skia::Sp<skia::SkData> data = skia::SkData::MakeFromFileName(path.c_str());
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

    // Slider body: webosu-2 SliderMesh geometry with mitered inside joints
    // to remove the bright overlapping triangles at sharp turns.
    if (auto texture = this->sliderTexture(tint); texture && !points.empty()) {
      // Filter duplicate points.
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

      const std::size_t m = curve.size();
      struct Seg {
        osu::Vec2 u;
        osu::Vec2 n;
      };
      std::vector<Seg> segs;
      segs.reserve(m - 1);
      for (std::size_t i = 0; i + 1 < m; ++i) {
        const double dx = curve[i + 1].fX - curve[i].fX;
        const double dy = curve[i + 1].fY - curve[i].fY;
        const double len = std::hypot(dx, dy);
        if (len > 1e-6) {
          segs.push_back(
              {{dx / len, dy / len}, {-dy / len * radius, dx / len * radius}});
        } else {
          segs.push_back({{1.0, 0.0}, {0.0, radius}});
        }
      }

      struct Joint {
        double cross;
        osu::Vec2 miter;
      };
      std::vector<Joint> joints(m);
      for (std::size_t i = 1; i + 1 < m; ++i) {
        const auto &a = segs[i - 1];
        const auto &b = segs[i];
        const double dot = a.u.fX * b.u.fX + a.u.fY * b.u.fY;
        const double cross = a.u.fX * b.u.fY - b.u.fX * a.u.fY;
        const double denom = 1.0 + dot;
        osu::Vec2 miter;
        if (denom > 1e-6) {
          miter = {(a.n.fX + b.n.fX) / denom, (a.n.fY + b.n.fY) / denom};
        } else {
          miter = a.n;
        }
        joints[i] = {cross, miter};
      }

      auto side1Start = [&](std::size_t i) -> osu::Vec2 {
        if (i == 0)
          return {curve[0].fX + segs[0].n.fX, curve[0].fY + segs[0].n.fY};
        const auto &j = joints[i];
        if (j.cross > 0.0)
          return {curve[i].fX + j.miter.fX, curve[i].fY + j.miter.fY};
        return {curve[i].fX + segs[i].n.fX, curve[i].fY + segs[i].n.fY};
      };
      auto side2Start = [&](std::size_t i) -> osu::Vec2 {
        if (i == 0)
          return {curve[0].fX - segs[0].n.fX, curve[0].fY - segs[0].n.fY};
        const auto &j = joints[i];
        if (j.cross < 0.0)
          return {curve[i].fX - j.miter.fX, curve[i].fY - j.miter.fY};
        return {curve[i].fX - segs[i].n.fX, curve[i].fY - segs[i].n.fY};
      };
      auto side1End = [&](std::size_t i) -> osu::Vec2 {
        const std::size_t k = i + 1;
        if (k + 1 == m)
          return {curve[k].fX + segs[i].n.fX, curve[k].fY + segs[i].n.fY};
        const auto &j = joints[k];
        if (j.cross > 0.0)
          return {curve[k].fX + j.miter.fX, curve[k].fY + j.miter.fY};
        return {curve[k].fX + segs[i].n.fX, curve[k].fY + segs[i].n.fY};
      };
      auto side2End = [&](std::size_t i) -> osu::Vec2 {
        const std::size_t k = i + 1;
        if (k + 1 == m)
          return {curve[k].fX - segs[i].n.fX, curve[k].fY - segs[i].n.fY};
        const auto &j = joints[k];
        if (j.cross < 0.0)
          return {curve[k].fX - j.miter.fX, curve[k].fY - j.miter.fY};
        return {curve[k].fX - segs[i].n.fX, curve[k].fY - segs[i].n.fY};
      };

      std::vector<osu::Vec2> vpos;
      std::vector<skia::SkPoint> vtexs;
      std::vector<std::uint16_t> indices;
      vpos.reserve(curve.size() * 5 + 64 * 3);
      vtexs.reserve(vpos.capacity());

      const float texW = static_cast<float>(texture->width());
      auto texFromOffset = [&](const osu::Vec2 &off) -> skia::SkPoint {
        const double d =
            std::clamp(std::hypot(off.fX, off.fY) / radius, 0.0, 1.0);
        return {static_cast<float>(d * texW), 0.5f};
      };
      auto pushVert = [&](const osu::Vec2 &p, const osu::Vec2 &off) {
        vpos.push_back(p);
        vtexs.push_back(texFromOffset(off));
      };

      pushVert(curve[0], {0.0, 0.0});
      for (std::size_t i = 0; i + 1 < m; ++i) {
        const osu::Vec2 s1 = side1Start(i);
        const osu::Vec2 s2 = side2Start(i);
        const osu::Vec2 e1 = side1End(i);
        const osu::Vec2 e2 = side2End(i);

        pushVert(s1, {s1.fX - curve[i].fX, s1.fY - curve[i].fY});
        pushVert(s2, {s2.fX - curve[i].fX, s2.fY - curve[i].fY});
        pushVert(e1, {e1.fX - curve[i + 1].fX, e1.fY - curve[i + 1].fY});
        pushVert(e2, {e2.fX - curve[i + 1].fX, e2.fY - curve[i + 1].fY});
        pushVert(curve[i + 1], {0.0, 0.0});

        const std::uint16_t n = static_cast<std::uint16_t>(5 * (i + 1) + 1);
        indices.insert(indices.end(), {static_cast<std::uint16_t>(n - 6),
                                       static_cast<std::uint16_t>(n - 5),
                                       static_cast<std::uint16_t>(n - 1),
                                       static_cast<std::uint16_t>(n - 5),
                                       static_cast<std::uint16_t>(n - 1),
                                       static_cast<std::uint16_t>(n - 3),
                                       static_cast<std::uint16_t>(n - 6),
                                       static_cast<std::uint16_t>(n - 4),
                                       static_cast<std::uint16_t>(n - 1),
                                       static_cast<std::uint16_t>(n - 4),
                                       static_cast<std::uint16_t>(n - 1),
                                       static_cast<std::uint16_t>(n - 2)});
      }

      constexpr int kDivides = 64;
      auto addArc = [&](std::uint16_t c, std::uint16_t p1, std::uint16_t p2) {
        double theta1 =
            std::atan2(vpos[p1].fY - vpos[c].fY, vpos[p1].fX - vpos[c].fX);
        double theta2 =
            std::atan2(vpos[p2].fY - vpos[c].fY, vpos[p2].fX - vpos[c].fX);
        if (theta1 > theta2)
          theta2 += 2.0 * std::numbers::pi;
        const double theta = theta2 - theta1;
        const int divs = static_cast<int>(
            std::ceil(kDivides * std::abs(theta) / (2.0 * std::numbers::pi)));
        std::uint16_t last = p1;
        for (int j = 1; j < divs; ++j) {
          const double angle = theta1 + theta * j / divs;
          const osu::Vec2 off = {std::cos(angle) * radius,
                                 std::sin(angle) * radius};
          pushVert(vpos[c] + off, off);
          const std::uint16_t newv =
              static_cast<std::uint16_t>(vpos.size() - 1);
          indices.insert(indices.end(), {c, last, newv});
          last = newv;
        }
        indices.insert(indices.end(), {c, last, p2});
      };

      const std::uint16_t lastCenter = static_cast<std::uint16_t>(5 * (m - 1));

      // Start cap.
      addArc(0, 1, 2);
      // End cap.
      addArc(lastCenter, static_cast<std::uint16_t>(lastCenter - 1),
             static_cast<std::uint16_t>(lastCenter - 2));
      // Joint arcs (outside only).
      for (std::size_t i = 1; i + 1 < m; ++i) {
        const std::uint16_t center = static_cast<std::uint16_t>(5 * i);
        if (joints[i].cross > 0.0) {
          addArc(center, static_cast<std::uint16_t>(center - 1),
                 static_cast<std::uint16_t>(center + 2));
        } else if (joints[i].cross < 0.0) {
          addArc(center, static_cast<std::uint16_t>(center + 1),
                 static_cast<std::uint16_t>(center - 2));
        }
      }

      std::vector<skia::SkPoint> verts;
      std::vector<skia::SkPoint> texs;
      verts.reserve(vpos.size());
      texs.reserve(vpos.size());
      for (std::size_t i = 0; i < vpos.size(); ++i) {
        verts.push_back(
            {static_cast<float>(vpos[i].fX), static_cast<float>(vpos[i].fY)});
        texs.push_back(vtexs[i]);
      }

      std::vector<skia::SkColor> colors;
      colors.assign(verts.size(), skia::colorSetARGB(static_cast<std::uint8_t>(
                                                         bodyAlpha * 255.0f),
                                                     255, 255, 255));

      skia::SkPaint bodyPaint;
      bodyPaint.setShader(texture->makeShader(
          skia::SkTileMode::kClamp, skia::SkTileMode::kClamp,
          skia::SkSamplingOptions(skia::SkFilterMode::kLinear)));
      bodyPaint.setColor(skia::kWhite);
      bodyPaint.setAntiAlias(true);

      skia::Sp<skia::SkVertices> vertices = skia::SkVertices::MakeCopy(
          skia::SkVertices::kTriangles_VertexMode, verts.size(), verts.data(),
          texs.data(), colors.data(), indices.size(), indices.data());
      canvas->drawVertices(vertices.get(), skia::SkBlendMode::kModulate,
                           bodyPaint);
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
    const skia::SkImageInfo info = skia::SkImageInfo::MakeN32Premul(kWidth, 1);
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
      *bitmap.getAddr32(x, 0) = skia::colorSetARGB(a, r, g, b);
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

} // namespace client
