export module archive;

import std;
import osu;
import osu.stars;
import osz;

namespace client {

namespace detail {

template <class F> class ScopeGuard {
public:
  explicit ScopeGuard(F f) : fFunc(std::move(f)) {}
  ~ScopeGuard() {
    if (fActive) {
      fFunc();
    }
  }
  ScopeGuard(const ScopeGuard &) = delete;
  ScopeGuard &operator=(const ScopeGuard &) = delete;
  ScopeGuard(ScopeGuard &&other) noexcept
      : fFunc(std::move(other.fFunc)), fActive(other.fActive) {
    other.fActive = false;
  }
  ScopeGuard &operator=(ScopeGuard &&) = delete;
  void dismiss() noexcept { fActive = false; }

private:
  F fFunc;
  bool fActive = true;
};

template <class F> ScopeGuard<F> scopeGuard(F f) { return ScopeGuard<F>(std::move(f)); }

inline std::string toLower(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

inline bool isOsz(const std::filesystem::path &path) {
  return toLower(path.extension().string()) == ".osz";
}

inline bool isOsu(std::string_view name) {
  return toLower(std::string(name)).ends_with(".osu");
}

[[nodiscard]] inline std::vector<std::uint8_t>
readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error{"failed to read file: " + path.string()};
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

[[nodiscard]] inline std::unordered_map<std::string, std::vector<std::uint8_t>>
readDirectory(const std::filesystem::path &dir) {
  std::unordered_map<std::string, std::vector<std::uint8_t>> out;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    out.emplace(name, readFile(entry.path()));
  }
  return out;
}

[[nodiscard]] inline std::unordered_map<std::string, std::vector<std::uint8_t>>
readOsz(const std::filesystem::path &path) {
  std::unordered_map<std::string, std::vector<std::uint8_t>> out;
  osz::zip_t *handle = osz::zip_open(path.c_str(), osz::kRdOnly, nullptr);
  if (handle == nullptr) {
    throw std::runtime_error{"failed to open osz archive: " + path.string()};
  }
  const auto close = detail::scopeGuard([&] { osz::zip_close(handle); });

  const osz::zip_int64_t count = osz::zip_get_num_entries(handle, 0);
  if (count < 0) {
    throw std::runtime_error{"failed to read osz entries: " + path.string()};
  }

  for (osz::zip_uint64_t i = 0; i < static_cast<osz::zip_uint64_t>(count);
       ++i) {
    const char *rawName = osz::zip_get_name(handle, i, osz::kFlEncGuess);
    if (rawName == nullptr) {
      continue;
    }
    const std::string name(rawName);
    if (name.empty() || name.back() == '/') {
      continue;
    }

    osz::zip_stat_t stat{};
    if (osz::zip_stat_index(handle, i, 0, &stat) < 0) {
      continue;
    }

    osz::zip_file_t *file = osz::zip_fopen_index(handle, i, 0);
    if (file == nullptr) {
      continue;
    }
    const auto closeFile = detail::scopeGuard([&] { osz::zip_fclose(file); });

    std::vector<std::uint8_t> buffer(stat.size);
    if (!buffer.empty()) {
      const osz::zip_int64_t read =
          osz::zip_fread(file, buffer.data(), buffer.size());
      if (read < 0 || static_cast<osz::zip_uint64_t>(read) != stat.size) {
        continue;
      }
    }
    out.emplace(name, std::move(buffer));
  }
  return out;
}

} // namespace detail

export [[nodiscard]] inline osu::BeatmapSet loadBeatmapSet(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error{"beatmap path does not exist: " + path.string()};
  }

  osu::BeatmapSet set;
  if (std::filesystem::is_directory(path)) {
    set.fFiles = detail::readDirectory(path);
  } else if (detail::isOsz(path)) {
    set.fFiles = detail::readOsz(path);
  } else if (detail::isOsu(path.filename().string())) {
    set.fFiles = detail::readDirectory(path.parent_path());
    const std::string target = path.filename().string();
    const auto it = set.fFiles.find(target);
    if (it == set.fFiles.end()) {
      throw std::runtime_error{"beatmap file not found: " + path.string()};
    }
    const std::string text(it->second.begin(), it->second.end());
    const osu::Beatmap bm = osu::loadBeatmap(text);
    auto info = osu::buildBeatmapInfo(target, bm);
    info.fStars = osu::calculateStars(bm).fTotal;
    set.fBeatmaps.push_back(std::move(info));
  } else {
    throw std::runtime_error{"unsupported beatmap path: " + path.string()};
  }

  if (set.fBeatmaps.empty()) {
    for (const auto &[name, bytes] : set.fFiles) {
      if (!detail::isOsu(name)) {
        continue;
      }
      try {
        const std::string text(bytes.begin(), bytes.end());
        const osu::Beatmap bm = osu::loadBeatmap(text);
    auto info = osu::buildBeatmapInfo(name, bm);
    info.fStars = osu::calculateStars(bm).fTotal;
    std::println("{} -> {:.2f}*", info.fMeta.fVersion, info.fStars);
    set.fBeatmaps.push_back(std::move(info));
      } catch (const osu::UnsupportedModeError &) {
        // Skip non-standard difficulties (taiko, catch, mania).
      }
    }
    std::ranges::sort(set.fBeatmaps, {}, &osu::BeatmapInfo::fStars);
  }

  if (set.fBeatmaps.empty()) {
    throw std::runtime_error{"no beatmaps found in: " + path.string()};
  }
  return set;
}

export [[nodiscard]] inline osu::Beatmap
loadBeatmap(const osu::BeatmapSet &set, const osu::BeatmapInfo &info) {
  const auto bytes = set.findFile(info.fFilename);
  if (bytes.empty()) {
    throw std::runtime_error{"beatmap file not found in set: " + info.fFilename};
  }
  const std::string text(bytes.begin(), bytes.end());
  return osu::loadBeatmap(text);
}

} // namespace client
