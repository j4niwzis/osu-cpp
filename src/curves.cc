export module osu.curves;

import std;
import osu.types;

export namespace osu {

class SliderPath {
public:
  SliderPath() = default;
  SliderPath(CurveType type, std::span<const Vec2> controlPoints,
             double pixelLength) {
    this->bake(type, controlPoints, pixelLength);
  }

  [[nodiscard]] static SliderPath from(const Slider &s) {
    return SliderPath(s.fCurveType, s.fControl, s.fPixelLength);
  }

  [[nodiscard]] double length() const noexcept { return fTotalLength; }
  [[nodiscard]] bool empty() const noexcept { return fPoints.empty(); }
  [[nodiscard]] std::span<const Vec2> points() const noexcept {
    return fPoints;
  }

  [[nodiscard]] Vec2 positionAt(double d) const noexcept {
    if (fPoints.empty()) {
      return {};
    }
    if (d <= 0.0) {
      return fPoints.front();
    }
    if (d >= fTotalLength || fCumulative.size() < 2) {
      return fPoints.back();
    }
    const auto it = std::ranges::upper_bound(fCumulative, d);
    const auto i =
        static_cast<std::size_t>(std::distance(fCumulative.begin(), it));
    const double segLen = fCumulative[i] - fCumulative[i - 1];
    const double t = segLen > 0.0 ? (d - fCumulative[i - 1]) / segLen : 0.0;
    return fPoints[i - 1].lerp(fPoints[i], t);
  }

private:
  std::vector<Vec2> fPoints;
  std::vector<double> fCumulative;
  double fTotalLength = 0.0;

