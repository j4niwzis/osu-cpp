import std;
import gtest;
import osu;

#include "gtest_macro.h"

namespace osu {

constexpr std::string_view kTestBeatmap = R"(osu file format v14

[General]
AudioFilename: test.mp3
AudioLeadIn: 0
PreviewTime: 500
Mode: 0

[Metadata]
Title:Test Title
Artist:Test Artist
Creator:Tester
Version:Hard
BeatmapID:123
BeatmapSetID:456

[Difficulty]
HPDrainRate:5
CircleSize:4
OverallDifficulty:5
ApproachRate:5
SliderMultiplier:1
SliderTickRate:1

[Events]
0,0,"bg.jpg"

[TimingPoints]
0,500,4,1,0,100,1,0
2000,250,4,2,0,100,1,1

[HitObjects]
64,80,500,1,0
256,192,1500,2,0,B|300:150|350:200,1,100
0,0,2100,8,0,3000
)";

TEST(Types, Vec2) {
  const Vec2 a{1.0, 2.0};
  const Vec2 b{4.0, 6.0};
  ASSERT_DOUBLE_EQ((b - a).length(), 5.0);
  const Vec2 m = a.lerp(b, 0.5);
  ASSERT_DOUBLE_EQ(m.fX, 2.5);
  ASSERT_DOUBLE_EQ(m.fY, 4.0);
}

TEST(Beatmap, Parse) {
  auto bm = parseBeatmap(kTestBeatmap);
  ASSERT_EQ(bm.fMeta.fTitle, "Test Title");
  ASSERT_EQ(bm.fMeta.fArtist, "Test Artist");
  ASSERT_EQ(bm.fMeta.fCreator, "Tester");
  ASSERT_EQ(bm.fMeta.fAudioFilename, "test.mp3");
  ASSERT_EQ(bm.fMeta.fBeatmapId, 123);
  ASSERT_DOUBLE_EQ(bm.fDiff.fCs, 4.0);
  ASSERT_EQ(bm.fTiming.size(), 2u);
  ASSERT_EQ(bm.fObjects.size(), 3u);
  ASSERT_DOUBLE_EQ(bm.firstObjectTime(), 500.0);
  ASSERT_EQ(std::holds_alternative<Spinner>(bm.fObjects[2]), true);
}

TEST(Beatmap, ParseErrors) {
  ASSERT_THROW(parseBeatmap("garbage"), BadHeaderError);
  ASSERT_THROW(
      parseBeatmap("osu file format v14\n\n[Difficulty]\nCircleSize:5\n"),
      MissingTimingError);
}

TEST(Curves, LinearPath) {
  std::vector<Vec2> ctrl{{0, 0}, {100, 100}};
  SliderPath path(curve::Linear{}, std::span{ctrl}, 0.0);
  const double len = Vec2{100, 100}.length();
  ASSERT_NEAR(path.length(), len, 0.1);
  const Vec2 mid = path.positionAt(len * 0.5);
  ASSERT_NEAR(mid.fX, 50.0, 0.1);
  ASSERT_NEAR(mid.fY, 50.0, 0.1);
}

TEST(Curves, BezierSplit) {
  std::vector<Vec2> ctrl{{0, 0}, {50, 100}, {50, 100}, {100, 0}};
  SliderPath path(curve::Bezier{}, std::span{ctrl}, 0.0);
  ASSERT_GT(path.length(), 0.0);
  ASSERT_NEAR(path.positionAt(path.length()).fY, 0.0, 1.0);
}

TEST(Curves, PerfectArc) {
  std::vector<Vec2> ctrl{{100, 100}, {150, 50}, {200, 100}};
  SliderPath path(curve::Perfect{}, std::span{ctrl}, 0.0);
  ASSERT_GT(path.length(), 0.0);
  const Vec2 end = path.positionAt(path.length());
  ASSERT_NEAR(end.fX, 200.0, 1.0);
  ASSERT_NEAR(end.fY, 100.0, 1.0);
}

TEST(Rules, WindowsAndMods) {
  ASSERT_DOUBLE_EQ(windowGreat(5.0), 50.0);
  ASSERT_DOUBLE_EQ(windowGood(5.0), 100.0);
  ASSERT_DOUBLE_EQ(windowMeh(5.0), 150.0);

  Difficulty d{5.0, 5.0, 5.0, 5.0};
  auto dt = applyMods(d, mod::kDoubleTime);
  ASSERT_DOUBLE_EQ(dt.fClockRate, 1.5);
  ASSERT_DOUBLE_EQ(dt.fOd, 7.5);

  ScoreState s;
  registerHit(s, judgement::Great{}, mod::kNone);
  registerHit(s, judgement::Good{}, mod::kNone);
  registerHit(s, judgement::Miss{}, mod::kNone);
  ASSERT_DOUBLE_EQ(s.accuracy(), (300.0 + 100.0) / (300.0 * 3.0));
  ASSERT_EQ(std::holds_alternative<grade::D>(computeGrade(s)), true);
}

TEST(Engine, AutoplayPerfect) {
  auto bm = parseBeatmap(kTestBeatmap);
  const auto result = runAutoplay(bm);
  ASSERT_EQ(result.fEvents.size(), 3u);
  ASSERT_EQ(result.fScore.fGreat, 3);
  ASSERT_EQ(result.fScore.fMiss, 0);
  ASSERT_EQ(result.fScore.fMaxCombo, 3);
  ASSERT_TRUE(std::holds_alternative<grade::SS>(computeGrade(result.fScore)));
}

TEST(Engine, NoInputAllMiss) {
  auto bm = parseBeatmap(kTestBeatmap);
  Engine engine(bm);
  engine.advance(bm.lastObjectEndTime() + 500.0);
  ASSERT_EQ(engine.score().fMiss, 3);
  ASSERT_EQ(engine.score().fScore, 0u);
}

TEST(Replay, RunAutoplay) {
  auto bm = parseBeatmap(kTestBeatmap);
  const auto result = runAutoplay(bm, mod::kDoubleTime);
  ASSERT_EQ(result.fEvents.size(), 3u);
  ASSERT_EQ(result.fScore.fGreat, 3);
}

TEST(Beatmap, ComboInfo) {
  auto bm = parseBeatmap(kTestBeatmap);
  const auto info = buildComboInfo(bm);
  ASSERT_EQ(info.fIndices.size(), 3u);
  ASSERT_EQ(info.fGroups[0], 1);
}

TEST(Beatmap, ObjectQueries) {
  auto bm = parseBeatmap(kTestBeatmap);
  ASSERT_EQ(objectPosition(bm.fObjects[0]).fX, 64.0);
  ASSERT_EQ(objectEndTime(bm.fObjects[2], bm), 3000.0);
  const auto [pos, end] = objectEnd(bm.fObjects[0], bm);
  ASSERT_EQ(pos.fX, 64.0);
  ASSERT_EQ(end, 500.0);
}

TEST(Rules, ModFormatter) {
  ASSERT_EQ(std::format("{}", mod::kNone), "");
  ASSERT_EQ(std::format("{}", mod::kDoubleTime | mod::kHardRock), "DT HR");
}

TEST(Rules, JudgementInfo) {
  const auto [label, rgb] = judgementInfo(judgement::Great{});
  ASSERT_EQ(std::string_view(label), "great");
  ASSERT_EQ(rgb[2], 255);
}

} // namespace osu
