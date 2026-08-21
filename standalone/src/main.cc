import std;
import osu;
import app;
import archive;
import osz;

#ifdef __EMSCRIPTEN__
#include "emscripten_macro.h"

extern "C" {
EMSCRIPTEN_KEEPALIVE int extractSkin() {
  osz::zip_t *handle = osz::zip_open("/skin.osk", osz::kRdOnly, nullptr);
  if (handle == nullptr)
    return 1;

  const osz::zip_int64_t count = osz::zip_get_num_entries(handle, 0);
  if (count < 0) {
    osz::zip_close(handle);
    return 1;
  }

  std::filesystem::create_directories("/skin");

  for (osz::zip_uint64_t i = 0; i < static_cast<osz::zip_uint64_t>(count);
       ++i) {
    const char *rawName = osz::zip_get_name(handle, i, osz::kFlEncGuess);
    if (rawName == nullptr)
      continue;
    std::string name(rawName);
    if (name.empty() || name.back() == '/')
      continue;

    osz::zip_stat_t stat{};
    if (osz::zip_stat_index(handle, i, 0, &stat) < 0)
      continue;

    osz::zip_file_t *file = osz::zip_fopen_index(handle, i, 0);
    if (file == nullptr)
      continue;

    std::vector<std::uint8_t> buffer(stat.size);
    if (!buffer.empty()) {
      const osz::zip_int64_t read =
          osz::zip_fread(file, buffer.data(), buffer.size());
      if (read < 0 || static_cast<osz::zip_uint64_t>(read) != stat.size) {
        osz::zip_fclose(file);
        continue;
      }
    }
    osz::zip_fclose(file);

    std::filesystem::path outPath = std::filesystem::path("/skin") / name;
    std::filesystem::create_directories(outPath.parent_path());

    std::ofstream out(outPath, std::ios::binary);
    if (!out)
      continue;
    for (auto b : buffer)
      out.put(static_cast<char>(b));
  }
  osz::zip_close(handle);
  return 0;
}
}
#endif

namespace {

std::filesystem::path executablePath(const char *argv0) {
#ifdef __linux__
  std::error_code ec;
  auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec && !self.empty()) {
    return self;
  }
#endif
  std::filesystem::path argPath = argv0 ? argv0 : "osu_client";
  if (argPath.has_parent_path() || argPath.is_absolute()) {
    return argPath;
  }
  const char *pathEnv = std::getenv("PATH");
  if (pathEnv == nullptr) {
    return argPath;
  }
  std::string_view remaining = pathEnv;
  while (!remaining.empty()) {
    const std::size_t colon = remaining.find(':');
    const std::string_view dir = remaining.substr(0, colon);
    if (!dir.empty()) {
      std::filesystem::path candidate = std::filesystem::path(dir) / argPath;
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }
    if (colon == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(colon + 1);
  }
  return argPath;
}

void printUsage(std::string_view program) {
  std::cout
      << "Usage: " << program << " [options] <beatmap.osu>\n"
      << "Options:\n"
      << "  --beatmap <path>   Path to the .osu beatmap file\n"
      << "  --skin <path>      Path to an osu! skin folder\n"
      << "  --headless         Run without a window (implies autoplay)\n"
      << "  --autoplay         Let the engine play the beatmap automatically\n"
      << "  --replay <path>    Play a saved .osr replay file\n"
      << "  --record           Record input events and save to .osr after "
         "play\n"
      << "  --stars            Print the star rating of the beatmap and "
         "exit\n"
      << "  --until <ms>       With --stars, only objects up to this time\n"
      << "  --dump-aim         With --stars, print the per-object aim "
         "strain\n"
      << "  --ranked           With --stars, use the calculator the servers "
         "run\n"
      << "  --trace-replay     Play a replay through the engine and print "
         "every judgement\n"
      << "  --dt               Apply DoubleTime\n"
      << "  --ht               Apply HalfTime\n"
      << "  --hr               Apply HardRock\n"
      << "  --ez               Apply Easy\n"
      << "  --help             Show this help message\n";
}

} // namespace