  void bake(CurveType type, std::span<const Vec2> control, double pixelLength);
  void finish(double pixelLength);
};

namespace detail {

inline void appendPoint(std::vector<Vec2> &out, Vec2 p) {
  if (out.empty() || out.back().distanceTo(p) > 1e-4) {
    out.push_back(p);
  }
}

inline void subdivideBezier(std::span<const Vec2> ctrl, std::vector<Vec2> &out,
                            int depth = 0) {
  // PathApproximator.BEZIER_TOLERANCE. The flatness test compares against
  // TOLERANCE_SQ * 4, so this constant is the framework's 0.25 and not half
  // of it: subdividing finer than lazer does gives a longer polyline and a
  // slightly different slider length, which then moves the duration, the
  // ticks and the star rating.
  constexpr double kTolerance = 0.25;
  constexpr int kMaxDepth = 14;

  const std::size_t n = ctrl.size();

  // osu!lazer 2nd-order finite-difference flatness test:
  //   control points form a straight line when each interior P[i]
  //   lies close to the midpoint of P[i-1] and P[i+1].
  bool flat = true;
  for (std::size_t i = 1; i + 1 < n; ++i) {
    const Vec2 d{
        ctrl[i - 1].fX - 2.0 * ctrl[i].fX + ctrl[i + 1].fX,
        ctrl[i - 1].fY - 2.0 * ctrl[i].fY + ctrl[i + 1].fY,
    };
    if (d.dot(d) > kTolerance * kTolerance * 4.0) {
      flat = false;
      break;
    }
  }

  // De Casteljau subdivision at t=0.5, producing left/right sub-curves.
  auto subdivide = [&](auto &l, auto &r) {
    std::vector<Vec2> tmp(ctrl.begin(), ctrl.end());
    l[0] = tmp[0];
    r[n - 1] = tmp[n - 1];
    for (std::size_t level = 0; level < n - 1; ++level) {
      for (std::size_t i = 0; i < n - level - 1; ++i) {
        tmp[i] = tmp[i].lerp(tmp[i + 1], 0.5);
      }
      l[level + 1] = tmp[0];
      r[n - level - 2] = tmp[n - level - 2];
    }
  };

  if (depth >= kMaxDepth || flat) {
    // osu!lazer bezierApproximate: subdivide once more, then emit
    // start point + smoothed interior points from the twice-subdivided
    // control cage. This produces n output points per flat segment
    // instead of just 1, giving 4x-18x denser sampling.
    std::vector<Vec2> lsub(n), rsub(n);
    subdivide(lsub, rsub);

    // L = left[0..n-1] + right[1..n-1] (skip shared midpoint right[0]).
    std::vector<Vec2> L(2 * n - 1);
    for (std::size_t i = 0; i < n; ++i)
      L[i] = lsub[i];
    for (std::size_t i = 1; i < n; ++i)
      L[n + i - 1] = rsub[i];

    // Emit start point, then for each interior control index i,
    // the smoothed point 0.25*(L[2i-1] + 2*L[2i] + L[2i+1]).
    appendPoint(out, ctrl.front());
    for (std::size_t i = 1; i + 1 < n; ++i) {
      const std::size_t idx = 2 * i;
      const Vec2 p{
          0.25 * (L[idx - 1].fX + 2.0 * L[idx].fX + L[idx + 1].fX),
          0.25 * (L[idx - 1].fY + 2.0 * L[idx].fY + L[idx + 1].fY),
      };
      appendPoint(out, p);
    }
    appendPoint(out, ctrl.back());
    return;
  }

  // Not flat: subdivide and recurse into both halves.
  std::vector<Vec2> left(n), right(n);
  subdivide(left, right);
  subdivideBezier(std::span<const Vec2>(left.data(), n), out, depth + 1);
  subdivideBezier(std::span<const Vec2>(right.data(), n), out, depth + 1);
}

// A repeated control point ends one bezier segment and starts the next --
// that is how the legacy format spells "corner here", and how a slider ends
// up with several independent curves. Running one de Casteljau over the whole
// list instead draws a quite different shape, which is what this did.
inline void bake(curve::Bezier, std::span<const Vec2> ctrl,
                 std::vector<Vec2> &out) {
  if (ctrl.size() < 2) {
    std::ranges::copy(ctrl, std::back_inserter(out));
    return;
  }
  appendPoint(out, ctrl.front());
  std::size_t start = 0;
  for (std::size_t i = 1; i < ctrl.size(); ++i) {
    if (ctrl[i].distanceTo(ctrl[i - 1]) > 1e-6) {
      continue;
    }
    const auto segment = ctrl.subspan(start, i - start);
    if (segment.size() >= 2) {
      subdivideBezier(segment, out);
    } else if (!segment.empty()) {
      appendPoint(out, segment.front());
    }
    start = i;
  }
  const auto tail = ctrl.subspan(start);
  if (tail.size() >= 2) {
    subdivideBezier(tail, out);
  } else if (!tail.empty()) {
    appendPoint(out, tail.front());
  }
}

inline void bake(curve::Linear, std::span<const Vec2> ctrl,
                 std::vector<Vec2> &out) {
  for (const Vec2 p : ctrl) {
    appendPoint(out, p);
  }
}

inline void bake(curve::Catmull, std::span<const Vec2> ctrl,
                 std::vector<Vec2> &out) {
  // PathApproximator.catmull_detail, and the same handling of the ends:
  // beyond the last control point the spline is extrapolated rather than
  // clamped.
  constexpr int kStepsPerSegment = 50;
  const std::size_t n = ctrl.size();
  if (n < 2) {
    std::ranges::copy(ctrl, std::back_inserter(out));
    return;
  }
  appendPoint(out, ctrl[0]);
  for (std::size_t i = 0; i + 1 < n; ++i) {
    const Vec2 p0 = ctrl[i == 0 ? 0 : i - 1];
    const Vec2 p1 = ctrl[i];
    const Vec2 p2 = i + 1 < n ? ctrl[i + 1] : p1 + p1 - p0;
    const Vec2 p3 = i + 2 < n ? ctrl[i + 2] : p2 + p2 - p1;
    for (int step = 1; step <= kStepsPerSegment; ++step) {
      const double t = static_cast<double>(step) / kStepsPerSegment;
      const double t2 = t * t;
      const double t3 = t2 * t;
      const Vec2 p = (p1 * 2.0 + (p2 - p0) * t +
                      (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2 +
                      (-p0 + p1 * 3.0 - p2 * 3.0 + p3) * t3) *
                     0.5;
      appendPoint(out, p);
    }
  }
}

inline void bake(curve::Perfect tag, std::span<const Vec2> ctrl,
                 std::vector<Vec2> &out) {
  // A perfect curve is three points and nothing else; the legacy parser
  // downgrades anything else to a bezier rather than to a line.
  if (ctrl.size() != 3) {
    bake(curve::Bezier{}, ctrl, out);
    return;
  }
  const auto [a, b, c] = std::tie(ctrl[0], ctrl[1], ctrl[2]);
  const double d = 2.0 * (a.fX * (b.fY - c.fY) + b.fX * (c.fY - a.fY) +
                          c.fX * (a.fY - b.fY));
  if (std::abs(d) < 1e-6) {
    bake(curve::Linear{}, ctrl, out);
    return;
  }
  const double a2 = a.dot(a);
  const double b2 = b.dot(b);
  const double c2 = c.dot(c);
  const Vec2 center{
      (a2 * (b.fY - c.fY) + b2 * (c.fY - a.fY) + c2 * (a.fY - b.fY)) / d,
      (a2 * (c.fX - b.fX) + b2 * (a.fX - c.fX) + c2 * (b.fX - a.fX)) / d};
  const double radius = center.distanceTo(a);

  const double start = center.angleTo(a);
  const double mid = center.angleTo(b);
  const double end = center.angleTo(c);
  const auto normalize = [](double ang) {
    while (ang < 0.0)
      ang += 2.0 * std::numbers::pi;
    while (ang >= 2.0 * std::numbers::pi)
      ang -= 2.0 * std::numbers::pi;
    return ang;
  };
  const double s = normalize(start);
  const double m = normalize(mid);
  const double e = normalize(end);

  const double ccwSpan =
      std::fmod(e - s + 2.0 * std::numbers::pi, 2.0 * std::numbers::pi);
  const double ccwMid =
      std::fmod(m - s + 2.0 * std::numbers::pi, 2.0 * std::numbers::pi);
  const bool counterClockwise = ccwMid <= ccwSpan;
  const double span =
      counterClockwise ? ccwSpan : ccwSpan - 2.0 * std::numbers::pi;

  // CircularArcToPiecewiseLinear: enough points that the discrete curvature
  // stays under the framework's tolerance of 0.1, which is
  // ceil(thetaRange / (2 * acos(1 - tolerance / radius))).
  constexpr double kArcTolerance = 0.1;
  const double thetaRange = std::abs(span);
  const int steps =
      2.0 * radius <= kArcTolerance
          ? 2
          : std::max(2, static_cast<int>(std::ceil(
                            thetaRange /
                            (2.0 * std::acos(1.0 - kArcTolerance / radius)))));
  for (int i = 0; i < steps; ++i) {
    const double fract = static_cast<double>(i) / (steps - 1);
    const double ang = s + span * fract;
    appendPoint(out, {center.fX + radius * std::cos(ang),
                      center.fY + radius * std::sin(ang)});
  }
}

} // namespace detail

inline void SliderPath::bake(CurveType type, std::span<const Vec2> control,
                             double pixelLength) {
  fPoints.clear();
  fCumulative.clear();
  fTotalLength = 0.0;
  if (control.empty()) {
    return;
  }

  std::visit([&](auto tag) { detail::bake(tag, control, fPoints); }, type);
  this->finish(pixelLength);
}

inline void SliderPath::finish(double pixelLength) {
  if (fPoints.size() < 2) {
    fCumulative.assign(fPoints.size(), 0.0);
    return;
  }
  fCumulative.resize(fPoints.size());
  fCumulative[0] = 0.0;
  for (std::size_t i = 1; i < fPoints.size(); ++i) {
    fCumulative[i] = fCumulative[i - 1] + fPoints[i - 1].distanceTo(fPoints[i]);
  }
  fTotalLength = fCumulative.back();

  if (pixelLength > 0.0 && fTotalLength > pixelLength) {
    const Vec2 tail = this->positionAt(pixelLength);
    const auto it = std::ranges::upper_bound(fCumulative, pixelLength);
    const auto keep =
        static_cast<std::size_t>(std::distance(fCumulative.begin(), it));
    fPoints.resize(keep);
    fCumulative.resize(keep);
    fPoints.push_back(tail);
    fCumulative.push_back(pixelLength);
    fTotalLength = pixelLength;
  }
}

[[nodiscard]] inline Vec2 sliderBallPosition(const SliderPath &path,
                                             double localTime,
                                             double spanDuration,
                                             double pixelLength) noexcept {
  if (spanDuration <= 0.0 || localTime < 0.0)
    return path.positionAt(0.0);
  const int spanIdx = static_cast<int>(localTime / spanDuration);
  const double dInSpan = std::fmod(localTime, spanDuration);
  const double dist =
      (spanIdx & 1) == 0
          ? std::min(dInSpan / spanDuration, 1.0) * pixelLength
          : (1.0 - std::min(dInSpan / spanDuration, 1.0)) * pixelLength;
  return path.positionAt(dist);
}

} // namespace osu
