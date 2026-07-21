import std;
import osu;
import app;

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
  bool headless = false;
  bool autoplay = false;
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
    } else if (!arg.starts_with('-')) {
      beatmapPath = arg;
    }
  }

  if (beatmapPath.empty() || !std::filesystem::exists(beatmapPath)) {
    std::cerr << "Error: beatmap path not provided or does not exist\n";
    printUsage(argc > 0 ? argv[0] : "osu_client");
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

  try {
    osu::Beatmap map = osu::loadBeatmap(beatmapPath);
    const std::filesystem::path beatmapDir = beatmapPath.parent_path();
    client::App app(std::move(map), mods, headless, autoplay, beatmapDir,
                    skinPath);
    return app.run();
  } catch (const osu::ParseError &e) {
    std::cerr << "Parse error: " << e.what() << '\n';
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
}