int main(int argc, char **argv) {
  const std::vector<std::string_view> args(argv + 1, argv + argc);

  std::filesystem::path beatmapPath;
  std::filesystem::path skinPath;
  std::filesystem::path replayPath;
  bool headless = false;
  bool autoplay = false;
  bool record = false;
  bool profile = false;
  bool starsOnly = false;
  double until = std::numeric_limits<double>::infinity();
  bool dumpAim = false;
  bool traceReplay = false;
  osu::StarAlgorithm algorithm = osu::StarAlgorithm::kLazerMaster;
  osu::ModSet mods = osu::mod::kNone;

  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "--help" || arg == "-h") {
      printUsage(argc > 0 ? argv[0] : "osu_client");
      return 0;
    }
    if (arg == "--headless") {
      headless = true;
      autoplay = true;
    } else if (arg == "--autoplay") {
      autoplay = true;
    } else if (arg == "--record") {
      record = true;
    } else if (arg == "--profile") {
      profile = true;
    } else if (arg == "--stars") {
      starsOnly = true;
      // Optional path right after it, so both spellings work.
      if (i + 1 < args.size() && !args[i + 1].starts_with('-')) {
        beatmapPath = args[++i];
      }
    } else if (arg == "--dt") {
      mods |= osu::mod::kDoubleTime;
    } else if (arg == "--ht") {
      mods |= osu::mod::kHalfTime;
    } else if (arg == "--hr") {
      mods |= osu::mod::kHardRock;
    } else if (arg == "--ez") {
      mods |= osu::mod::kEasy;
    } else if (arg == "--dump-aim") {
      dumpAim = true;
    } else if (arg == "--ranked") {
      algorithm = osu::StarAlgorithm::kRanked;
    } else if (arg == "--trace-replay") {
      traceReplay = true;
    } else if (arg == "--until" && i + 1 < args.size()) {
      until = std::stod(std::string(args[++i]));
    } else if (arg == "--beatmap" && i + 1 < args.size()) {
      beatmapPath = args[++i];
    } else if (arg == "--skin" && i + 1 < args.size()) {
      skinPath = args[++i];
    } else if (arg == "--replay" && i + 1 < args.size()) {
      replayPath = args[++i];
      autoplay = true;
    } else if (!arg.starts_with('-')) {
      beatmapPath = arg;
    } else if (arg == "--beatmap" || arg == "--skin" || arg == "--replay" ||
               arg == "--until") {
      std::cerr << "Error: " << arg << " needs a path after it\n";
      printUsage(argc > 0 ? argv[0] : "osu_client");
      return 1;
    } else {
      // Silently ignoring an option nobody knows means a stale binary looks
      // like a broken feature: run --stars against a build that predates it
      // and the client just opens.
      std::cerr << "Error: unknown option " << arg << '\n';
      printUsage(argc > 0 ? argv[0] : "osu_client");
      return 1;
    }
  }

  // A beatmap argument is now optional: without one the client opens song
  // select over the local library (maps/ next to the executable, or /maps in
  // the browser). Headless mode still needs a concrete map.
  if (!beatmapPath.empty() && !std::filesystem::exists(beatmapPath)) {
    std::cerr << "Error: beatmap path does not exist\n";
    printUsage(argc > 0 ? argv[0] : "osu_client");
    return 1;
  }
  if (beatmapPath.empty() && headless) {
    std::cerr << "Error: --headless requires a beatmap\n";
    return 1;
  }

  if (skinPath.empty()) {
    const auto exeDir =
        executablePath(argc > 0 ? argv[0] : "osu_client").parent_path();
    const std::vector<std::filesystem::path> candidates{
        exeDir / "skin",
        exeDir / ".." / "skin",
        exeDir / ".." / "share" / "osu_client" / "skin",
    };
    for (const auto &candidate : candidates) {
      auto canonical = std::filesystem::weakly_canonical(candidate);
      if (std::filesystem::exists(canonical)) {
        skinPath = canonical.make_preferred();
        break;
      }
    }
    if (skinPath.empty()) {
      skinPath = std::filesystem::path{"skin"};
    }
  }

  // A replay, judged: every result the engine produces, with the health and
  // the score after it. This is the sequence to hold against lazer's own
  // processors -- they are plain classes, so the same list can be fed to them
  // and compared line for line.
  if (traceReplay) {
    if (beatmapPath.empty() || replayPath.empty()) {
      std::cerr << "Error: --trace-replay needs a beatmap and --replay\n";
      return 1;
    }
    try {
      std::ifstream mapFile(beatmapPath, std::ios::binary);
      const std::string text((std::istreambuf_iterator<char>(mapFile)),
                             std::istreambuf_iterator<char>());
      const osu::Beatmap map = osu::loadBeatmap(text);

      std::ifstream replayFile(replayPath, std::ios::binary);
      const std::vector<std::uint8_t> bytes{
          std::istreambuf_iterator<char>(replayFile),
          std::istreambuf_iterator<char>()};
      const osu::ReplayData replay = osu::decodeReplay(bytes);

      const osu::RuleSet rules = replay.fVersion < osu::kLazerRulesVersion
                                     ? osu::RuleSet::kLegacyClient
                                     : osu::RuleSet::kLazer;
      std::cout << std::format(
          "replay version {} rules {} mods {} events {}\n", replay.fVersion,
          rules == osu::RuleSet::kLazer ? "lazer" : "legacy",
          replay.fMods.fValue, replay.fEvents.size());

      osu::Engine engine(map, replay.fMods, rules);
      std::size_t seen = 0;
      const auto flush = [&] {
        const auto events = engine.events();
        while (seen < events.size()) {
          const auto &e = events[seen++];
          const char *kind = e.fKind == osu::HitKind::kBasic     ? "basic"
                             : e.fKind == osu::HitKind::kLargeTick ? "tick"
                             : e.fKind == osu::HitKind::kSliderTail
                                 ? "tail"
                             : e.fKind == osu::HitKind::kSmallBonus
                                 ? "smallbonus"
                                 : "largebonus";
          const auto &s = engine.score();
          std::cout << std::format(
              "judge {:8.1f} obj {:4} {:<10} {:<5} health {:.9f} score {:7}"
              " combo {:4} acc {:.9f}\n",
              e.fIndex < map.fObjects.size()
                  ? osu::startTime(map.fObjects[e.fIndex])
                  : 0.0,
              e.fIndex, kind, osu::judgementInfo(e.fResult).first, s.fHealth,
              s.fScore, s.fCombo, s.accuracy());
        }
      };
      for (const auto &ev : replay.fEvents) {
        engine.submit(ev);
        flush();
      }
      engine.advance(map.lastObjectEndTime() + 1000.0);
      flush();
      const auto &s = engine.score();
      std::cout << std::format(
          "final health {:.9f} score {} accuracy {:.9f} combo {}/{} "
          "great {} ok {} meh {} miss {} tick {}/{} tail {}/{} failed {}\n",
          s.fHealth, s.fScore, s.accuracy(), s.fMaxCombo,
          engine.maxAchievableCombo(), s.fGreat, s.fGood, s.fMeh, s.fMiss,
          s.fLargeTickHit, s.fLargeTickHit + s.fLargeTickMiss, s.fTailHit,
          s.fTailHit + s.fTailMiss, engine.failed());
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << '\n';
      return 1;
    }
  }

  // Difficulty only: parse the map, print what the algorithm makes of it,
  // and exit. Nothing is opened and no cache is consulted, so this is what
  // the code in front of you computes rather than what was computed once.
  if (starsOnly) {
    if (beatmapPath.empty()) {
      std::cerr << "Error: --stars requires a beatmap\n";
      return 1;
    }
    // A single .osu, or a directory of them, which is how the test maps come.
    std::vector<std::filesystem::path> targets;
    if (std::filesystem::is_directory(beatmapPath)) {
      for (const auto &entry :
           std::filesystem::directory_iterator(beatmapPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".osu") {
          targets.push_back(entry.path());
        }
      }
      std::ranges::sort(targets);
    } else {
      targets.push_back(beatmapPath);
    }
    if (targets.empty()) {
      std::cerr << "Error: no .osu files in " << beatmapPath.string() << '\n';
      return 1;
    }
    int failures = 0;
    for (const auto &target : targets) {
      try {
        std::ifstream in(target, std::ios::binary);
        if (!in) {
          throw std::runtime_error{"cannot read " + target.string()};
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        const osu::Beatmap map = osu::loadBeatmap(text);
        std::vector<osu::stars::AimSkill::TracePoint> aimTrace;
        const osu::StarRating rating = osu::calculateStars(
            map, mods, dumpAim ? &aimTrace : nullptr, until, algorithm);
        // The combo and object counts are of the part that was processed,
        // which is what the timed tests assert.
        osu::Beatmap counted = map;
        if (std::isfinite(until)) {
          std::size_t keep = 0;
          while (keep < counted.fObjects.size() &&
                 osu::startTime(counted.fObjects[keep]) <= until) {
            ++keep;
          }
          counted.fObjects.resize(keep);
          counted.fSliderPaths.resize(keep);
        }
        const std::size_t processed = counted.fObjects.size();
        const osu::Engine engine(counted, mods);
        std::cout << std::format("{}\n", target.filename().string())
                  << std::format("  stars     {:.13f}\n", rating.fTotal)
                  << std::format("  aim       {:.13f}\n", rating.fAim)
                  << std::format("  speed     {:.13f}\n", rating.fSpeed)
                  << std::format("  reading   {:.13f}\n", rating.fReading)
                  << std::format("  readval   {:.10f}\n", rating.fReadingValue)
                  << std::format("  reduced   {:.0f}\n", rating.fReducedNotes)
                  << std::format("  max combo {}\n",
                                 engine.maxAchievableCombo())
                  << std::format("  objects   {}\n", processed);
        if (dumpAim) {
          // The geometry behind those numbers: what each slider's path came
          // out as, which is the other half of any disagreement about aim.
          for (std::size_t i = 0; i < map.fObjects.size(); ++i) {
            const auto *slider = std::get_if<osu::Slider>(&map.fObjects[i]);
            if (slider == nullptr) {
              continue;
            }
            std::cout << std::format(
                "slider {:.1f} spans {} declared {:.6f} path {:.6f}"
                " span {:.4f} tick {:.4f}\n",
                slider->fTime, slider->fRepeat, slider->fPixelLength,
                map.fSliderPaths[i].length(), map.sliderSpanDuration(*slider),
                map.sliderTickDistance(*slider));
          }
        }
        for (const auto &point : aimTrace) {
          std::cout << std::format(
              "strain {:.1f} snap {:.10f} agility {:.10f} flow {:.10f}"
              " total {:.10f} angle {:.4f} last {:.4f} rep {:.6f}"
              " jump {:.4f} lazytravel {:.6f} travel {:.6f}"
              " traveltime {:.4f}\n",
              point.fTime, point.fSnap, point.fAgility, point.fFlow,
              point.fStrain, point.fAngle, point.fLastAngle,
              point.fRepetition, point.fJump, point.fLazyTravel,
              point.fTravel, point.fTravelTime);
        }
      } catch (const std::exception &e) {
        std::cerr << target.filename().string() << ": " << e.what() << '\n';
        ++failures;
      }
    }
    return failures == 0 ? 0 : 1;
  }

  try {
    std::optional<osu::BeatmapSet> set;
    if (!beatmapPath.empty()) {
      set = client::loadBeatmapSet(beatmapPath);
    }
    client::App app(std::move(set), mods, headless, autoplay, replayPath,
                    record, skinPath, profile);
    return app.run();
  } catch (const osu::ParseError &e) {
    std::cerr << "Parse error: " << e.what() << '\n';
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
}
