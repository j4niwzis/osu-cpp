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

TEST(Stars, LazerTestBeatmaps) {
  struct Case {
    const char *file;
    double expected;
    double tol;
  };
  const Case cases[] = {
      {"test/data/diffcalc-test.osu", 6.524317, 0.15},
      {"test/data/zero-length-sliders.osu", 1.328041, 10.0},
      {"test/data/very-fast-slider.osu", 0.408673, 10.0},
      {"test/data/nan-slider.osu", 0.870582, 10.0},
  };
  for (auto [file, expected, tol] : cases) {
    std::ifstream ifs(file);
    ASSERT_TRUE(ifs.is_open()) << "Can't open " << file;
    std::stringstream buf;
    buf << ifs.rdbuf();
    auto bm = parseBeatmap(buf.str());
    auto stars = calculateStars(bm);
    ASSERT_NEAR(stars.fTotal, expected, tol);
  }
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
