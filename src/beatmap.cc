export module osu.beatmap;

import std;
import osu.types;
import osu.rules;
import osu.curves;

export namespace osu {

class ParseError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class BadHeaderError : public ParseError {
public:
  using ParseError::ParseError;
};
class MissingTimingError : public ParseError {
public:
  using ParseError::ParseError;
};
class UnsupportedModeError : public ParseError {
public:
  using ParseError::ParseError;
};

class Beatmap {
public:
  Metadata fMeta;
  Difficulty fDiff;
  std::vector<TimingPoint> fTiming;
  std::vector<HitObject> fObjects;
  std::vector<SliderPath> fSliderPaths;
  std::vector<std::array<std::uint8_t, 3>> fComboColors;
  int fFormatVersion = 14;
  int fMode = 0;
  double fStackLeniency = 0.7;
  double fAudioLeadIn = 0.0;

  [[nodiscard]] double baseSliderVelocity() const noexcept {
    return fDiff.fSliderMultiplier * 100.0;
  }

  [[nodiscard]] const TimingPoint *activeTiming(double t) const noexcept {
    const TimingPoint *best = nullptr;
    for (const auto &tp : fTiming) {
      if (!tp.inherited() && tp.fTime <= t &&
          (!best || tp.fTime > best->fTime)) {
        best = &tp;
      }
    }
    return best;
  }

  [[nodiscard]] double sliderVelocityAt(double t) const noexcept {
    double sv = 1.0;
    for (const auto &tp : fTiming) {
      if (tp.inherited() && tp.fTime <= t) {
        sv = tp.svMultiplier();
      }
    }
    const TimingPoint *active = this->activeTiming(t);
    const double beatLength = active ? active->fBeatLength : 500.0;
    return this->baseSliderVelocity() * sv / beatLength;
  }

  [[nodiscard]] double sliderSpanDuration(const Slider &s) const noexcept {
    const double vel = this->sliderVelocityAt(s.fTime);
    return vel > 0.0 ? s.fPixelLength / vel : 0.0;
  }

  [[nodiscard]] double sliderTickDistance(const Slider &s) const noexcept {
    const double vel = this->sliderVelocityAt(s.fTime);
    const TimingPoint *active = this->activeTiming(s.fTime);
    const double beatLength = active ? active->fBeatLength : 500.0;
    return vel * beatLength / fDiff.fSliderTickRate;
  }

  [[nodiscard]] double firstObjectTime() const noexcept {
    return fObjects.empty() ? 0.0 : startTime(fObjects.front());
  }

