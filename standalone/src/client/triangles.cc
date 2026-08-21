export module client.triangles;

import std;
import skia;

// osu.Game.Graphics.Backgrounds.TrianglesV2, as a thing that can be dropped
// into any box: the main menu has its own copy of this from before there was
// anywhere else to put it, and the buttons of the pause overlay have one each.
//
// The numbers are lazer's: triangles 100 across at scale 1, sqrt(3)/2 tall,
// carried upwards at 50 px/s times Velocity, count = width * 0.02 * SpawnRatio,
// speed multipliers drawn from a normal(0.5, 0.16) and never below 0.1, and a
// border a fiftieth of the triangle's own size -- their TriangleBorder shader
// draws the outline and leaves the middle empty, which is why these are
// stroked rather than filled.
export namespace client::triangles {

inline constexpr float kSize = 100.0f;
inline constexpr float kBaseVelocity = 50.0f;
inline constexpr float kEquilateralRatio = 0.866f;

class Field {
public:
  void setVelocity(float velocity) { fVelocity = velocity; }
  void setSpawnRatio(float ratio) { fSpawnRatio = ratio; }
  void setScaleAdjust(float scale) { fScaleAdjust = scale; }
  // TrianglesV2.Thickness: the border as a fraction of a triangle's size.
  void setThickness(float thickness) { fThickness = thickness; }
  // Lerped down the box, the way lazer tints the ones inside the logo.
  void setColours(skia::SkColor top, skia::SkColor bottom) {
    fTop = top;
    fBottom = bottom;
  }
  // The band the per-triangle shade picks from, so a field can be uniform
  // (both ends the same) or vary with depth as the background one does.
  void setAlphaRange(float low, float high) {
    fAlphaLow = low;
    fAlphaHigh = high;
  }

  // Advances by dt and draws inside the box. Positions are relative to it, as
  // lazer's are, so a box that changes size keeps its triangles.
  void draw(skia::SkCanvas *canvas, const skia::SkRect &bounds, double dtMs,
            float alpha, skia::SkBlendMode blend = skia::SkBlendMode::kSrcOver) {
    const float width = bounds.width();
    const float height = bounds.height();
    if (width <= 0.0f || height <= 0.0f || alpha <= 0.001f) {
      return;
    }
    this->refill(width);

    // Triangles integrate time linearly, so a frame that arrives late moves
    // them proportionally far. Capped at one triangle's own height per frame,
    // which is the distance past which the drift stops reading as drift and
    // starts reading as a jump -- a number in the field's own units rather
    // than a guess at how late a frame is allowed to be.
    const float elapsed = static_cast<float>(dtMs) / 1000.0f;
    const float travel = elapsed * fVelocity * kBaseVelocity;
    const float step = kSize * fScaleAdjust * kEquilateralRatio;
    const float moved = std::min(travel, step) / height;
    const float size = kSize * fScaleAdjust;

    skia::SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(skia::kStrokeStyle);
    paint.setBlendMode(blend);

    for (auto &part : fParts) {
      part.fY -= std::max(0.5f, part.fSpeed) * moved;
      const float w = size * part.fScale;
      const float h = w * kEquilateralRatio;
      if (part.fY * height + h < 0.0f) {
        // Off the top: back to the bottom as a new one, which is what
        // CreateNewTriangles amounts to when the count is fixed.
        part = this->spawn(false);
      }
      const float cx = bounds.fLeft + part.fX * width;
      const float cy = bounds.fTop + part.fY * height;
      skia::SkPathBuilder builder;
      builder.moveTo(cx, cy - h * 0.5f);
      builder.lineTo(cx - w * 0.5f, cy + h * 0.5f);
      builder.lineTo(cx + w * 0.5f, cy + h * 0.5f);
      builder.close();
      paint.setStrokeWidth(std::max(1.0f, w * fThickness * 2.0f));
      // Clamped: a particle above the top edge or waiting to respawn has a y
      // outside [0,1], and an unclamped lerp wraps the colour bytes.
      const float depth = std::clamp(part.fY, 0.0f, 1.0f);
      paint.setColor(lerpColour(fTop, fBottom, depth));
      paint.setAlphaf(alpha *
                      (fAlphaLow + (fAlphaHigh - fAlphaLow) * part.fShade));
      canvas->drawPath(builder.detach(), paint);
    }
  }

private:
  [[nodiscard]] static skia::SkColor lerpColour(skia::SkColor from,
                                                skia::SkColor to, float t) {
    const auto channel = [](skia::SkColor c, int shift) {
      return static_cast<float>((c >> shift) & 0xffu);
    };
    const auto mix = [t](float a, float b) {
      return static_cast<std::uint8_t>(a + (b - a) * t);
    };
    return skia::colorSetARGB(mix(channel(from, 24), channel(to, 24)),
                              mix(channel(from, 16), channel(to, 16)),
                              mix(channel(from, 8), channel(to, 8)),
                              mix(channel(from, 0), channel(to, 0)));
  }

  struct Particle {
    float fX = 0.0f;
    float fY = 0.0f;
    float fScale = 1.0f;
    float fSpeed = 1.0f;
    float fShade = 0.5f;
  };

  void refill(float width) {
    // AimCount = clamp(width * 0.02 * SpawnRatio, 1, max).
    //
    // lazer takes that width from the layout, which its Scale transforms do
    // not touch. What arrives here is the box as drawn, and the boxes this
    // goes in are animated: the logo beats with the music, grows under the
    // pointer and is punched on a click, and the pause buttons swell on
    // hover. Recounting from that every frame makes the aim wobble by one,
    // and a count that drops by one deletes a triangle mid-flight while a
    // count that rises spawns one out of nowhere -- which reads as triangles
    // blinking in and out.
    //
    // So the count is held until the width has moved by more than one
    // triangle's worth of it. An animation of a few percent leaves it alone;
    // a window resize still takes effect.
    const float per = std::max(1e-4f, 0.02f * fSpawnRatio);
    if (!fParts.empty() && std::abs(width - fCountedWidth) * per < 1.0f) {
      return;
    }
    fCountedWidth = width;
    const auto aim = static_cast<std::size_t>(
        std::clamp(width * per, 1.0f, 64.0f));
    while (fParts.size() < aim) {
      fParts.push_back(this->spawn(true));
    }
    while (fParts.size() > aim) {
      fParts.pop_back();
    }
  }

  [[nodiscard]] Particle spawn(bool randomY) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    Particle part;
    part.fX = unit(fRng);
    part.fY = randomY ? unit(fRng) : 1.0f + kEquilateralRatio * 0.5f;
    part.fScale = 0.6f + 0.8f * unit(fRng);
    part.fShade = unit(fRng);
    // A normal(0.5, 0.16), the way CreateTriangle draws its speeds.
    const float u1 = 1.0f - unit(fRng);
    const float u2 = 1.0f - unit(fRng);
    const float normal = std::sqrt(-2.0f * std::log(u1)) *
                         std::sin(2.0f * std::numbers::pi_v<float> * u2);
    part.fSpeed = std::max(0.5f + 0.16f * normal, 0.1f);
    return part;
  }

  std::vector<Particle> fParts;
  float fCountedWidth = 0.0f;
  std::mt19937 fRng{std::random_device{}()};
  float fVelocity = 1.0f;
  float fSpawnRatio = 1.0f;
  float fScaleAdjust = 1.0f;
  float fThickness = 0.02f;
  float fAlphaLow = 0.6f;
  float fAlphaHigh = 1.0f;
  skia::SkColor fTop = skia::kWhite;
  skia::SkColor fBottom = skia::kWhite;
};

} // namespace client::triangles
