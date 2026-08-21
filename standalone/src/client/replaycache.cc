export module client.replaycache;

import std;
import osu;
import bjson;

export namespace client {

// One replay, as much of it as a list needs.
struct ReplayIndexEntry {
  std::filesystem::path fPath;
  std::string fLabel; // file stem: difficulty and the time it was saved
  std::uintmax_t fSize = 0;
  std::int64_t fMtime = 0;
  std::string fBeatmapMd5;
  osu::ReplayScore fScore;
  std::string fGrade;
  bool fHasScore = false;
  // Which rules the play was made under. Recorded here rather than in the
  // .osr, whose every field belongs to osu!'s format and is read by osu!.
  // -1 when the file came from somewhere else and nothing is known.
  int fRules = -1;
  // No seed frame in the events: one of the files this client wrote before
  // the format was fixed. Those predate the rules being osu!'s, so they only
  // ever play back under the old model.
  bool fLegacyFormat = false;
};

// An index of the replay directory.
//
// The list beside a beatmap only wants the replays of that one difficulty, and
// the selection moves with every keypress in song select. Answering that by
// re-reading and re-decoding every .osr each time is work proportional to the
// whole collection for a question about one map, so the headers are read once,
// written to disk beside the map cache, and kept grouped by beatmap: a
// selection change is then a hash lookup.
//
// Entries are keyed by file name and validated by size and mtime, the same
// identity test the map cache uses; a file that changed is re-read, one that
// vanished is dropped. The header is parsed from a prefix of the file, so
// indexing never decompresses the input events.
class ReplayIndex {
public:
  // Bump when the field layout below changes.
  static constexpr int kSchemaVersion = 2;
  void load(const std::filesystem::path &file) {
    fFile = file;
    fEntries.clear();
    std::ifstream in(file);
    if (!in) {
      return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    const auto parsed = bjson::tryParse(text);
    const bjson::object *root = parsed ? parsed->if_object() : nullptr;
    if (root == nullptr || intOf(root, "schema") != kSchemaVersion) {
      return;
    }
    const bjson::value *listVal = root->if_contains("replays");
    const bjson::array *list = listVal ? listVal->if_array() : nullptr;
    if (list == nullptr) {
      return;
    }
    for (const auto &v : *list) {
      const bjson::object *o = v.if_object();
      if (o == nullptr) {
        continue;
      }
      ReplayIndexEntry e;
      const std::string name = strOf(o, "file");
      if (name.empty()) {
        continue;
      }
      e.fLabel = strOf(o, "label");
      e.fSize = static_cast<std::uintmax_t>(numOf(o, "size"));
      e.fMtime = static_cast<std::int64_t>(numOf(o, "mtime"));
      e.fBeatmapMd5 = strOf(o, "map");
      e.fGrade = strOf(o, "grade");
      e.fHasScore = numOf(o, "hasScore") != 0.0;
      e.fScore.f300 = static_cast<std::uint16_t>(numOf(o, "c300"));
      e.fScore.f100 = static_cast<std::uint16_t>(numOf(o, "c100"));
      e.fScore.f50 = static_cast<std::uint16_t>(numOf(o, "c50"));
      e.fScore.fMiss = static_cast<std::uint16_t>(numOf(o, "miss"));
      e.fScore.fTotalScore = static_cast<std::int32_t>(numOf(o, "score"));
      e.fScore.fMaxCombo = static_cast<std::uint16_t>(numOf(o, "combo"));
      e.fScore.fPerfect = numOf(o, "perfect") != 0.0;
      e.fRules = o->if_contains("rules") ? intOf(o, "rules") : -1;
      e.fLegacyFormat = numOf(o, "legacyFormat") != 0.0;
      fEntries.emplace(name, std::move(e));
    }
  }

  // Bring the index in line with the directory: stat everything, read only
  // what is new or has changed.
  void refresh(const std::filesystem::path &dir) {
    fDir = dir;
    std::error_code ec;
    std::set<std::string, std::less<>> seen;
    int read = 0;
    for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
      if (!e.is_regular_file(ec) || e.path().extension() != ".osr") {
        continue;
      }
      const std::string name = e.path().filename().string();
      const auto size = e.file_size(ec);
      const auto mtime = static_cast<std::int64_t>(
          e.last_write_time(ec).time_since_epoch().count());
      seen.insert(name);
      const auto it = fEntries.find(name);
      if (it != fEntries.end() && it->second.fSize == size &&
          it->second.fMtime == mtime) {
        it->second.fPath = e.path(); // paths are not persisted
        continue;
      }
      if (auto entry = readHeader(e.path(), size, mtime)) {
        fEntries[name] = std::move(*entry);
        ++read;
        fDirty = true;
      } else if (it != fEntries.end()) {
        fEntries.erase(it); // unreadable now, so it does not belong in a list
        fDirty = true;
      }
    }
    for (auto it = fEntries.begin(); it != fEntries.end();) {
      if (seen.contains(it->first)) {
        ++it;
      } else {
        it = fEntries.erase(it);
        fDirty = true;
      }
    }
    if (fDirty) {
      std::println(std::cerr, "[replays] indexed {} of {} ({} read)",
                   fEntries.size(), seen.size(), read);
      this->rebuildGroups();
      this->save();
    } else if (fByBeatmap.empty() && !fEntries.empty()) {
      this->rebuildGroups();
    }
  }

