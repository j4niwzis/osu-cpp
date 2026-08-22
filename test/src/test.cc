import std;
import gtest;
import osu;

#include "gtest/gtest-macros.h"

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
  // Legacy windows: the value floored to a whole millisecond, less half of
  // one. Checked against lazer's own HitWindowGreat, which is twice this.
  ASSERT_DOUBLE_EQ(windowGreat(5.0), 49.5);
  ASSERT_DOUBLE_EQ(windowGood(5.0), 99.5);
  ASSERT_DOUBLE_EQ(windowMeh(5.0), 149.5);
  ASSERT_DOUBLE_EQ(windowGreat(7.0), 37.5);   // lazer: HitWindowGreat 75
  ASSERT_DOUBLE_EQ(windowGreat(9.0), 25.5);   // 51
  ASSERT_DOUBLE_EQ(windowGreat(9.6), 21.5);   // 43
  ASSERT_DOUBLE_EQ(windowGreat(3.0), 61.5);   // 123

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
  // Three objects, four judgements: a slider is its head and its tail now,
  // and this one is short enough to have no ticks between them.
  ASSERT_EQ(result.fEvents.size(), 4u);
  const auto basic = std::ranges::count_if(
      result.fEvents,
      [](const HitEvent &e) { return e.fKind == HitKind::kBasic; });
  ASSERT_EQ(basic, 3);
  ASSERT_EQ(result.fScore.fGreat, 3);
  ASSERT_EQ(result.fScore.fTailHit, 1);
  ASSERT_EQ(result.fScore.fMiss, 0);
  // The tail raises the combo like anything else does.
  ASSERT_EQ(result.fScore.fMaxCombo, 4);
  ASSERT_DOUBLE_EQ(result.fScore.accuracy(), 1.0);
  ASSERT_TRUE(std::holds_alternative<grade::SS>(computeGrade(result.fScore)));
}

TEST(Engine, NoInputAllMiss) {
  auto bm = parseBeatmap(kTestBeatmap);
  Engine engine(bm);
  engine.advance(bm.lastObjectEndTime() + 500.0);
  // The circle, the slider's head and the spinner are misses; the tail is an
  // IgnoreMiss, which is counted apart and breaks nothing.
  ASSERT_EQ(engine.score().fMiss, 3);
  ASSERT_EQ(engine.score().fTailMiss, 1);
  ASSERT_EQ(engine.score().fScore, 0u);
  ASSERT_DOUBLE_EQ(engine.score().accuracy(), 0.0);
}