  [[nodiscard]] double lastObjectEndTime() const noexcept {
    if (fObjects.empty()) {
      return 0.0;
    }
    return std::visit(Overloaded{
                          [this](const Slider &o) -> double {
                            return o.fTime +
                                   this->sliderSpanDuration(o) * o.fRepeat;
                          },
                          [](const Spinner &o) -> double { return o.fEnd; },
                          [](const Circle &o) -> double { return o.fTime; },
                      },
                      fObjects.back());
  }
};

struct ComboInfo {
  std::vector<std::size_t> fIndices;
  std::vector<int> fGroups;
};

[[nodiscard]] inline ComboInfo buildComboInfo(const Beatmap &bm) {
  ComboInfo info;
  info.fIndices.resize(bm.fObjects.size());
  info.fGroups.resize(bm.fObjects.size());
  std::size_t group = 0;
  int prevCombo = 0;
  bool forceNextNewCombo = false;
  for (std::size_t i = 0; i < bm.fObjects.size(); ++i) {
    const bool isSpinner = std::holds_alternative<Spinner>(bm.fObjects[i]);
    const int combo = std::visit([](const auto &o) -> int { return o.fCombo; },
                                 bm.fObjects[i]);
    bool newCombo = forceNextNewCombo || i == 0 || combo <= prevCombo;
    if (isSpinner && i != 0) {
      newCombo = false;
    }
    if (newCombo) {
      ++group;
    }
    info.fIndices[i] = group % 4;
    info.fGroups[i] = static_cast<int>(group);
    forceNextNewCombo = isSpinner;
    prevCombo = combo;
  }
  return info;
}

[[nodiscard]] inline Vec2 objectPosition(const HitObject &obj) noexcept {
  return std::visit(
      Overloaded{
          [](const Circle &o) -> Vec2 { return o.fPos; },
          [](const Slider &o) -> Vec2 { return o.fPos; },
          [](const Spinner &) -> Vec2 { return kPlayfieldCenter; },
      },
      obj);
}

[[nodiscard]] inline double objectEndTime(const HitObject &obj,
                                          const Beatmap &bm) noexcept {
  return std::visit(Overloaded{
                        [&bm](const Slider &o) -> double {
                          return o.fTime + bm.sliderSpanDuration(o) * o.fRepeat;
                        },
                        [](const Spinner &o) -> double { return o.fEnd; },
                        [](const Circle &o) -> double { return o.fTime; },
                    },
                    obj);
}

[[nodiscard]] inline std::pair<Vec2, double>
objectEnd(const HitObject &obj, const Beatmap &bm) noexcept {
  return {objectPosition(obj), objectEndTime(obj, bm)};
}

namespace detail {

inline std::string_view trim(std::string_view s) noexcept {
  const auto isSpace = [](char c) {
    return c == ' ' || c == '\t' || c == '\r';
  };
  while (!s.empty() && isSpace(s.front()))
    s.remove_prefix(1);
  while (!s.empty() && isSpace(s.back()))
    s.remove_suffix(1);
  return s;
}

inline double toDouble(std::string_view s, double fallback = 0.0) noexcept {
  s = trim(s);
  double value = fallback;
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec == std::errc::invalid_argument) {
    return fallback;
  }
  return value;
}

inline int toInt(std::string_view s, int fallback = 0) noexcept {
  return static_cast<int>(toDouble(s, fallback));
}

inline std::vector<std::string_view> split(std::string_view s, char delim) {
  std::vector<std::string_view> out;
  out.reserve(8);
  std::size_t pos = 0;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == delim) {
      out.push_back(s.substr(pos, i - pos));
      pos = i + 1;
    }
  }
  return out;
}

inline std::pair<std::string_view, std::string_view>
splitKeyValue(std::string_view line) {
  const auto colon = line.find(':');
  if (colon == std::string_view::npos) {
    return {trim(line), {}};
  }
  return {trim(line.substr(0, colon)), trim(line.substr(colon + 1))};
}

} // namespace detail

inline Vec2 nonStackedEndPosition(const HitObject &obj) {
  return std::visit(
      Overloaded{
          [](const Circle &o) -> Vec2 { return o.fPos; },
          [](const Spinner &) -> Vec2 { return kPlayfieldCenter; },
          [](const Slider &s) -> Vec2 {
            SliderPath path(s.fCurveType, s.fControl, s.fPixelLength);
            return path.positionAt(path.length());
          },
      },
      obj);
}

inline int &stackHeight(HitObject &obj) {
  return std::visit(Overloaded{
                        [](Circle &c) -> int & { return c.fStack; },
                        [](Slider &s) -> int & { return s.fStack; },
                        [](Spinner &) -> int & {
                          static int dummy;
                          return dummy;
                        },
                    },
                    obj);
}

inline void resetStack(HitObject &obj) {
  std::visit(Overloaded{
                 [](Circle &c) { c.fStack = 0; },
                 [](Slider &s) { s.fStack = 0; },
                 [](Spinner &) {},
             },
             obj);
}

