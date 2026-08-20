export module osu.stars;

import std;
import osu.types;
import osu.rules;
import osu.curves;
import osu.beatmap;

export import :core;
export import :aim;
export import :speed;
export import :rhythm;
export import :reading;
export import :ranked;

export namespace osu {

// Which calculator to run. They disagree -- the reworked one in ppy/osu master
// against the one the servers still run, which is where a beatmap's listed
// star rating comes from -- and a client that wants to show either needs both.
enum class StarAlgorithm { kLazerMaster, kRanked };


// `aimTrace`, when given, is filled with the per-object aim terms: the
// series behind the aim value, for comparing against lazer's own.
// `untilTime`, when finite, stops the skills after the objects up to that
// moment -- but the difficulty objects are still built from the whole map, so
// each of them still has a next object to look at. That is what
// DifficultyCalculator.CalculateTimed does, and the reading evaluator cares:
// it measures the density ahead of an object, and an object with nothing
// ahead of it reads as denser than it is.
[[nodiscard]] inline StarRating
calculateStars(const Beatmap &bm, ModSet mods = mod::kNone,
               std::vector<stars::AimSkill::TracePoint> *aimTrace = nullptr,
               double untilTime = std::numeric_limits<double>::infinity(),
               StarAlgorithm algorithm = StarAlgorithm::kLazerMaster) {
  if (algorithm == StarAlgorithm::kRanked) {
    return stars::ranked::calculate(bm, mods);
  }
  using namespace stars;
  if (bm.fObjects.size() < 2)
    return {};
  double rate = clockRate(mods);
  std::vector<OsuDifficultyHitObject> objs;
  objs.reserve(bm.fObjects.size());
  for (std::size_t i = 1; i < bm.fObjects.size(); ++i)
    objs.push_back(OsuDifficultyHitObject(bm.fObjects[i], bm.fObjects[i - 1],
                                          rate, objs,
                                          static_cast<int>(objs.size()), bm));
  if (objs.empty())
    return {};
  AimSkill aimSkill(true);
  SpeedSkill spdSkill;
  ReadingSkill rdSkill;
  const double limit = std::isfinite(untilTime) ? untilTime / rate
                                                : std::numeric_limits<double>::infinity();
  for (auto &o : objs) {
    if (o.fStart > limit) {
      break;
    }
    aimSkill.process(o);
    spdSkill.process(o);
    rdSkill.process(o);
  }
  if (aimTrace != nullptr) {
    const auto trace = aimSkill.trace();
    aimTrace->assign(trace.begin(), trace.end());
  }
  double aimDiff = aimSkill.difficulty();
  double spdDiff = spdSkill.difficulty();

  double ar = std::pow(aimDiff, 0.63) * 0.02275;
  double speedRating = std::sqrt(spdDiff) * 0.0675;
  const double readingValue = rdSkill.difficulty();
  double readingRating = std::sqrt(readingValue) * 0.0675;

  double ba = 4.0 * std::pow(ar, 3.0);
  double bs = 4.0 * std::pow(speedRating, 3.0);
  double br = 4.0 * std::pow(readingRating, 3.0);
  double bp = diffutil::norm(1.1, ba, bs, br);

  double total = std::cbrt(bp * 1.12);

  return {ar,          speedRating,  total,
          readingRating, readingValue, rdSkill.reducedNoteCount()};
}

} // namespace osu