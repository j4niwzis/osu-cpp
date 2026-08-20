export module archive;

import std;
import osu;
import osu.stars;
import osz;
import client.util;

namespace client {

namespace detail {

[[nodiscard]] inline std::vector<std::uint8_t>
readDirFile(const std::filesystem::path &path) {
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
    out.emplace(name, readDirFile(entry.path()));
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

// Star ratings are the expensive half of loading a set -- seconds, over a
// library -- and a client that has scanned before already knows them. Loading
// for the audio, the objects or the artwork asks for them off.
export [[nodiscard]] inline osu::BeatmapSet
loadBeatmapSet(const std::filesystem::path &path, bool computeStars = true) {
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
    if (computeStars) {
      info.fStars = osu::calculateStars(bm).fTotal;
    }
    info.fMd5 = osu::md5HashString(it->second);
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
        if (computeStars) {
          info.fStars = osu::calculateStars(bm).fTotal;
          std::println(std::cerr, "[stars] {} -> {:.2f}*", info.fMeta.fVersion,
                       info.fStars);
        }
        info.fMd5 = osu::md5HashString(bytes);
        set.fBeatmaps.push_back(std::move(info));
      } catch (const osu::UnsupportedModeError &) {
        // Skip non-standard difficulties (taiko, catch, mania).
      }
    }
    if (computeStars) {
      // Ordered by difficulty, which is the order everything else assumes --
      // the picker, the cache, and an index into either. Without the ratings
      // this sort has nothing to order by and would shuffle the difficulties
      // into an arbitrary order instead, so the caller fills the ratings in
      // from its cache and sorts then.
      std::ranges::sort(set.fBeatmaps, {}, &osu::BeatmapInfo::fStars);
    }
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
    throw std::runtime_error{"beatmap file not found in set: " +
                             info.fFilename};
  }
  const std::string text(bytes.begin(), bytes.end());
  return osu::loadBeatmap(text);
}

} // namespace client
