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
    } else if (arg == "--dt") {
      mods |= osu::mod::kDoubleTime;
    } else if (arg == "--ht") {
      mods |= osu::mod::kHalfTime;
    } else if (arg == "--hr") {
      mods |= osu::mod::kHardRock;
    } else if (arg == "--ez") {
      mods |= osu::mod::kEasy;
    } else if (arg == "--beatmap" && i + 1 < args.size()) {
      beatmapPath = args[++i];
    } else if (arg == "--skin" && i + 1 < args.size()) {
      skinPath = args[++i];
    } else if (arg == "--replay" && i + 1 < args.size()) {
      replayPath = args[++i];
      autoplay = true;
    } else if (!arg.starts_with('-')) {
      beatmapPath = arg;
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

  // Difficulty only: parse the map, print what the algorithm makes of it,
  // and exit. Nothing is opened and no cache is consulted, so this is what
  // the code in front of you computes rather than what was computed once.
  if (starsOnly) {
    if (beatmapPath.empty()) {
      std::cerr << "Error: --stars requires a beatmap\n";
      return 1;
    }
    try {
      std::ifstream in(beatmapPath, std::ios::binary);
      if (!in) {
        std::cerr << "Error: cannot read " << beatmapPath.string() << '\n';
        return 1;
      }
      const std::string text((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
      const osu::Beatmap map = osu::loadBeatmap(text);
      const osu::StarRating rating = osu::calculateStars(map, mods);
      const osu::Engine engine(map, mods);
      std::cout << std::format("{}\n", beatmapPath.filename().string())
                << std::format("  stars     {:.13f}\n", rating.fTotal)
                << std::format("  aim       {:.13f}\n", rating.fAim)
                << std::format("  speed     {:.13f}\n", rating.fSpeed)
                << std::format("  max combo {}\n", engine.maxAchievableCombo())
                << std::format("  objects   {}\n", map.fObjects.size());
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << '\n';
      return 1;
    }
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