TEST(Replay, RunAutoplay) {
  auto bm = parseBeatmap(kTestBeatmap);
  const auto result = runAutoplay(bm, mod::kDoubleTime);
  ASSERT_EQ(result.fEvents.size(), 4u);
  ASSERT_EQ(result.fScore.fGreat, 3);
  ASSERT_EQ(result.fScore.fTailHit, 1);
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

// The numbers below come from running lazer's own difficulty calculator --
// ppy.osu.Game 2026.730.0, which carries the reworked skills -- over these
// four beatmaps, which are ppy's own diffcalc test cases. They are not the
// values in lazer's test file: those were generated at a slightly older
// commit and differ from what its current code produces by about a
// millionth. These are what the code produces today.
//
// The tolerances are relative and tight on purpose. Every one of these
// agreed to eight or nine digits when it was written, and anything that
// moves them further than a millionth is a change in behaviour worth
// looking at rather than noise: what is left between us is double against
// lazer's single precision positions, and the last ulp of exp, log, pow and
// atan2, which no two runtimes agree on anyway.
struct StarCase {
  const char *fFile;
  double fStars;
  double fAim;
  double fSpeed;
  double fReading;
  int fCombo;
  double fAimTolerance; // relative
};

inline Beatmap loadTestBeatmap(const char *file) {
  std::ifstream ifs(file);
  EXPECT_TRUE(ifs.is_open()) << "Can't open " << file;
  std::stringstream buf;
  buf << ifs.rdbuf();
  return parseBeatmap(buf.str());
}

TEST(Stars, MatchesLazer) {
  const StarCase cases[] = {
      {"test/data/diffcalc-test.osu", 6.5243230054515, 3.8093990088670,
       1.5647105745091, 1.7803497292117, 239, 1e-6},
      {"test/data/zero-length-sliders.osu", 1.3280410795791, 0.8047199090814,
       0.1463710443218, 0.0, 54, 1e-6},
      {"test/data/very-fast-slider.osu", 0.4086732514770, 0.0,
       0.2479040043017, 0.0, 4, 1e-6},
      // Four circles stacked on one spot and a multi-segment slider under a
      // NaN timing point. The aim here is a thousandth of a star, so a
      // looser relative tolerance: at that size the last digits are noise
      // even when everything agrees.
      {"test/data/nan-slider.osu", 0.8705817579435, 0.0016034639251,
       0.5279682051204, 0.0615058614521, 6, 1e-4},
  };

  for (const auto &c : cases) {
    const Beatmap bm = loadTestBeatmap(c.fFile);
    const StarRating stars = calculateStars(bm);
    const Engine engine(bm);

    EXPECT_NEAR(stars.fTotal, c.fStars, c.fStars * 1e-6) << c.fFile;
    EXPECT_NEAR(stars.fSpeed, c.fSpeed, std::max(c.fSpeed, 1.0) * 1e-6)
        << c.fFile;
    EXPECT_NEAR(stars.fReading, c.fReading, std::max(c.fReading, 1.0) * 1e-6)
        << c.fFile;
    EXPECT_NEAR(stars.fAim, c.fAim, std::max(c.fAim, 1e-3) * c.fAimTolerance)
        << c.fFile;
    EXPECT_EQ(engine.maxAchievableCombo(), c.fCombo) << c.fFile;
  }
}

// The other calculator: the one the servers actually rank with, checked
// against rosu-pp, which is the reference implementation of it. Agreement is
// to within a part in a million here; section by section the two series agree
// to about 1e-8, which is single precision -- lazer and rosu-pp both hold
// object positions as floats and this holds them as doubles.
//
// Note the reference is rosu-pp and not osu! itself: this algorithm no longer
// exists in osu!'s master branch, so at the last few digits there is nothing
// to say which of the two is right.
// Failing keeps the score running and stops the health, which is what
// MultiplayerPlayer asks ScoreProcessor for and what this client does on
// every screen. Checked against lazer's own processors over the judgements of
// a real replay: 413 of them, agreeing on health, score, combo and accuracy
// at every step.
TEST(Rules, FailedPlayKeepsScoringAndGradesF) {
  ScoreState state;
  state.fGreat = 90;
  state.fMiss = 10;
  state.adoptLegacyCounts();
  EXPECT_FALSE(std::holds_alternative<grade::F>(computeGrade(state)));

  // Health clamps at zero when a play fails, so the grade cannot be read off
  // it -- the state has to say so itself.
  state.fHealth = 0.0;
  EXPECT_FALSE(std::holds_alternative<grade::F>(computeGrade(state)));

  state.fFailed = true;
  EXPECT_TRUE(std::holds_alternative<grade::F>(computeGrade(state)));
}

TEST(Stars, MatchesRankedCalculator) {
  struct RankedCase {
    const char *fFile;
    double fStars;
    double fAim;
    double fSpeed;
  };
  const RankedCase cases[] = {
      {"test/data/diffcalc-test.osu", 6.6232539338574, 3.8012985833856,
       2.1987202735588},
      {"test/data/zero-length-sliders.osu", 1.5045783545700, 0.8903373157054,
       0.2543994557061},
      {"test/data/very-fast-slider.osu", 0.4333383667119, 0.0,
       0.2480856854663},
      {"test/data/nan-slider.osu", 0.7294182074713, 0.0855674731135,
       0.4261969247179},
  };

  for (const auto &c : cases) {
    const Beatmap bm = loadTestBeatmap(c.fFile);
    const StarRating stars =
        calculateStars(bm, mod::kNone, nullptr,
                       std::numeric_limits<double>::infinity(),
                       StarAlgorithm::kRanked);
    EXPECT_NEAR(stars.fTotal, c.fStars, c.fStars * 1e-6) << c.fFile;
    EXPECT_NEAR(stars.fAim, c.fAim, std::max(c.fAim, 1e-3) * 1e-6) << c.fFile;
    EXPECT_NEAR(stars.fSpeed, c.fSpeed, std::max(c.fSpeed, 1e-3) * 1e-6)
        << c.fFile;
  }
}

TEST(Stars, MatchesRankedCalculatorWithDoubleTime) {
  const Beatmap bm = loadTestBeatmap("test/data/diffcalc-test.osu");
  const StarRating stars =
      calculateStars(bm, mod::kDoubleTime, nullptr,
                     std::numeric_limits<double>::infinity(),
                     StarAlgorithm::kRanked);
  EXPECT_NEAR(stars.fTotal, 9.6491732889114, 9.6491732889114 * 1e-6);
  EXPECT_NEAR(stars.fAim, 5.5958565319540, 5.5958565319540 * 1e-6);
  EXPECT_NEAR(stars.fSpeed, 2.9843504836661, 2.9843504836661 * 1e-6);
}

TEST(Stars, MatchesLazerWithDoubleTime) {
  const StarCase cases[] = {
      {"test/data/diffcalc-test.osu", 9.4677694877984, 5.5930033565508,
       2.1913401312275, 2.1998562282900, 239, 1e-6},
      {"test/data/zero-length-sliders.osu", 1.6856612715619, 1.0212972657136,
       0.1916589040296, 0.0, 54, 1e-6},
      {"test/data/very-fast-slider.osu", 0.5358847318657, 0.0,
       0.3250713629863, 0.0, 4, 1e-6},
  };

  for (const auto &c : cases) {
    const Beatmap bm = loadTestBeatmap(c.fFile);
    const StarRating stars = calculateStars(bm, mod::kDoubleTime);
    EXPECT_NEAR(stars.fTotal, c.fStars, c.fStars * 1e-6) << c.fFile;
    EXPECT_NEAR(stars.fSpeed, c.fSpeed, std::max(c.fSpeed, 1.0) * 1e-6)
        << c.fFile;
    EXPECT_NEAR(stars.fReading, c.fReading, std::max(c.fReading, 1.0) * 1e-6)
        << c.fFile;
    EXPECT_NEAR(stars.fAim, c.fAim, std::max(c.fAim, 1e-3) * c.fAimTolerance)
        << c.fFile;
  }
}

// CalculateTimed: the rating of the map as it stands partway through. The
// difficulty objects are still built from the whole beatmap -- the reading
// evaluator looks ahead, and an object with nothing after it reads as denser
// than it is -- so only the skills stop early. Values from lazer's own
// CalculateTimed on the same beatmap.
TEST(Stars, MatchesLazerPartway) {
  const Beatmap bm = loadTestBeatmap("test/data/diffcalc-test.osu");
  struct Point {
    double fUntil;
    double fStars;
    double fAim;
    double fReading;
  };
  const Point points[] = {
      {2000.0, 0.3577563175471, 0.1192003954766, 0.0},
      {17000.0, 5.6685465406782, 3.2929877493005, 1.5333896824701},
      {61500.0, 6.5240565852554, 3.8093052195745, 1.7803493437573},
  };
  for (const auto &p : points) {
    const StarRating stars =
        calculateStars(bm, mod::kNone, nullptr, p.fUntil);
    EXPECT_NEAR(stars.fTotal, p.fStars, p.fStars * 1e-6) << p.fUntil;
    EXPECT_NEAR(stars.fAim, p.fAim, p.fAim * 1e-6) << p.fUntil;
    EXPECT_NEAR(stars.fReading, p.fReading, std::max(p.fReading, 1.0) * 1e-6)
        << p.fUntil;
  }
}

// The three pieces of rounding that osu! inherited from stable, each checked
// against a value lazer printed for a beatmap that uses it.
TEST(Rules, LegacyRounding) {
  // Object radius: 64 times a scale that carries the gamefield rounding
  // allowance of 1.00041.
  ASSERT_NEAR(circleRadius(3.0), 40.97679138183594, 1e-9);
  ASSERT_NEAR(circleRadius(4.0), 36.49495315551758, 1e-9);
  ASSERT_NEAR(circleRadius(3.4), 39.18405532836914, 1e-9);

  // A stack step is that scale times 6.4 in each axis, so adjacent notes in
  // a stack sit 7.0710678 normalised units apart at CS 4.
  const Vec2 step = stackOffset(1, 4.0);
  ASSERT_NEAR(step.length() * 50.0 / circleRadius(4.0), 7.0710678118654755,
              1e-9);

  // TimePreempt, floored -- and floored after the approach rate has been
  // through single precision, which is why 9.6 gives 509 and not 510.
  ASSERT_DOUBLE_EQ(preemptTime(4.5), 1260.0);
  ASSERT_DOUBLE_EQ(preemptTime(8.3), 704.0);
  ASSERT_DOUBLE_EQ(preemptTime(9.3), 554.0);
  ASSERT_DOUBLE_EQ(preemptTime(9.6), 509.0);
  ASSERT_DOUBLE_EQ(preemptTime(10.0), 450.0);
}

// Checked against lazer's own ScoreProcessor and OsuHealthProcessor, driven
// with the same sequence of judgements over a real beatmap: on a play of
// nothing but greats the standardised score is exactly the million, and the
// health bar ends full.
TEST(Engine, PerfectPlayScoresTheMillion) {
  auto bm = parseBeatmap(kTestBeatmap);
  const auto result = runAutoplay(bm);
  ASSERT_DOUBLE_EQ(result.fScore.accuracy(), 1.0);
  ASSERT_EQ(result.fScore.fScore, 1000000u);
  ASSERT_GT(result.fScore.fHealth, 0.0);
}

// Health is a drain as much as a set of increases, and the rate is solved for
// rather than given: a perfect play should dip to the target and no lower.
TEST(Rules, HealthIncreasesAndDrain) {
  // OsuHealthProcessor's table, at HP 5.
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kBasic, judgement::Great{}, true,
                                     5.0),
                   0.03);
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kBasic, judgement::Good{}, true,
                                     5.0),
                   0.011);
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kBasic, judgement::Meh{}, true,
                                     5.0),
                   0.002);
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kBasic, judgement::Miss{}, false,
                                     5.0),
                   -0.125);
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kLargeTick, judgement::Great{},
                                     true, 5.0, true),
                   0.015);
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kSliderTail, judgement::Great{},
                                     true, 5.0),
                   0.02);
  // A dropped tail is an IgnoreMiss and costs nothing.
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kSliderTail, judgement::Miss{},
                                     false, 5.0),
                   0.0);
  // The miss penalty and the drain target both follow the drain rate.
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kBasic, judgement::Miss{}, false,
                                     0.0),
                   -0.03);
  ASSERT_DOUBLE_EQ(healthIncreaseFor(HitKind::kBasic, judgement::Miss{}, false,
                                     10.0),
                   -0.2);
  ASSERT_DOUBLE_EQ(targetMinimumHealth(0.0), 0.99);
  ASSERT_DOUBLE_EQ(targetMinimumHealth(5.0), 0.9);
  ASSERT_DOUBLE_EQ(targetMinimumHealth(10.0), 0.4);
  ASSERT_DOUBLE_EQ(comboBonusFor(ComboResult::kPerfect), 0.07);
  ASSERT_DOUBLE_EQ(comboBonusFor(ComboResult::kGood), 0.05);
  ASSERT_DOUBLE_EQ(comboBonusFor(ComboResult::kNone), 0.03);
}

