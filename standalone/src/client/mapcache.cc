export module client.mapcache;

import std;
import osu;
import bjson;

export namespace client {

// On-disk cache of beatmap metadata and star ratings.
//
// Scanning a library means unzipping every .osz, parsing every difficulty and
// running the star algorithm over it -- seconds per launch once the library is
// more than a handful of maps. The results only change when the archive
// changes or when the algorithm does, so both are versioned here: entries are
// keyed by file identity (size + mtime) and stamped with the algorithm
// version, and any mismatch simply recomputes that set.
struct CachedDifficulty {
  std::string fFilename;
  std::string fMd5;
  int fSetId = 0; // BeatmapSetID from the .osu, so a re-download is spotted
  std::string fTitle, fTitleUnicode, fArtist, fArtistUnicode, fCreator,
      fVersion, fAudioFilename, fBackground;
  double fStars = 0.0;
  double fCs = 5.0, fAr = 9.0, fOd = 8.0, fHp = 5.0;
  double fLengthMs = 0.0;
  int fObjectCount = 0;
};

struct CachedSet {
  std::uintmax_t fSize = 0;
  std::int64_t fMtime = 0;
  std::vector<CachedDifficulty> fDifficulties;
};

class MapCache {
public:
  // Bump when the star algorithm changes so stale ratings are recomputed.
  // 2: strain peak capping, slider repeat/tick thresholds, spinners no longer
  //    skipped by the speed and reading skills.
  // 3: the angle fix (atan2 of a zero vector), the reduction loop, zero-length
  //    sliders, NaN green lines, stack heights, curve approximation.
  // 4: slider path extension, RepeatCount off by one, the normalised vector
  //    angle of a zero vector. This is the version that matches lazer's own
  //    diffcalc tests to within a ten-thousandth.
  static constexpr int kStarAlgorithmVersion = 4;
  // Bump when this file's field layout changes.
  static constexpr int kSchemaVersion = 3;

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
    if (!parsed) {
      return;
    }
    const bjson::object *root = parsed->if_object();
    if (root == nullptr) {
      return;
    }
    if (intOf(root, "schema") != kSchemaVersion ||
        intOf(root, "stars") != kStarAlgorithmVersion) {
      std::println(std::cerr, "[cache] version mismatch, rebuilding");
      return;
    }
    const bjson::value *setsVal = root->if_contains("sets");
    const bjson::object *sets = setsVal ? setsVal->if_object() : nullptr;
    if (sets == nullptr) {
      return;
    }
    for (const auto &kv : *sets) {
      const bjson::object *o = kv.value().if_object();
      if (o == nullptr) {
        continue;
      }
      CachedSet cs;
      cs.fSize = static_cast<std::uintmax_t>(numOf(o, "size"));
      cs.fMtime = static_cast<std::int64_t>(numOf(o, "mtime"));
      const bjson::value *diffsVal = o->if_contains("diffs");
      const bjson::array *diffs = diffsVal ? diffsVal->if_array() : nullptr;
      if (diffs != nullptr) {
        for (const auto &d : *diffs) {
          const bjson::object *dobj = d.if_object();
          if (dobj == nullptr) {
            continue;
          }
          CachedDifficulty cd;
          cd.fFilename = strOf(dobj, "file");
          cd.fMd5 = strOf(dobj, "md5");
          cd.fSetId = intOf(dobj, "setId");
          cd.fTitle = strOf(dobj, "title");
          cd.fTitleUnicode = strOf(dobj, "titleU");
          cd.fArtist = strOf(dobj, "artist");
          cd.fArtistUnicode = strOf(dobj, "artistU");
          cd.fCreator = strOf(dobj, "creator");
          cd.fVersion = strOf(dobj, "version");
          cd.fAudioFilename = strOf(dobj, "audio");
          cd.fBackground = strOf(dobj, "bg");
          cd.fStars = numOf(dobj, "stars");
          cd.fCs = numOf(dobj, "cs");
          cd.fAr = numOf(dobj, "ar");
          cd.fOd = numOf(dobj, "od");
          cd.fHp = numOf(dobj, "hp");
          cd.fLengthMs = numOf(dobj, "len");
          cd.fObjectCount = static_cast<int>(numOf(dobj, "objects"));
          cs.fDifficulties.push_back(std::move(cd));
        }
      }
      fEntries.emplace(std::string(kv.key()), std::move(cs));
    }
    std::println(std::cerr, "[cache] loaded {} sets", fEntries.size());
  }

  // Returns the cached difficulties if the archive is unchanged.
  [[nodiscard]] const std::vector<CachedDifficulty> *
  lookup(const std::string &key, std::uintmax_t size, std::int64_t mtime) const {
    const auto it = fEntries.find(key);
    if (it == fEntries.end()) {
      return nullptr;
    }
    if (it->second.fSize != size || it->second.fMtime != mtime) {
      return nullptr;
    }
    return &it->second.fDifficulties;
  }

  void store(const std::string &key, std::uintmax_t size, std::int64_t mtime,
             std::vector<CachedDifficulty> diffs) {
    fEntries[key] = CachedSet{size, mtime, std::move(diffs)};
    fDirty = true;
  }

  // A beatmap that has been deleted has no business staying in the cache.
  void remove(const std::string &key) {
    if (fEntries.erase(key) > 0) {
      fDirty = true;
    }
  }

  void save() {
    if (!fDirty || fFile.empty()) {
      return;
    }
    bjson::object root;
    root["schema"] = kSchemaVersion;
    root["stars"] = kStarAlgorithmVersion;
    bjson::object sets;
    for (const auto &[key, cs] : fEntries) {
      bjson::object o;
      o["size"] = static_cast<std::int64_t>(cs.fSize);
      o["mtime"] = cs.fMtime;
      bjson::array diffs;
      for (const auto &d : cs.fDifficulties) {
        bjson::object dobj;
        dobj["file"] = d.fFilename;
        dobj["md5"] = d.fMd5;
        dobj["setId"] = d.fSetId;
        dobj["title"] = d.fTitle;
        dobj["titleU"] = d.fTitleUnicode;
        dobj["artist"] = d.fArtist;
        dobj["artistU"] = d.fArtistUnicode;
        dobj["creator"] = d.fCreator;
        dobj["version"] = d.fVersion;
        dobj["audio"] = d.fAudioFilename;
        dobj["bg"] = d.fBackground;
        dobj["stars"] = d.fStars;
        dobj["cs"] = d.fCs;
        dobj["ar"] = d.fAr;
        dobj["od"] = d.fOd;
        dobj["hp"] = d.fHp;
        dobj["len"] = d.fLengthMs;
        dobj["objects"] = d.fObjectCount;
        diffs.push_back(std::move(dobj));
      }
      o["diffs"] = std::move(diffs);
      sets[key] = std::move(o);
    }
    root["sets"] = std::move(sets);

    std::ofstream out(fFile, std::ios::trunc);
    if (!out) {
      std::println(std::cerr, "[cache] cannot write {}", fFile.string());
      return;
    }
    out << bjson::serialize(bjson::value(std::move(root)));
    fDirty = false;
  }

private:
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
  [[nodiscard]] static int intOf(const bjson::object *o,
                                 std::string_view key) {
    return static_cast<int>(numOf(o, key));
  }

  std::filesystem::path fFile;
  std::map<std::string, CachedSet, std::less<>> fEntries;
  bool fDirty = false;
};

} // namespace client
