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

export namespace osu {

[[nodiscard]] inline StarRating calculateStars(const Beatmap &bm,
                                               ModSet mods = mod::kNone) {
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
  for (auto &o : objs) {
    aimSkill.process(o);
    spdSkill.process(o);
    rdSkill.process(o);
  }
  double aimDiff = aimSkill.difficulty();
  double spdDiff = spdSkill.difficulty();

  double ar = std::pow(aimDiff, 0.63) * 0.02275;
  double speedRating = std::sqrt(spdDiff) * 0.0675;
  double readingRating = std::sqrt(rdSkill.difficulty()) * 0.0675;

  double ba = 4.0 * std::pow(ar, 3.0);
  double bs = 4.0 * std::pow(speedRating, 3.0);
  double br = 4.0 * std::pow(readingRating, 3.0);
  double bp = diffutil::norm(1.1, ba, bs, br);

  double total = std::cbrt(bp * 1.12);

  return {ar, speedRating, total};
}

} // namespace osu