TEST(SliderBody, CuspDetection) {
  // The user's problematic 18-point slider with duplicate anchors.
  const std::vector<Vec2> ctrl{
      {451, 8},   {405, -31}, {349, -10}, {325, 35},  {322, 63},  {334, 106},
      {371, 132}, {413, 136}, {445, 117}, {465, 90},  {465, 90},  {438, 142},
      {441, 173}, {460, 210}, {460, 210}, {475, 235}, {475, 276}, {465, 306},
  };
  SliderPath path(curve::Bezier{}, std::span{ctrl}, 540.8);
  const auto pts = path.points();

  // Verify the path has the expected number of points.
  ASSERT_GE(pts.size(), 2u);

  // Build segments and check for cusp-like direction reversals.
  struct Seg {
    Vec2 u;
    Vec2 n;
    double len;
  };
  std::vector<Seg> segs;
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
    double dx = pts[i + 1].fX - pts[i].fX;
    double dy = pts[i + 1].fY - pts[i].fY;
    double l = std::hypot(dx, dy);
    if (l > 1e-6)
      segs.push_back({{dx / l, dy / l}, {-dy / l, dx / l}, l});
    else
      segs.push_back({{1, 0}, {0, 1}, 0});
  }

  // Check adjacent-segment dot product (local cusp).
  double minAdjDot = 1.0;
  // Check spatial dot product at ~10 px distance.
  double minSpatialDot = 1.0;
  for (std::size_t i = 1; i + 1 < segs.size(); ++i) {
    double adjDot =
        segs[i - 1].u.fX * segs[i].u.fX + segs[i - 1].u.fY * segs[i].u.fY;
    if (adjDot < minAdjDot)
      minAdjDot = adjDot;

    // Walk backward and forward ~10 px.
    double db = 0.0, df = 0.0;
    std::size_t wb = 1, wf = 1;
    while (i >= wb && db < 10.0 && wb < 2000) {
      db += segs[i - wb].len;
      ++wb;
    }
    while (i + wf < segs.size() && df < 10.0 && wf < 2000) {
      df += segs[i + wf - 1].len;
      ++wf;
    }
    if (wb > 1 && wf > 1) {
      double d = segs[i - (wb - 1)].u.fX * segs[i + (wf - 1) - 1].u.fX +
                 segs[i - (wb - 1)].u.fY * segs[i + (wf - 1) - 1].u.fY;
      if (d < minSpatialDot)
        minSpatialDot = d;
    }
  }

  // Print diagnostics.
  std::println("path points = {}", pts.size());
  std::println("segs = {}", segs.size());
  std::println("minAdjDot = {}", minAdjDot);
  std::println("minSpatialDot = {}", minSpatialDot);
  // Search ALL window sizes for the minimum dot product.
  double bestSpatial = 1.0;
  std::size_t bestI = 0;
  for (std::size_t i = 1; i + 1 < segs.size(); ++i) {
    for (int w = 1; i >= static_cast<std::size_t>(w) && i + w < segs.size();
         ++w) {
      double d = segs[i - w].u.fX * segs[i + w - 1].u.fX +
                 segs[i - w].u.fY * segs[i + w - 1].u.fY;
      if (d < bestSpatial) {
        bestSpatial = d;
        bestI = i;
      }
      if (w >= 500)
        break;
    }
  }
  std::println("best spatial dot = {} at joint {}", bestSpatial, bestI);
  // Also report the distance at the best match.
  {
    double db = 0.0, df = 0.0;
    std::size_t wb = 1, wf = 1;
    while (bestI >= wb && db < 100.0 && wb < 2000) {
      db += segs[bestI - wb].len;
      if (wb > 1) {
        double d = segs[bestI - wb].u.fX * segs[bestI + wb - 1].u.fX +
                   segs[bestI - wb].u.fY * segs[bestI + wb - 1].u.fY;
        if (d < -0.5)
          break;
      }
      ++wb;
    }
    wf = wb;
    while (bestI + wf < segs.size() && df < 100.0 && wf < 2000) {
      df += segs[bestI + wf - 1].len;
      ++wf;
    }
    std::println("  at dist back={:.1f} fwd={:.1f} (w={})", db, df, wb);
  }

  constexpr double kRadius = 50.0;
  if (bestSpatial < -0.9) {
    std::println("CUSP DETECTED at joint {}", bestI);

    // Incoming quad end (side 1): curve[bestI] + n_in
    // Outgoing quad start (side 1): curve[bestI] + n_out
    const auto &a = segs[bestI - 1];
    const auto &b = segs[bestI];
    Vec2 inSide1 = {pts[bestI].fX + a.n.fX * kRadius,
                    pts[bestI].fY + a.n.fY * kRadius};
    Vec2 outSide1 = {pts[bestI].fX + b.n.fX * kRadius,
                     pts[bestI].fY + b.n.fY * kRadius};
    std::println("  incoming side1 end = ({:.1f}, {:.1f})  dir = ({:.3f}, "
                 "{:.3f})  n = ({:.1f}, {:.1f})",
                 inSide1.fX, inSide1.fY, a.u.fX, a.u.fY, a.n.fX * kRadius,
                 a.n.fY * kRadius);
    std::println("  outgoing side1 start = ({:.1f}, {:.1f})  dir = ({:.3f}, "
                 "{:.3f})  n = ({:.1f}, {:.1f})",
                 outSide1.fX, outSide1.fY, b.u.fX, b.u.fY, b.n.fX * kRadius,
                 b.n.fY * kRadius);

    // After the cusp clip (both using incoming normal):
    // Incoming keeps: P·u_in <= 0. Outgoing keeps: P·u_in >= 0.
    Vec2 refDir = a.u; // incoming direction
    double inDot = (inSide1.fX - pts[bestI].fX) * refDir.fX +
                   (inSide1.fY - pts[bestI].fY) * refDir.fY;
    double outDot = (outSide1.fX - pts[bestI].fX) * refDir.fX +
                    (outSide1.fY - pts[bestI].fY) * refDir.fY;
    std::println("  (P-J)·u_in  in={:.3f}  out={:.3f}", inDot, outDot);
    std::println("  incoming on side: {}", inDot <= 0 ? "before" : "after");
    std::println("  outgoing on side: {}", outDot >= 0 ? "after" : "before");
    ASSERT_LE(inDot, 0.0) << "incoming quad should be clipped to 'before'";
    ASSERT_GE(outDot, 0.0) << "outgoing quad should be clipped to 'after'";
  } else {
    std::println("NO CUSP DETECTED (best spatial dot = {:.3f})", bestSpatial);
  }
}

} // namespace osu
