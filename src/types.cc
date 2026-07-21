export module osu.types;

import std;

export namespace osu {

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};

inline constexpr double kPlayfieldWidth = 512.0;
inline constexpr double kPlayfieldHeight = 384.0;

struct Vec2 {
  double fX = 0.0;
  double fY = 0.0;

  constexpr Vec2 operator+(Vec2 rhs) const noexcept {
    return {fX + rhs.fX, fY + rhs.fY};
  }
  constexpr Vec2 operator-(Vec2 rhs) const noexcept {
    return {fX - rhs.fX, fY - rhs.fY};
  }
  constexpr Vec2 operator-() const noexcept { return {-fX, -fY}; }
  constexpr Vec2 operator*(double k) const noexcept { return {fX * k, fY * k}; }
  constexpr Vec2 operator/(double k) const noexcept { return {fX / k, fY / k}; }
  constexpr Vec2 &operator+=(Vec2 rhs) noexcept {
    fX += rhs.fX;
    fY += rhs.fY;
    return *this;
  }

  [[nodiscard]] constexpr double dot(Vec2 rhs) const noexcept {
    return fX * rhs.fX + fY * rhs.fY;
  }
  [[nodiscard]] double length() const noexcept { return std::hypot(fX, fY); }
  [[nodiscard]] double distanceTo(Vec2 rhs) const noexcept {
    return (*this - rhs).length();
  }
  [[nodiscard]] Vec2 lerp(Vec2 rhs, double t) const noexcept {
    return *this + (rhs - *this) * t;
  }
  [[nodiscard]] double angleTo(Vec2 rhs) const noexcept {
    return std::atan2(rhs.fY - fY, rhs.fX - fX);
  }

  bool operator==(const Vec2 &) const = default;
};

inline constexpr Vec2 kPlayfieldCenter{kPlayfieldWidth / 2.0,
                                       kPlayfieldHeight / 2.0};

enum class HitSound : std::uint8_t {
  kNone = 0,
  kNormal = 1 << 0,
  kWhistle = 1 << 1,
  kFinish = 1 << 2,
  kClap = 1 << 3,
};

[[nodiscard]] constexpr HitSound operator|(HitSound a, HitSound b) noexcept {
  return static_cast<HitSound>(std::to_underlying(a) | std::to_underlying(b));
}
[[nodiscard]] constexpr HitSound operator&(HitSound a, HitSound b) noexcept {
  return static_cast<HitSound>(std::to_underlying(a) & std::to_underlying(b));
}

namespace curve {
struct Linear {};
struct Bezier {};
struct Perfect {};
struct Catmull {};
} // namespace curve

using CurveType =
    std::variant<curve::Linear, curve::Bezier, curve::Perfect, curve::Catmull>;

[[nodiscard]] constexpr CurveType curveTypeFromChar(char c) noexcept {
  switch (c) {
  case 'L':
    return curve::Linear{};
  case 'P':
    return curve::Perfect{};
  case 'C':
    return curve::Catmull{};
  default:
    return curve::Bezier{};
  }
}

namespace sampleSet {
struct None {};
struct Normal {};
struct Soft {};
struct Drum {};
} // namespace sampleSet

using SampleSet = std::variant<sampleSet::None, sampleSet::Normal,
                               sampleSet::Soft, sampleSet::Drum>;

[[nodiscard]] constexpr SampleSet sampleSetFromInt(int i) noexcept {
  switch (i) {
  case 2:
    return sampleSet::Soft{};
  case 3:
    return sampleSet::Drum{};
  case 1:
    return sampleSet::Normal{};
  default:
    return sampleSet::None{};
  }
}

struct HitSample {
  SampleSet fNormalSet = sampleSet::None{};
  SampleSet fAdditionSet = sampleSet::None{};
  int fIndex = 0;
  int fVolume = 100;
  std::string fFilename;
};

namespace judgement {
struct Miss {};
struct Meh {};
struct Good {};
struct Great {};
} // namespace judgement

using Judgement = std::variant<judgement::Miss, judgement::Meh, judgement::Good,
                               judgement::Great>;

namespace grade {
struct SS {};
struct S {};
struct A {};
struct B {};
struct C {};
struct D {};
struct F {};
} // namespace grade

using Grade = std::variant<grade::SS, grade::S, grade::A, grade::B, grade::C,
                           grade::D, grade::F>;

struct Circle {
  Vec2 fPos;
  double fTime = 0.0;
  HitSound fSound = HitSound::kNormal;
  HitSample fSample;
  int fStack = 0;
  int fCombo = 1;
};

struct Slider {
  Vec2 fPos;
  double fTime = 0.0;
  CurveType fCurveType = curve::Bezier{};
  std::vector<Vec2> fControl{};
  int fRepeat = 1;
  double fPixelLength = 0.0;
  HitSound fSound = HitSound::kNormal;
  HitSample fSample;
  std::vector<int> fEdgeHitsounds;
  std::vector<HitSample> fEdgeSamples;
  int fStack = 0;
  int fCombo = 1;
};

struct Spinner {
  double fTime = 0.0;
  double fEnd = 0.0;
  HitSound fSound = HitSound::kNormal;
  HitSample fSample;
  int fCombo = 1;
};

using HitObject = std::variant<Circle, Slider, Spinner>;

[[nodiscard]] constexpr double startTime(const HitObject &obj) noexcept {
  return std::visit([](const auto &o) { return o.fTime; }, obj);
}

[[nodiscard]] constexpr double circleScale(double cs) noexcept {
  return (1.0 - 0.7 * (cs - 5.0) / 5.0) / 2.0;
}

[[nodiscard]] constexpr Vec2 stackOffset(int stack, double cs) noexcept {
  const double shift = stack * circleScale(cs) * 6.4;
  return {-shift, -shift};
}

[[nodiscard]] constexpr Vec2 stackedPosition(const Circle &c,
                                             double cs) noexcept {
  return c.fPos + stackOffset(c.fStack, cs);
}

[[nodiscard]] constexpr Vec2 stackedPosition(const Slider &s,
                                             double cs) noexcept {
  return s.fPos + stackOffset(s.fStack, cs);
}

struct TimingPoint {
  double fTime = 0.0;
  double fBeatLength = 0.0;
  int fMeter = 4;
  SampleSet fSet = sampleSet::Normal{};
  int fSampleIndex = 0;
  int fVolume = 100;
  bool fKiai = false;

  [[nodiscard]] constexpr bool inherited() const noexcept {
    return fBeatLength < 0.0;
  }
  [[nodiscard]] constexpr double svMultiplier() const noexcept {
    return inherited() ? -100.0 / fBeatLength : 1.0;
  }
};

struct Difficulty {
  double fHp = 5.0;
  double fCs = 5.0;
  double fOd = 5.0;
  double fAr = 5.0;
  double fSliderMultiplier = 1.4;
  double fSliderTickRate = 1.0;
};

struct Metadata {
  std::string fTitle;
  std::string fTitleUnicode;
  std::string fArtist;
  std::string fArtistUnicode;
  std::string fCreator;
  std::string fVersion;
  std::string fSource;
  std::string fTags;
  std::string fAudioFilename;
  std::string fBackground;
  int fBeatmapId = 0;
  int fBeatmapSetId = 0;
};

} // namespace osu