inline void applyStacking(Beatmap &bm) {
  if (bm.fObjects.empty())
    return;
  const int count = static_cast<int>(bm.fObjects.size());
  constexpr double kStackDistance = 3.0;

  if (bm.fFormatVersion >= 6) {
    for (auto &obj : bm.fObjects)
      resetStack(obj);

    int extendedStartIndex = 0;
    for (int i = count - 1; i > 0; --i) {
      HitObject &objI = bm.fObjects[static_cast<std::size_t>(i)];
      if (std::holds_alternative<Spinner>(objI))
        continue;
      int &stackI = stackHeight(objI);
      if (stackI != 0)
        continue;

      double stackThreshold =
          std::floor(osu::preemptTime(bm.fDiff.fAr)) * bm.fStackLeniency;

      if (std::holds_alternative<Circle>(objI)) {
        int iBase = i;
        for (int n = i - 1; n >= 0; --n) {
          HitObject &objN = bm.fObjects[static_cast<std::size_t>(n)];
          if (std::holds_alternative<Spinner>(objN))
            continue;
          if (static_cast<int>(startTime(objI)) -
                  static_cast<int>(objectEndTime(objN, bm)) >
              stackThreshold)
            break;
          if (n < extendedStartIndex) {
            resetStack(objN);
            extendedStartIndex = n;
          }
          if (std::holds_alternative<Slider>(objN)) {
            Vec2 endN = nonStackedEndPosition(objN);
            if (objectPosition(bm.fObjects[static_cast<std::size_t>(iBase)])
                    .distanceTo(endN) < kStackDistance) {
              int offset = stackI - stackHeight(objN) + 1;
              for (int j = n + 1; j <= i; ++j) {
                HitObject &objJ = bm.fObjects[static_cast<std::size_t>(j)];
                if (objectPosition(objJ).distanceTo(endN) < kStackDistance) {
                  std::visit(Overloaded{
                                 [offset](Circle &c) { c.fStack -= offset; },
                                 [offset](Slider &s) { s.fStack -= offset; },
                                 [](Spinner &) {},
                             },
                             objJ);
                }
              }
              break;
            }
          }
          if (objectPosition(bm.fObjects[static_cast<std::size_t>(iBase)])
                  .distanceTo(objectPosition(objN)) < kStackDistance) {
            stackHeight(objN) = stackI + 1;
            iBase = n;
          }
        }
      } else { // Slider
        int iBase = i;
        for (int n = i - 1; n >= 0; --n) {
          HitObject &objN = bm.fObjects[static_cast<std::size_t>(n)];
          if (std::holds_alternative<Spinner>(objN))
            continue;
          if (startTime(objI) - startTime(objN) > stackThreshold)
            break;
          if (objectPosition(bm.fObjects[static_cast<std::size_t>(iBase)])
                  .distanceTo(nonStackedEndPosition(objN)) < kStackDistance) {
            stackHeight(objN) = stackI + 1;
            iBase = n;
          }
        }
      }
    }
  } else {
    for (int i = 0; i < count; ++i) {
      HitObject &objI = bm.fObjects[static_cast<std::size_t>(i)];
      if (std::holds_alternative<Spinner>(objI))
        continue;
      int &stackI = stackHeight(objI);
      if (stackI != 0 && !std::holds_alternative<Slider>(objI))
        continue;

      double startT = objectEndTime(objI, bm);
      int sliderStack = 0;

      for (int j = i + 1; j < count; ++j) {
        HitObject &objJ = bm.fObjects[static_cast<std::size_t>(j)];
        double threshold =
            std::floor(osu::preemptTime(bm.fDiff.fAr)) * bm.fStackLeniency;
        if (startTime(objJ) - threshold > startT)
          break;

        Vec2 pos2 = std::holds_alternative<Slider>(objI)
                        ? nonStackedEndPosition(objI)
                        : objectPosition(objI);

        if (objectPosition(objJ).distanceTo(objectPosition(objI)) <
            kStackDistance) {
          ++stackI;
          startT = startTime(objJ);
        } else if (objectPosition(objJ).distanceTo(pos2) < kStackDistance) {
          ++sliderStack;
          std::visit(Overloaded{
                         [sliderStack](Circle &c) { c.fStack -= sliderStack; },
                         [sliderStack](Slider &s) { s.fStack -= sliderStack; },
                         [](Spinner &) {},
                     },
                     objJ);
          startT = startTime(objJ);
        }
      }
    }
  }
}

inline HitSample parseHitSample(std::string_view text) {
  HitSample sample;
  const auto parts = detail::split(text, ':');
  if (parts.size() > 0)
    sample.fNormalSet = sampleSetFromInt(detail::toInt(parts[0]));
  if (parts.size() > 1)
    sample.fAdditionSet = sampleSetFromInt(detail::toInt(parts[1]));
  if (parts.size() > 2)
    sample.fIndex = detail::toInt(parts[2]);
  if (parts.size() > 3)
    sample.fVolume = detail::toInt(parts[3]);
  if (parts.size() > 4)
    sample.fFilename = std::string(parts[4]);
  return sample;
}

