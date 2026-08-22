export module client.playresult;

import std;
import osu;

export namespace client {

struct PlayResult {
  osu::ScoreState fScore{};
  double fMean = 0.0;
  double fUr = 0.0;
  std::string fGrade = "F";
  double fPp = 0.0;
};

[[nodiscard]] double pricePlay(const osu::StarRating &attributes,
                               const osu::ScoreState &score) {
  osu::ScoreInput input;
  input.fGreat = score.fGreat;
  input.fOk = score.fGood;
  input.fMeh = score.fMeh;
  input.fMiss = score.fMiss;
  input.fMaxCombo = score.fMaxCombo;
  input.fSliderTailHits = score.fTailHit;
  input.fLargeTickHits = score.fLargeTickHit;
  return osu::performanceRanked(attributes, input).fTotal;
}

[[nodiscard]] PlayResult captureResult(const osu::Engine &engine,
                                       const osu::StarRating &attributes) {
  PlayResult result;
  result.fScore = engine.score();
  double sum = 0.0;
  double sumSq = 0.0;
  std::size_t count = 0;
  for (const double delta : engine.tapDeltas()) {
    sum += delta;
    sumSq += delta * delta;
    ++count;
  }
  if (count > 0) {
    result.fMean = sum / static_cast<double>(count);
    result.fUr = 10.0 * std::sqrt(std::max(
                              0.0, sumSq / static_cast<double>(count) -
                                       result.fMean * result.fMean));
  }
  result.fGrade = osu::gradeString(osu::computeGrade(result.fScore));
  result.fPp = pricePlay(attributes, result.fScore);
  return result;
}

void printResult(const osu::Engine &engine, std::size_t droppedInput) {
  std::println("{}", engine.score());
  double sum = 0.0;
  double sumSq = 0.0;
  std::size_t count = 0;
  for (const double delta : engine.tapDeltas()) {
    sum += delta;
    sumSq += delta * delta;
    ++count;
  }
  if (count > 0) {
    const double mean = sum / static_cast<double>(count);
    const double variance =
        std::max(0.0, sumSq / static_cast<double>(count) - mean * mean);
    std::println("hit error: {:+.1f} ms avg, UR {:.0f}", mean,
                 10.0 * std::sqrt(variance));
  }
  if (droppedInput > 0) {
    std::println("warning: {} input events dropped", droppedInput);
  }
}

[[nodiscard]] std::string beatmapMd5(const osu::BeatmapSet &set,
                                     std::string_view filename) {
  for (const auto &info : set.fBeatmaps) {
    if (info.fFilename == filename) {
      return info.fMd5;
    }
  }
  return {};
}

[[nodiscard]] std::optional<std::filesystem::path>
saveReplay(const std::vector<osu::InputEvent> &events,
           const osu::Beatmap &map, const osu::Engine &engine,
           const std::string &mapMd5, osu::ModSet mods,
           const std::filesystem::path &replayDir) {
  if (events.empty()) {
    return std::nullopt;
  }
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::ostringstream name;
  name << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");

  const auto &scoreState = engine.score();
  osu::ReplayScore score;
  score.f300 = static_cast<std::uint16_t>(scoreState.fGreat);
  score.f100 = static_cast<std::uint16_t>(scoreState.fGood);
  score.f50 = static_cast<std::uint16_t>(scoreState.fMeh);
  score.fMiss = static_cast<std::uint16_t>(scoreState.fMiss);
  score.fTotalScore = static_cast<std::int32_t>(scoreState.fScore);
  score.fMaxCombo = static_cast<std::uint16_t>(scoreState.fMaxCombo);
  score.fPerfect = scoreState.fMiss == 0 && scoreState.fGood == 0 &&
                   scoreState.fMeh == 0;

  const auto maximum = engine.maximumStatistics();
  osu::ReplayStatistics stats;
  stats.fPresent = true;
  stats.fGreat = scoreState.fGreat;
  stats.fOk = scoreState.fGood;
  stats.fMeh = scoreState.fMeh;
  stats.fMiss = scoreState.fMiss;
  stats.fLargeTickHit = scoreState.fLargeTickHit;
  stats.fLargeTickMiss = scoreState.fLargeTickMiss;
  stats.fSliderTailHit = scoreState.fTailHit;
  stats.fSmallBonus = scoreState.fSmallBonus;
  stats.fLargeBonus = scoreState.fLargeBonus;
  stats.fMaxGreat = maximum.fGreat;
  stats.fMaxLargeTick = maximum.fLargeTick;
  stats.fMaxSliderTail = maximum.fSliderTail;
  stats.fMaxSmallBonus = maximum.fSmallBonus;
  stats.fMaxLargeBonus = maximum.fLargeBonus;
  stats.fRank = osu::gradeString(osu::computeGrade(scoreState));
  stats.fTotalScore = static_cast<std::int64_t>(scoreState.fScore);

  const auto bytes =
      osu::encodeReplay(events, mapMd5, "Player", mods, score, stats);
  std::error_code error;
  std::filesystem::create_directories(replayDir, error);
  const auto path = replayDir / (map.fMeta.fVersion + "_" + name.str() + ".osr");
  std::ofstream out(path, std::ios::binary);
  for (const std::uint8_t byte : bytes) {
    out.put(static_cast<char>(byte));
  }
  if (!out) {
    return std::nullopt;
  }
  return path;
}

} // namespace client