  // A replay that has just been written; no need to re-scan the directory.
  // `rules` is what it was played under: 0 for osu!'s, 1 for this client's
  // old model, -1 when there is nothing to say.
  void add(const std::filesystem::path &path, int rules = -1) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto mtime = static_cast<std::int64_t>(
        std::filesystem::last_write_time(path, ec).time_since_epoch().count());
    if (auto entry = readHeader(path, size, mtime)) {
      entry->fRules = rules;
      fEntries[path.filename().string()] = std::move(*entry);
      fDirty = true;
      this->rebuildGroups();
      this->save();
    }
  }

  // The replays of one beatmap, newest first. No file system access.
  [[nodiscard]] std::span<const ReplayIndexEntry *const>
  forBeatmap(const std::string &md5) const {
    const auto it = fByBeatmap.find(md5);
    if (it == fByBeatmap.end()) {
      return {};
    }
    return it->second;
  }

  [[nodiscard]] std::size_t size() const noexcept { return fEntries.size(); }

  void save() {
    if (!fDirty || fFile.empty()) {
      return;
    }
    bjson::object root;
    root["schema"] = kSchemaVersion;
    bjson::array list;
    for (const auto &[name, e] : fEntries) {
      bjson::object o;
      o["file"] = name;
      o["label"] = e.fLabel;
      o["size"] = static_cast<std::int64_t>(e.fSize);
      o["mtime"] = e.fMtime;
      o["map"] = e.fBeatmapMd5;
      o["grade"] = e.fGrade;
      o["hasScore"] = e.fHasScore ? 1 : 0;
      o["c300"] = static_cast<int>(e.fScore.f300);
      o["c100"] = static_cast<int>(e.fScore.f100);
      o["c50"] = static_cast<int>(e.fScore.f50);
      o["miss"] = static_cast<int>(e.fScore.fMiss);
      o["score"] = static_cast<int>(e.fScore.fTotalScore);
      o["combo"] = static_cast<int>(e.fScore.fMaxCombo);
      o["perfect"] = e.fScore.fPerfect ? 1 : 0;
      o["rules"] = e.fRules;
      o["legacyFormat"] = e.fLegacyFormat ? 1 : 0;
      list.push_back(std::move(o));
    }
    root["replays"] = std::move(list);
    std::ofstream out(fFile, std::ios::trunc);
    if (!out) {
      std::println(std::cerr, "[replays] cannot write {}", fFile.string());
      return;
    }
    out << bjson::serialize(bjson::value(std::move(root)));
    fDirty = false;
  }

private:
  // Reads what a list needs out of one file. The header alone would come out
  // of a prefix, but whether the replay is one of the old ones is answered by
  // the seed frame, which is inside the compressed events, so the whole file
  // is read and the events decompressed. That happens once: the answer is
  // written to the index and the file is not opened again until it changes.
  [[nodiscard]] static std::optional<ReplayIndexEntry>
  readHeader(const std::filesystem::path &path, std::uintmax_t size,
             std::int64_t mtime) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const bool legacyFormat = osu::replayIsLegacyFormat(bytes);
    try {
      const auto header = osu::decodeReplayHeader(bytes);
      ReplayIndexEntry e;
      e.fPath = path;
      e.fLabel = path.stem().string();
      e.fSize = size;
      e.fMtime = mtime;
      e.fBeatmapMd5 = header.fBeatmapMd5;
      e.fScore = header.fScore;
      e.fHasScore = header.fScore.totalHits() > 0;
      e.fLegacyFormat = legacyFormat;
      if (e.fHasScore) {
        osu::ScoreState state;
        state.fGreat = header.fScore.f300;
        state.fGood = header.fScore.f100;
        state.fMeh = header.fScore.f50;
        state.fMiss = header.fScore.fMiss;
        state.adoptLegacyCounts();
        state.fScore = static_cast<std::uint64_t>(header.fScore.fTotalScore);
        state.fMaxCombo = header.fScore.fMaxCombo;
        e.fGrade = osu::gradeString(osu::computeGrade(state));
      }
      return e;
    } catch (const std::exception &) {
      return std::nullopt; // truncated or not a replay at all
    }
  }

  void rebuildGroups() {
    fByBeatmap.clear();
    for (auto &[name, e] : fEntries) {
      if (e.fPath.empty()) {
        e.fPath = fDir / name;
      }
      fByBeatmap[e.fBeatmapMd5].push_back(&e);
    }
    for (auto &group : fByBeatmap) {
      std::ranges::sort(group.second, std::ranges::greater{},
                        &ReplayIndexEntry::fMtime);
    }
  }

  [[nodiscard]] static std::string strOf(const bjson::object *o,
                                         std::string_view key) {
    if (const bjson::value *v = o->if_contains(key)) {
      if (const auto *s = v->if_string()) {
        return std::string(s->begin(), s->end());
      }
    }
    return {};
  }
  [[nodiscard]] static double numOf(const bjson::object *o,
                                    std::string_view key) {
    if (const bjson::value *v = o->if_contains(key)) {
      if (v->is_number()) {
        return v->to_number<double>();
      }
    }
    return 0.0;
  }
  [[nodiscard]] static int intOf(const bjson::object *o, std::string_view key) {
    return static_cast<int>(numOf(o, key));
  }

  std::filesystem::path fFile;
  std::filesystem::path fDir;
  // Node-stable, so the grouped pointers below stay valid.
  std::map<std::string, ReplayIndexEntry, std::less<>> fEntries;
  std::map<std::string, std::vector<const ReplayIndexEntry *>, std::less<>>
      fByBeatmap;
  bool fDirty = false;
};

} // namespace client