inline std::array<std::uint8_t, 3> parseColor(std::string_view text) {
  const auto parts = detail::split(text, ',');
  std::array<std::uint8_t, 3> color{255, 255, 255};
  for (std::size_t i = 0; i < 3 && i < parts.size(); ++i) {
    color[i] =
        static_cast<std::uint8_t>(std::clamp(detail::toInt(parts[i]), 0, 255));
  }
  return color;
}

inline Beatmap parseBeatmap(std::string_view text) {
  Beatmap bm;

  auto cursor = text;
  const auto nextLine = [&cursor]() -> std::string_view {
    const auto nl = cursor.find('\n');
    if (nl == std::string_view::npos) {
      return std::exchange(cursor, {});
    }
    auto line = cursor.substr(0, nl);
    cursor.remove_prefix(nl + 1);
    return line;
  };

  const std::string_view first = detail::trim(nextLine());
  constexpr std::string_view kHeader = "osu file format v";
  if (!first.starts_with(kHeader)) {
    throw BadHeaderError{"missing format header"};
  }
  bm.fFormatVersion = detail::toInt(first.substr(kHeader.size()), 14);

  enum class Section {
    kNone,
    kGeneral,
    kMetadata,
    kDifficulty,
    kEvents,
    kTiming,
    kColours,
    kObjects
  };
  Section current = Section::kNone;
  int comboNumber = 1;

  for (std::string_view raw = nextLine(); !raw.empty() || !cursor.empty();
       raw = nextLine()) {
    const std::string_view line = detail::trim(raw);
    if (line.empty() || line.starts_with("//")) {
      if (raw.empty() && cursor.empty()) {
        break;
      }
      continue;
    }
    if (line.starts_with('[') && line.ends_with(']')) {
      const std::string_view name = line.substr(1, line.size() - 2);
      if (name == "General")
        current = Section::kGeneral;
      else if (name == "Metadata")
        current = Section::kMetadata;
      else if (name == "Difficulty")
        current = Section::kDifficulty;
      else if (name == "Events")
        current = Section::kEvents;
      else if (name == "TimingPoints")
        current = Section::kTiming;
      else if (name == "Colours")
        current = Section::kColours;
      else if (name == "HitObjects")
        current = Section::kObjects;
      else
        current = Section::kNone;
      continue;
    }

    switch (current) {
    case Section::kGeneral: {
      const auto [key, value] = detail::splitKeyValue(line);
      if (key == "AudioFilename")
        bm.fMeta.fAudioFilename = value;
      else if (key == "AudioLeadIn")
        bm.fAudioLeadIn = detail::toDouble(value);
      else if (key == "Mode")
        bm.fMode = detail::toInt(value);
      else if (key == "StackLeniency")
        bm.fStackLeniency = detail::toDouble(value, 0.7);
      break;
    }
    case Section::kMetadata: {
      const auto [key, value] = detail::splitKeyValue(line);
      if (key == "Title")
        bm.fMeta.fTitle = value;
      else if (key == "TitleUnicode")
        bm.fMeta.fTitleUnicode = value;
      else if (key == "Artist")
        bm.fMeta.fArtist = value;
      else if (key == "ArtistUnicode")
        bm.fMeta.fArtistUnicode = value;
      else if (key == "Creator")
        bm.fMeta.fCreator = value;
      else if (key == "Version")
        bm.fMeta.fVersion = value;
      else if (key == "Source")
        bm.fMeta.fSource = value;
      else if (key == "Tags")
        bm.fMeta.fTags = value;
      else if (key == "BeatmapID")
        bm.fMeta.fBeatmapId = detail::toInt(value);
      else if (key == "BeatmapSetID")
        bm.fMeta.fBeatmapSetId = detail::toInt(value);
      break;
    }
    case Section::kDifficulty: {
      const auto [key, value] = detail::splitKeyValue(line);
      if (key == "HPDrainRate")
        bm.fDiff.fHp = detail::toDouble(value);
      else if (key == "CircleSize")
        bm.fDiff.fCs = detail::toDouble(value);
      else if (key == "OverallDifficulty")
        bm.fDiff.fOd = detail::toDouble(value);
      else if (key == "ApproachRate")
        bm.fDiff.fAr = detail::toDouble(value);
      else if (key == "SliderMultiplier")
        bm.fDiff.fSliderMultiplier = detail::toDouble(value, 1.4);
      else if (key == "SliderTickRate")
        bm.fDiff.fSliderTickRate = detail::toDouble(value, 1.0);
      break;
    }
    case Section::kEvents: {
      if (bm.fMeta.fBackground.empty()) {
        const auto fields = detail::split(line, ',');
        if (fields.size() >= 3 && detail::trim(fields[0]) == "0") {
          std::string_view name = detail::trim(fields[2]);
          if (name.starts_with('"') && name.ends_with('"') &&
              name.size() >= 2) {
            name = name.substr(1, name.size() - 2);
          }
          bm.fMeta.fBackground = name;
        }
      }
      break;
    }
    case Section::kColours: {
      const auto [key, value] = detail::splitKeyValue(line);
      if (key == "SliderTrackOverride" || key == "SliderBorder") {
        break;
      }
      bm.fComboColors.push_back(parseColor(value));
      break;
    }
    case Section::kTiming: {
      const auto fields = detail::split(line, ',');
      if (fields.size() < 2) {
        break;
      }
      TimingPoint tp;
      tp.fTime = detail::toDouble(fields[0]);
      tp.fBeatLength = detail::toDouble(fields[1]);
      if (fields.size() > 2)
        tp.fMeter = detail::toInt(fields[2], 4);
      if (fields.size() > 3)
        tp.fSet = sampleSetFromInt(detail::toInt(fields[3], 1));
      if (fields.size() > 4)
        tp.fSampleIndex = detail::toInt(fields[4]);
      if (fields.size() > 5)
        tp.fVolume = detail::toInt(fields[5], 100);
      if (fields.size() > 6 && detail::toInt(fields[6], 1) == 0 &&
          tp.fBeatLength > 0) {
        tp.fBeatLength = -100.0;
      }
      if (fields.size() > 7)
        tp.fKiai = detail::toInt(fields[7]) != 0;
      bm.fTiming.push_back(tp);
      break;
    }
    case Section::kObjects: {
      const auto fields = detail::split(line, ',');
      if (fields.size() < 5) {
        break;
      }
      const Vec2 pos{detail::toDouble(fields[0]), detail::toDouble(fields[1])};
      const double time = detail::toDouble(fields[2]);
      const int type = detail::toInt(fields[3]);
      const auto sound = static_cast<HitSound>(detail::toInt(fields[4]) & 0xF);

      if ((type & 4) != 0) {
        comboNumber = 1;
      }

      if (type & 1) { // circle
        Circle c;
        c.fPos = pos;
        c.fTime = time;
        c.fSound = sound;
        c.fCombo = comboNumber;
        if (fields.size() > 5)
          c.fSample = parseHitSample(fields[5]);
        bm.fObjects.emplace_back(std::move(c));
      } else if (type & 2) { // slider
        Slider s;
        s.fPos = pos;
        s.fTime = time;
        s.fSound = sound;
        s.fCombo = comboNumber;
        if (fields.size() > 5) {
          const auto curve = detail::split(fields[5], '|');
          if (!curve.empty()) {
            s.fCurveType =
                curveTypeFromChar(curve[0].empty() ? 'B' : curve[0][0]);
            s.fControl.push_back(pos);
            for (std::size_t i = 1; i < curve.size(); ++i) {
              const auto xy = detail::split(curve[i], ':');
              if (xy.size() == 2) {
                s.fControl.push_back(
                    {detail::toDouble(xy[0]), detail::toDouble(xy[1])});
              }
            }
          }
        }
        if (fields.size() > 6)
          s.fRepeat = std::max(1, detail::toInt(fields[6], 1));
        if (fields.size() > 7)
          s.fPixelLength = std::max(0.0, detail::toDouble(fields[7]));
        if (s.fPixelLength <= 0.0) {
          // ConvertHitObjectParser: a length of zero means "no expected
          // distance", and the slider is as long as its control points make
          // it. Taking the zero literally gives a slider of no duration, no
          // ticks and no tail -- which is what this did, and it cost the
          // combo, the judgements and the star rating on any map that has
          // one.
          s.fPixelLength = SliderPath(s.fCurveType, s.fControl, 0.0).length();
        }
        if (fields.size() > 8) {
          const auto edges = detail::split(fields[8], '|');
          for (const auto &e : edges)
            s.fEdgeHitsounds.push_back(detail::toInt(e));
        }
        if (fields.size() > 9) {
          const auto sets = detail::split(fields[9], '|');
          for (const auto &set : sets) {
            HitSample edge;
            const auto parts = detail::split(set, ':');
            if (parts.size() > 0)
              edge.fNormalSet = sampleSetFromInt(detail::toInt(parts[0]));
            if (parts.size() > 1)
              edge.fAdditionSet = sampleSetFromInt(detail::toInt(parts[1]));
            s.fEdgeSamples.push_back(std::move(edge));
          }
        }
        if (fields.size() > 10)
          s.fSample = parseHitSample(fields[10]);
        bm.fObjects.emplace_back(std::move(s));
      } else if (type & 8) { // spinner
        Spinner sp;
        sp.fTime = time;
        sp.fSound = sound;
        sp.fCombo = comboNumber;
        if (fields.size() > 5)
          sp.fEnd = detail::toDouble(fields[5], time);
        if (fields.size() > 6)
          sp.fSample = parseHitSample(fields[6]);
        bm.fObjects.emplace_back(std::move(sp));
      }
      comboNumber++;
      break;
    }
    case Section::kNone:
      break;
    }
  }

  if (bm.fComboColors.empty()) {
    bm.fComboColors = {
        {96, 159, 159},
        {192, 192, 192},
        {128, 255, 255},
        {139, 191, 222},
    };
  }

  if (bm.fMode != 0) {
    throw UnsupportedModeError{"only osu!standard is supported"};
  }
  if (bm.fTiming.empty()) {
    throw MissingTimingError{"no timing points"};
  }
  std::ranges::sort(bm.fTiming, {}, &TimingPoint::fTime);
  std::ranges::sort(bm.fObjects, {}, &startTime);
  applyStacking(bm);

  bm.fSliderPaths.resize(bm.fObjects.size());
  for (std::size_t i = 0; i < bm.fObjects.size(); ++i) {
    if (auto *s = std::get_if<Slider>(&bm.fObjects[i])) {
      bm.fSliderPaths[i] = SliderPath::from(*s);
    }
  }

  return bm;
}

