export module osu.autopilot;

import std;
import osu.types;
import osu.curves;
import osu.beatmap;
import osu.rules;
import osu.engine;

export namespace osu {

[[nodiscard]] inline std::vector<InputEvent>
buildAutoplay(const Beatmap &map, ModSet mods = mod::kNone) {
  const EffectiveDifficulty diff = applyMods(map.fDiff, mods);
  std::vector<InputEvent> events;
  events.reserve(map.fObjects.size() * 4 + 64);

  std::vector<SliderPath> paths;
  paths.reserve(map.fObjects.size());
  for (const auto &obj : map.fObjects) {
    if (const Slider *s = std::get_if<Slider>(&obj)) {
      paths.push_back(SliderPath::from(*s));
    } else {
      paths.emplace_back();
    }
  }

  constexpr double kMoveStep = 6.0;

  Vec2 prev = kPlayfieldCenter;
  double lastEnd = 0.0;
  const auto append = [&](InputAction action, double time, Vec2 pos) {
    events.push_back({time, pos, action});
  };

  const auto moveTo = [&](Vec2 from, Vec2 to, double startTime,
                          double duration) {
    if (duration <= kMoveStep) {
      append(InputAction::kMove, startTime + duration, to);
      return;
    }
    for (double t = kMoveStep; t < duration; t += kMoveStep) {
      const double frac = t / duration;
      append(InputAction::kMove, startTime + t, from + (to - from) * frac);
    }
    append(InputAction::kMove, startTime + duration, to);
  };

  for (std::size_t i = 0; i < map.fObjects.size(); ++i) {
    const auto &obj = map.fObjects[i];
    std::visit(
        Overloaded{
            [&](const Circle &o) {
              const double pressTime = std::max(o.fTime, lastEnd);
              if (pressTime > lastEnd) {
                moveTo(prev, o.fPos, lastEnd, pressTime - lastEnd);
              }
              append(InputAction::kPress, pressTime, o.fPos);
              append(InputAction::kRelease, pressTime + 15.0, o.fPos);
              lastEnd = pressTime + 15.0;
              prev = o.fPos;
            },
            [&](const Slider &o) {
              const double span = map.sliderSpanDuration(o);
              const double sliderEnd = o.fTime + span * o.fRepeat;
              const double pressTime = std::max(o.fTime, lastEnd);
              if (pressTime > lastEnd) {
                moveTo(prev, o.fPos, lastEnd, pressTime - lastEnd);
              }
              append(InputAction::kPress, pressTime, o.fPos);
              for (double t = kMoveStep; t < sliderEnd - o.fTime;
                   t += kMoveStep) {
                const int spanIdx = static_cast<int>(t / span);
                const double dInSpan = std::fmod(t, span);
                const double total = o.fPixelLength;
                const double dist =
                    (spanIdx & 1) == 0
                        ? std::min(dInSpan / span, 1.0) * total
                        : (1.0 - std::min(dInSpan / span, 1.0)) * total;
                append(InputAction::kMove, o.fTime + t,
                       sliderBallPosition(paths[i], t, span, o.fPixelLength));
              }
              const double releaseTime = std::max(sliderEnd + 15.0, pressTime);
              append(InputAction::kRelease, releaseTime,
                     sliderEnd > o.fTime ? o.fPos : kPlayfieldCenter);
              lastEnd = releaseTime;
              prev = o.fPos;
            },
            [&](const Spinner &o) {
              const double pressTime = std::max(o.fTime, lastEnd);
              if (pressTime > lastEnd) {
                moveTo(prev, kPlayfieldCenter, lastEnd, pressTime - lastEnd);
              }
              append(InputAction::kPress, pressTime, kPlayfieldCenter);
              constexpr double kRpm = 480.0;
              const double omega =
                  kRpm * 2.0 * std::numbers::pi / 60.0 / 1000.0;
              for (double t = kMoveStep; t < o.fEnd - o.fTime; t += kMoveStep) {
                const double ang = t * omega;
                const Vec2 p{kPlayfieldCenter.fX + 80.0 * std::cos(ang),
                             kPlayfieldCenter.fY + 80.0 * std::sin(ang)};
                append(InputAction::kMove, o.fTime + t, p);
              }
              const double releaseTime = std::max(o.fEnd + 15.0, pressTime);
              append(InputAction::kRelease, releaseTime, kPlayfieldCenter);
              lastEnd = releaseTime;
              prev = kPlayfieldCenter;
            },
        },
        obj);
  }

  std::ranges::sort(events, {}, &InputEvent::fTime);
  return events;
}

} // namespace osu