struct BeatmapInfo {
  std::string fFilename;
  std::string fMd5; // of the .osu file, which is how a replay names it
  Metadata fMeta;
  Difficulty fDiff;
  double fLengthMs = 0.0;
  int fObjectCount = 0;
  double fStars = 0.0;
};

class BeatmapSet {
public:
  std::vector<BeatmapInfo> fBeatmaps;
  std::unordered_map<std::string, std::vector<std::uint8_t>> fFiles;

  [[nodiscard]] std::span<const std::uint8_t>
  findFile(std::string_view name) const {
    const std::string lower = toLower(name);
    for (const auto &[key, value] : fFiles) {
      if (toLower(key) == lower) {
        return std::span{value};
      }
    }
    return {};
  }

  [[nodiscard]] bool hasFile(std::string_view name) const {
    return !this->findFile(name).empty();
  }
};

[[nodiscard]] inline BeatmapInfo buildBeatmapInfo(std::string_view filename,
                                                  const Beatmap &bm) {
  BeatmapInfo info;
  info.fFilename = filename;
  info.fMeta = bm.fMeta;
  info.fDiff = bm.fDiff;
  info.fObjectCount = static_cast<int>(bm.fObjects.size());
  info.fLengthMs = std::max(0.0, bm.lastObjectEndTime() - bm.firstObjectTime());
  return info;
}

[[nodiscard]] inline Beatmap loadBeatmap(std::string_view text) {
  return parseBeatmap(text);
}

} // namespace osu
