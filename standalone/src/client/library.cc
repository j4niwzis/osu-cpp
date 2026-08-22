export module client.library;

import std;
import osu;
import skia;
import skin;
import client.filter;
import client.loader;
import archive;
import client.mapcache;

// The beatmaps this client knows about: what is on disk, which of them pass
// the filter, and which one is chosen.
//
// Entries hold metadata only. The full BeatmapSet -- every file in the
// archive, decoded audio, images -- is loaded on demand and a handful are
// kept alive at a time; keeping every set resident made startup slow and the
// resident set enormous once the library grew.
export namespace client::library {

// How the list is ordered. Named here rather than taken from the widget that
// offers the choice: the ordering is a property of the library, and the
// library has no business importing a control that draws dropdowns.
enum class Sort : std::uint8_t {
  kAuthor,
  kTitle,
  kArtist,
  kDifficulty,
  kLength,
};

// Library entries hold metadata only. The full BeatmapSet (every file in
// the archive, decoded audio, images) is loaded on demand and a handful are
// kept alive at a time -- keeping every set resident made startup slow and
// the resident set enormous once the library grew.
struct Entry {
  std::filesystem::path fPath; // empty for the set passed on the CLI
  std::vector<osu::BeatmapInfo> fInfos;
  std::shared_ptr<osu::BeatmapSet> fLoaded; // nullptr until needed
  skia::Sp<skia::SkImage> fPanelArt;        // lazily fetched cover
  bool fPanelArtTried = false;
};

class Library {
public:
  void configure(Loader &loader, std::filesystem::path mapsDir,
                 std::filesystem::path thumbDir, std::function<void()> sync) {
    fLoader = &loader;
    fMapsDir = std::move(mapsDir);
    fThumbDir = std::move(thumbDir);
    fSync = std::move(sync);
  }

  void loadCache() { fCache.load(fMapsDir / "metadata-cache.json"); }
  [[nodiscard]] const std::vector<Entry> &sets() const { return fSets; }
  [[nodiscard]] std::vector<Entry> &sets() { return fSets; }
  [[nodiscard]] const std::vector<int> &visible() const { return fVisible; }
  [[nodiscard]] bool empty() const { return fSets.empty(); }
  // Which set and which of its difficulties is chosen. Handed out as a
  // reference, the way listing hands out its filters: the selection is moved
  // from a dozen places -- the carousel, the arrow keys, a random pick, a
  // finished import -- and each of them owns the move.
  [[nodiscard]] int &selSet() { return fSelSet; }
  [[nodiscard]] int &selDiff() { return fSelDiff; }
  [[nodiscard]] int selSet() const { return fSelSet; }
  [[nodiscard]] int selDiff() const { return fSelDiff; }
  [[nodiscard]] std::deque<int> &loadedOrder() { return fLoadedOrder; }
  void markDirty() { fDirty = true; }
  [[nodiscard]] bool ranked() const { return fRanked; }
  void setRanked(bool ranked) { fRanked = ranked; }
  void setSort(Sort sort) { fSort = sort; }
  void setRange(double lo, double hi) {
    fRangeMin = lo;
    fRangeMax = hi;
  }
  void add(Entry entry) { fSets.push_back(std::move(entry)); }
  void noteLoaded(int index) { fLoadedOrder.push_back(index); }
  void erase(std::size_t index) {
    fSets.erase(fSets.begin() + static_cast<std::ptrdiff_t>(index));
  }

  // Library / song select (lazer-style carousel on the right).

  // The maps this client knows about, which of them are listed, and
  // which one is chosen. Everything the library screens read.

  [[nodiscard]] static std::pair<std::uintmax_t, std::int64_t>
  fileStamp(const std::filesystem::path &path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    return {size,
            static_cast<std::int64_t>(writeTime.time_since_epoch().count())};
  }

  [[nodiscard]] std::optional<Entry>
  cachedEntryFor(const std::filesystem::path &path) {
    const auto [size, mtime] = fileStamp(path);
    const auto *cached = fCache.lookup(path.filename().string(), size, mtime);
    if (cached == nullptr || cached->empty()) {
      return std::nullopt;
    }
    Entry entry;
    entry.fPath = path;
    for (const auto &d : *cached) {
      entry.fInfos.push_back(infoFromCache(d));
    }
    return entry;
  }

  [[nodiscard]] static osu::BeatmapInfo
  infoFromCache(const CachedDifficulty &d) {
    osu::BeatmapInfo info;
    info.fFilename = d.fFilename;
    info.fMd5 = d.fMd5;
    info.fMeta.fBeatmapSetId = d.fSetId;
    info.fMeta.fTitle = d.fTitle;
    info.fMeta.fTitleUnicode = d.fTitleUnicode;
    info.fMeta.fArtist = d.fArtist;
    info.fMeta.fArtistUnicode = d.fArtistUnicode;
    info.fMeta.fCreator = d.fCreator;
    info.fMeta.fVersion = d.fVersion;
    info.fMeta.fAudioFilename = d.fAudioFilename;
    info.fMeta.fBackground = d.fBackground;
    info.fStars = d.fStars;
    info.fStarsRanked = d.fStarsRanked;
    info.fDiff.fCs = d.fCs;
    info.fDiff.fAr = d.fAr;
    info.fDiff.fOd = d.fOd;
    info.fDiff.fHp = d.fHp;
    info.fLengthMs = d.fLengthMs;
    info.fObjectCount = d.fObjectCount;
    return info;
  }

  // Which of the two ratings to put in front of the user. The order maps and
  // difficulties are kept in is not this: that stays on the master rating,
  // because the cached list is written in that order and a difficulty is
  // chosen by its position in it. Flipping the setting would otherwise
  // reorder one side and not the other.
  [[nodiscard]] double shownStars(const osu::BeatmapInfo &info) const {
    return fRanked ? info.fStarsRanked : info.fStars;
  }

  [[nodiscard]] static std::vector<CachedDifficulty>
  cacheRecordFor(const osu::BeatmapSet &set) {
    std::vector<CachedDifficulty> diffs;
    diffs.reserve(set.fBeatmaps.size());
    for (const auto &info : set.fBeatmaps) {
      diffs.push_back(
          {info.fFilename, info.fMd5, info.fMeta.fBeatmapSetId,
           info.fMeta.fTitle, info.fMeta.fTitleUnicode, info.fMeta.fArtist,
           info.fMeta.fArtistUnicode, info.fMeta.fCreator, info.fMeta.fVersion,
           info.fMeta.fAudioFilename, info.fMeta.fBackground, info.fStars,
           info.fStarsRanked, info.fDiff.fCs, info.fDiff.fAr, info.fDiff.fOd,
           info.fDiff.fHp, info.fLengthMs, info.fObjectCount});
    }
    return diffs;
  }

  // Load unchanged entries from the metadata cache and parse cache misses in
  // parallel. Workers only produce independent values; Library and MapCache
  // remain owned by the calling thread and are updated after every join.
  void scanArchives() {
    std::vector<std::filesystem::path> archives;
    std::error_code iterEc;
    for (const auto &e :
         std::filesystem::directory_iterator(fMapsDir, iterEc)) {
      if (e.is_regular_file() && e.path().extension() == ".osz") {
        archives.push_back(e.path());
      }
    }

    std::vector<std::filesystem::path> misses;
    for (const auto &path : archives) {
      if (auto cached = this->cachedEntryFor(path)) {
        fSets.push_back(std::move(*cached));
      } else {
        misses.push_back(path);
      }
    }
    std::println(std::cerr, "[cache] reused {} sets; {} stale or new",
                 archives.size() - misses.size(), misses.size());

    struct ParsedArchive {
      Entry fEntry;
      std::vector<CachedDifficulty> fDiffs;
    };
    std::vector<std::optional<ParsedArchive>> parsed(misses.size());
    if (!misses.empty()) {
      const auto threads =
          std::max(1u, std::min(std::thread::hardware_concurrency(),
                                static_cast<unsigned>(misses.size())));
      std::println(std::cerr, "[library] parsing {} new sets on {} threads",
                   misses.size(), threads);
      std::atomic<std::size_t> next{0};
      std::vector<std::thread> pool;
      pool.reserve(threads);
      for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
          for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (i >= misses.size()) {
              return;
            }
            try {
              const auto set = loadBeatmapSet(misses[i]);
              Entry entry;
              entry.fPath = misses[i];
              entry.fInfos = set.fBeatmaps;
              parsed[i].emplace(std::move(entry), cacheRecordFor(set));
            } catch (const std::exception &e) {
              std::println(std::cerr, "[library] skipping {}: {}",
                           misses[i].filename().string(), e.what());
            }
          }
        });
      }
      for (auto &thread : pool) {
        thread.join();
      }
    }

    for (std::size_t i = 0; i < parsed.size(); ++i) {
      if (!parsed[i]) {
        continue;
      }
      const auto stamp = fileStamp(misses[i]);
      fSets.push_back(std::move(parsed[i]->fEntry));
      fCache.store(misses[i].filename().string(), stamp.first, stamp.second,
                   std::move(parsed[i]->fDiffs));
    }
    fCache.save();
  }

  // Metadata for one archive, from the cache when the file is unchanged.
  void scanArchive(const std::filesystem::path &path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    const auto mtime =
        static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    const std::string key = path.filename().string();

    if (const auto *cached = fCache.lookup(key, size, mtime)) {
      Entry entry;
      entry.fPath = path;
      for (const auto &d : *cached) {
        entry.fInfos.push_back(infoFromCache(d));
      }
      if (!entry.fInfos.empty()) {
        fSets.push_back(std::move(entry));
        return;
      }
    }

    // Cache miss: parse the archive once, then remember the result.
    try {
      auto set = std::make_shared<osu::BeatmapSet>(loadBeatmapSet(path));
      Entry entry;
      entry.fPath = path;
      entry.fInfos = set->fBeatmaps;
      fSets.push_back(std::move(entry));

      fCache.store(key, size, mtime, cacheRecordFor(*set));
    } catch (const std::exception &e) {
      std::println(std::cerr, "[library] skipping {}: {}", key, e.what());
    }
  }

  // Full set for an entry if it is already resident. Never blocks: a miss
  // queues a background load and returns null, and the caller retries on a
  // later frame. Unzipping an archive and decoding its audio takes hundreds
  // of milliseconds, which is a visible freeze when done inline.
  [[nodiscard]] std::shared_ptr<osu::BeatmapSet> setFor(int index) {
    if (index < 0 || index >= static_cast<int>(fSets.size())) {
      return nullptr;
    }
    auto &entry = fSets[static_cast<std::size_t>(index)];
    if (entry.fLoaded) {
      return entry.fLoaded;
    }
    if (entry.fPath.empty()) {
      return nullptr;
    }
    this->requestSet(index);
    return nullptr;
  }

  // Blocking load, for the one case that genuinely cannot proceed without
  // the data: starting gameplay.
  [[nodiscard]] std::shared_ptr<osu::BeatmapSet> setForBlocking(int index) {
    if (index < 0 || index >= static_cast<int>(fSets.size())) {
      return nullptr;
    }
    auto &entry = fSets[static_cast<std::size_t>(index)];
    if (entry.fLoaded) {
      return entry.fLoaded;
    }
    if (entry.fPath.empty()) {
      return nullptr;
    }
    try {
      entry.fLoaded =
          std::make_shared<osu::BeatmapSet>(loadBeatmapSet(entry.fPath, false));
      this->adoptCachedStars(index, *entry.fLoaded);
      this->touchLoaded(index);
    } catch (const std::exception &e) {
      std::println(std::cerr, "[library] load failed {}: {}",
                   entry.fPath.filename().string(), e.what());
      return nullptr;
    }
    return entry.fLoaded;
  }

  void requestSet(int index) {
    const auto path = fSets[static_cast<std::size_t>(index)].fPath;
    if (path.empty()) {
      return;
    }
    auto result = std::make_shared<std::shared_ptr<osu::BeatmapSet>>();
    fLoader->submit(
        static_cast<std::uint64_t>(index) | (1ull << 32),
        [path, result] {
          *result =
              std::make_shared<osu::BeatmapSet>(loadBeatmapSet(path, false));
        },
        [this, index, path, result] {
          if (index >= static_cast<int>(fSets.size()) ||
              fSets[static_cast<std::size_t>(index)].fPath != path) {
            return; // library was re-sorted underneath us
          }
          if (!*result) {
            return;
          }
          // The scan already worked the star ratings out and wrote them to the
          // cache; this load was for the objects and the audio.
          this->adoptCachedStars(index, **result);
          fSets[static_cast<std::size_t>(index)].fLoaded = *result;
          this->touchLoaded(index);
        });
  }

  // The library knows the star ratings from its cache, so a set loaded for
  // its objects and its audio takes them from there rather than spending
  // seconds working them out again -- and is then put into the same order as
  // that cached list, because a difficulty is chosen by its position in it.
  //
  // The order is taken from the list by name, one entry at a time. Sorting
  // both sides by star rating and trusting them to agree, which is what this
  // did, only holds while no two difficulties in a set share a rating: a
  // stable sort leaves ties in the order it was given, and the two sides are
  // given different orders -- the cached list is in whatever order it was
  // written in, the loaded set in whatever order the archive lists its files.
  // A set with two difficulties of the same rating would then play one while
  // the client believed it was the other, showing the wrong replays, the
  // wrong difficulty name and saving new replays under the wrong one.
  void adoptCachedStars(int index, osu::BeatmapSet &set) const {
    const auto &known = this->infosFor(index);
    for (auto &info : set.fBeatmaps) {
      for (const auto &cached : known) {
        if (cached.fFilename == info.fFilename) {
          info.fStars = cached.fStars;
          info.fStarsRanked = cached.fStarsRanked;
          break;
        }
      }
    }

    std::vector<osu::BeatmapInfo> ordered;
    ordered.reserve(set.fBeatmaps.size());
    std::vector<bool> taken(set.fBeatmaps.size(), false);
    for (const auto &cached : known) {
      for (std::size_t i = 0; i < set.fBeatmaps.size(); ++i) {
        if (!taken[i] && set.fBeatmaps[i].fFilename == cached.fFilename) {
          ordered.push_back(std::move(set.fBeatmaps[i]));
          taken[i] = true;
          break;
        }
      }
    }
    // Anything the cache does not know about keeps its own order behind the
    // rest, which is the only place it can go without shifting a position the
    // cached list has already claimed.
    for (std::size_t i = 0; i < set.fBeatmaps.size(); ++i) {
      if (!taken[i]) {
        ordered.push_back(std::move(set.fBeatmaps[i]));
      }
    }
    set.fBeatmaps = std::move(ordered);
  }

  void touchLoaded(int index) {
    fLoadedOrder.push_back(index);
    while (fLoadedOrder.size() > kMaxLoadedSets) {
      const int evict = fLoadedOrder.front();
      fLoadedOrder.pop_front();
      if (evict != index && evict >= 0 &&
          evict < static_cast<int>(fSets.size()) &&
          !fSets[static_cast<std::size_t>(evict)].fPath.empty()) {
        fSets[static_cast<std::size_t>(evict)].fLoaded.reset();
      }
    }
  }

  [[nodiscard]] const std::vector<osu::BeatmapInfo> &infosFor(int index) const {
    static const std::vector<osu::BeatmapInfo> kEmpty;
    if (index < 0 || index >= static_cast<int>(fSets.size())) {
      return kEmpty;
    }
    return fSets[static_cast<std::size_t>(index)].fInfos;
  }

  void sortLibrary() {
    const int selected = fSelSet;
    const std::filesystem::path selPath =
        selected >= 0 && selected < static_cast<int>(fSets.size())
            ? fSets[static_cast<std::size_t>(selected)].fPath
            : std::filesystem::path{};

    const auto key = [](const Entry &e) -> const osu::BeatmapInfo * {
      return e.fInfos.empty() ? nullptr : &e.fInfos.front();
    };
    switch (fSort) {
    case Sort::kAuthor:
      std::ranges::stable_sort(fSets, {}, [&](const Entry &e) {
        const auto *i = key(e);
        return i ? toLowerAscii(i->fMeta.fCreator) : std::string{};
      });
      break;
    case Sort::kTitle:
      std::ranges::stable_sort(fSets, {}, [&](const Entry &e) {
        const auto *i = key(e);
        return i ? toLowerAscii(i->fMeta.fTitle) : std::string{};
      });
      break;
    case Sort::kArtist:
      std::ranges::stable_sort(fSets, {}, [&](const Entry &e) {
        const auto *i = key(e);
        return i ? toLowerAscii(i->fMeta.fArtist) : std::string{};
      });
      break;
    case Sort::kDifficulty:
      std::ranges::stable_sort(fSets, {}, [&](const Entry &e) {
        const auto *i = key(e);
        return i ? this->shownStars(*i) : 0.0;
      });
      break;
    case Sort::kLength:
      std::ranges::stable_sort(fSets, {}, [&](const Entry &e) {
        const auto *i = key(e);
        return i ? i->fLengthMs : 0.0;
      });
      break;
    }
    // The LRU holds indices, which the sort just invalidated.
    fLoadedOrder.clear();
    for (int i = 0; i < static_cast<int>(fSets.size()); ++i) {
      if (fSets[static_cast<std::size_t>(i)].fLoaded) {
        fLoadedOrder.push_back(i);
      }
      if (!selPath.empty() &&
          fSets[static_cast<std::size_t>(i)].fPath == selPath) {
        fSelSet = i;
      }
    }
    fDirty = true;
  }

  [[nodiscard]] static std::string toLowerAscii(std::string_view in) {
    std::string out(in);
    std::ranges::transform(out, out.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return out;
  }

  void rebuildVisible(const Criteria &criteria) {
    if (!fDirty) {
      return;
    }
    fDirty = false;
    fVisible.clear();

    for (int i = 0; i < static_cast<int>(fSets.size()); ++i) {
      const auto &infos = fSets[static_cast<std::size_t>(i)].fInfos;
      if (infos.empty()) {
        continue;
      }
      // A set matches when any of its difficulties matches, which is how
      // lazer's carousel filters (sets are hidden only if every child fails).
      bool any = false;
      for (const auto &info : infos) {
        if (this->matchesCriteria(criteria, infos.front().fMeta, info)) {
          any = true;
          break;
        }
      }
      if (any) {
        fVisible.push_back(i);
      }
    }

    if (!fVisible.empty() &&
        std::ranges::find(fVisible, fSelSet) == fVisible.end()) {
      fSelSet = fVisible.front();
      fSelDiff = 0;
    }
  }

  [[nodiscard]] bool matchesCriteria(const Criteria &c,
                                     const osu::Metadata &setMeta,
                                     const osu::BeatmapInfo &info) const {
    // DifficultyRangeSlider is an additional constraint on top of the query.
    const double stars = this->shownStars(info);
    if (stars < fRangeMin || (fRangeMax < kRangeCap && stars > fRangeMax)) {
      return false;
    }
    if (!c.fStars.matches(stars) || !c.fAr.matches(info.fDiff.fAr) ||
        !c.fCs.matches(info.fDiff.fCs) || !c.fOd.matches(info.fDiff.fOd) ||
        !c.fHp.matches(info.fDiff.fHp) ||
        !c.fLengthSec.matches(info.fLengthMs / 1000.0) ||
        !c.fObjects.matches(info.fObjectCount)) {
      return false;
    }
    const auto contains = [](std::string_view hay, const std::string &needle) {
      return needle.empty() ||
             toLowerAscii(hay).find(needle) != std::string::npos;
    };
    if (!contains(info.fMeta.fCreator, c.fCreator)) {
      return false;
    }
    if (!c.fArtist.empty() && !contains(setMeta.fArtist, c.fArtist) &&
        !contains(setMeta.fArtistUnicode, c.fArtist)) {
      return false;
    }
    if (!c.fTitle.empty() && !contains(setMeta.fTitle, c.fTitle) &&
        !contains(setMeta.fTitleUnicode, c.fTitle)) {
      return false;
    }
    if (!contains(info.fMeta.fVersion, c.fDiff)) {
      return false;
    }
    if (c.fSearchText.empty()) {
      return true;
    }
    // Remaining free text: every space-separated term must appear somewhere,
    // as FilterCriteria.Matches does.
    const std::string haystack = toLowerAscii(setMeta.fTitle) + '\x1f' +
                                 toLowerAscii(setMeta.fTitleUnicode) + '\x1f' +
                                 toLowerAscii(setMeta.fArtist) + '\x1f' +
                                 toLowerAscii(setMeta.fArtistUnicode) + '\x1f' +
                                 toLowerAscii(info.fMeta.fCreator) + '\x1f' +
                                 toLowerAscii(info.fMeta.fVersion);
    std::size_t pos = 0;
    while (pos < c.fSearchText.size()) {
      const auto next = c.fSearchText.find(' ', pos);
      const auto term = c.fSearchText.substr(
          pos, next == std::string::npos ? std::string::npos : next - pos);
      if (!term.empty() && haystack.find(term) == std::string::npos) {
        return false;
      }
      if (next == std::string::npos) {
        break;
      }
      pos = next + 1;
    }
    return true;
  }

  [[nodiscard]] int visiblePos() const {
    const auto it = std::ranges::find(fVisible, fSelSet);
    return it == fVisible.end() ? -1 : static_cast<int>(it - fVisible.begin());
  }

  // Point the selection at the set that came from the command line.
  void selectInitialSet() {
    for (std::size_t i = 0; i < fSets.size(); ++i) {
      if (fSets[i].fPath.empty()) {
        fSelSet = static_cast<int>(i);
        fSelDiff = 0;
        return;
      }
    }
  }

  // The online id of a library entry, from the .osu files themselves.
  [[nodiscard]] static int onlineSetId(const Entry &entry) {
    for (const auto &info : entry.fInfos) {
      if (info.fMeta.fBeatmapSetId > 0) {
        return info.fMeta.fBeatmapSetId;
      }
    }
    return 0;
  }

  [[nodiscard]] int libraryIndexForSet(int setId) const {
    if (setId <= 0) {
      return -1;
    }
    for (std::size_t i = 0; i < fSets.size(); ++i) {
      if (onlineSetId(fSets[i]) == setId) {
        return static_cast<int>(i);
      }
    }
    return -1;
  }

  bool addOszToLibrary(const std::filesystem::path &path, bool select,
                       const Criteria &criteria) {
    const std::size_t before = fSets.size();
    this->scanArchive(path);
    if (fSets.size() == before) {
      return false;
    }
    // The same set can already be in the library under another file name --
    // imported from elsewhere, or downloaded before. Keep the new archive and
    // drop the old entry rather than listing the beatmap twice.
    const int setId = onlineSetId(fSets.back());
    // Unsubmitted maps carry no online id; their difficulty hashes identify
    // them just as well.
    std::vector<std::string> hashes;
    if (setId <= 0) {
      for (const auto &info : fSets.back().fInfos) {
        if (!info.fMd5.empty()) {
          hashes.push_back(info.fMd5);
        }
      }
      std::ranges::sort(hashes);
    }
    const auto sameSet = [&](const Entry &entry) {
      if (setId > 0) {
        return onlineSetId(entry) == setId;
      }
      if (hashes.empty()) {
        return false;
      }
      std::vector<std::string> other;
      for (const auto &info : entry.fInfos) {
        if (!info.fMd5.empty()) {
          other.push_back(info.fMd5);
        }
      }
      std::ranges::sort(other);
      return other == hashes;
    };
    {
      for (std::size_t i = 0; i + 1 < fSets.size();) {
        if (!sameSet(fSets[i])) {
          ++i;
          continue;
        }
        const auto stale = fSets[i].fPath;
        fSets.erase(fSets.begin() + static_cast<std::ptrdiff_t>(i));
        if (!stale.empty() && stale != path &&
            stale.parent_path() == fMapsDir) {
          std::error_code ec;
          std::filesystem::remove(stale, ec);
          std::println(std::cerr, "[library] replaced {} with {}",
                       stale.filename().string(), path.filename().string());
        }
      }
    }
    fCache.save();
    fSync();
    const auto added = fSets.back().fPath;
    this->sortLibrary();
    this->rebuildVisible(criteria);
    if (select) {
      for (int i = 0; i < static_cast<int>(fSets.size()); ++i) {
        if (fSets[static_cast<std::size_t>(i)].fPath == added) {
          fSelSet = i;
          fSelDiff = 0;
          break;
        }
      }
    }
    return true;
  }

  // Copy an archive chosen outside the maps directory into the library and
  // index it. App owns the picker; Library owns where beatmaps are stored.
  bool importArchive(const std::filesystem::path &src,
                     const Criteria &criteria) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
      std::println(std::cerr, "[import] no such file: {}", src.string());
      return false;
    }
    const std::string ext = toLowerAscii(src.extension().string());
    if (ext != ".osz" && ext != ".zip") {
      std::println(std::cerr, "[import] not a beatmap archive: {}",
                   src.string());
      return false;
    }
    const auto dest = fMapsDir / src.filename();
    std::filesystem::copy_file(
        src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      std::println(std::cerr, "[import] copy failed: {}", ec.message());
      return false;
    }
    if (!this->addOszToLibrary(dest, true, criteria)) {
      return false;
    }
    std::println(std::cerr, "[import] added {}", dest.filename().string());
    return true;
  }

  // Remove an entry, its archive, thumbnail and cache record as one library
  // operation. Returns the filename used by the UI notification.
  [[nodiscard]] std::string deleteSet(std::size_t index) {
    if (index >= fSets.size()) {
      return {};
    }
    const auto path = fSets[index].fPath;
    const std::string name =
        path.empty() ? std::string{} : path.filename().string();
    fSets.erase(fSets.begin() + static_cast<std::ptrdiff_t>(index));
    fLoadedOrder.clear(); // deletion shifted every following index
    for (std::size_t i = 0; i < fSets.size(); ++i) {
      if (fSets[i].fLoaded) {
        fLoadedOrder.push_back(static_cast<int>(i));
      }
    }
    if (!path.empty()) {
      std::error_code ec;
      std::filesystem::remove(path, ec);
      std::filesystem::remove(this->thumbPathFor(path), ec);
      fCache.remove(name);
      fCache.save();
      fSync();
    }
    fSelSet = std::clamp(
        fSelSet, 0, std::max(0, static_cast<int>(fSets.size()) - 1));
    fSelDiff = 0;
    fDirty = true;
    return name;
  }

  void sortLibraryByStars() {
    std::string selected;
    if (fSelSet >= 0 && fSelSet < static_cast<int>(fSets.size())) {
      const auto &infos = this->infosFor(fSelSet);
      if (fSelDiff >= 0 && fSelDiff < static_cast<int>(infos.size())) {
        selected = infos[static_cast<std::size_t>(fSelDiff)].fFilename;
      }
    }
    for (std::size_t i = 0; i < fSets.size(); ++i) {
      auto &entry = fSets[i];
      std::ranges::stable_sort(entry.fInfos, {},
                               [this](const osu::BeatmapInfo &info) {
                                 return this->shownStars(info);
                               });
      // A set already in memory is put back in step with its list rather than
      // dropped: the set given on the command line has no path to load it
      // from again, and dropping it would lose it for the rest of the run.
      if (entry.fLoaded) {
        this->adoptCachedStars(static_cast<int>(i), *entry.fLoaded);
      }
    }
    if (!selected.empty() && fSelSet >= 0 &&
        fSelSet < static_cast<int>(fSets.size())) {
      const auto &infos = this->infosFor(fSelSet);
      for (std::size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].fFilename == selected) {
          fSelDiff = static_cast<int>(i);
          break;
        }
      }
    }
  }

  // Cover art for a set panel: decoded on the loader thread and cached with
  // the entry, so every visible panel gets one without stalling a frame.
  [[nodiscard]] skia::Sp<skia::SkImage> panelArt(int setIndex) {
    if (setIndex < 0 || setIndex >= static_cast<int>(fSets.size())) {
      return nullptr;
    }
    auto &entry = fSets[static_cast<std::size_t>(setIndex)];
    if (entry.fPanelArt || entry.fPanelArtTried) {
      return entry.fPanelArt;
    }
    const auto path = entry.fPath;
    if (path.empty()) {
      // The command-line set is already resident; decode it directly.
      entry.fPanelArtTried = true;
      if (auto set = entry.fLoaded) {
        entry.fPanelArt = this->decodeSetArt(*set);
      }
      return entry.fPanelArt;
    }

    // Thumbnails live on disk next to the metadata cache, so the archive is
    // only opened the first time a cover is needed. Everything after that is
    // a small PNG read.
    auto image = std::make_shared<skia::Sp<skia::SkImage>>();
    const auto thumb = this->thumbPathFor(path);
    fLoader->submit(
        static_cast<std::uint64_t>(setIndex) | (2ull << 32),
        [path, thumb, image, this] {
          if (std::filesystem::exists(thumb)) {
            *image = loadImage(thumb);
            if (*image) {
              return;
            }
          }
          const auto set = loadBeatmapSet(path, false); // artwork only
          auto full = this->decodeSetArt(set);
          if (!full) {
            return;
          }
          // Downscale once and keep the small copy; panels are ~680px wide.
          *image = this->makeThumbnail(full);
          if (*image) {
            // Raster image, so no GPU context is needed for the encode.
            auto png = skia::png::Encode(nullptr, (*image).get(),
                                         skia::png::Options{});
            if (png && !png->isEmpty()) {
              std::ofstream out(thumb, std::ios::binary);
              out.write(static_cast<const char *>(png->data()),
                        static_cast<std::streamsize>(png->size()));
            }
          }
        },
        [this, setIndex, path, image] {
          if (setIndex >= static_cast<int>(fSets.size()) ||
              fSets[static_cast<std::size_t>(setIndex)].fPath != path) {
            return;
          }
          auto &e = fSets[static_cast<std::size_t>(setIndex)];
          e.fPanelArt = *image;
          e.fPanelArtTried = true;
        });
    return nullptr;
  }

  [[nodiscard]] std::filesystem::path
  thumbPathFor(const std::filesystem::path &archive) const {
    return fThumbDir / (archive.stem().string() + ".png");
  }

  [[nodiscard]] static skia::Sp<skia::SkImage>
  makeThumbnail(const skia::Sp<skia::SkImage> &src) {
    if (!src) {
      return nullptr;
    }
    constexpr int kWidth = 512;
    const float scale =
        static_cast<float>(kWidth) / static_cast<float>(src->width());
    if (scale >= 1.0f) {
      return src;
    }
    const int h = std::max(
        1, static_cast<int>(static_cast<float>(src->height()) * scale));
    skia::SkBitmap bmp;
    if (!bmp.tryAllocPixels(
            skia::SkImageInfo::Make(kWidth, h, skia::kRGBA_8888_SkColorType,
                                    skia::kPremul_SkAlphaType))) {
      return src;
    }
    skia::SkCanvas canvas(bmp);
    skia::SkPaint paint;
    paint.setAntiAlias(true);
    canvas.drawImageRect(
        src.get(),
        skia::SkRect::MakeXYWH(0.0f, 0.0f, static_cast<float>(kWidth),
                               static_cast<float>(h)),
        skia::SkSamplingOptions(skia::SkFilterMode::kLinear), &paint);
    return skia::RasterFromBitmap(bmp);
  }

  [[nodiscard]] static skia::Sp<skia::SkImage>
  decodeSetArt(const osu::BeatmapSet &set) {
    for (const auto &info : set.fBeatmaps) {
      if (info.fMeta.fBackground.empty()) {
        continue;
      }
      const auto bytes = set.findFile(info.fMeta.fBackground);
      if (bytes.empty()) {
        continue;
      }
      if (auto img = loadImage(bytes)) {
        return img;
      }
    }
    return nullptr;
  }

  // How many resident sets to keep. Four is enough for the selection and its
  // neighbours; every one of them is an unpacked archive with decoded audio.
  static constexpr std::size_t kMaxLoadedSets = 4;
  // DifficultyRangeSlider's upper stop, past which it stops constraining.
  static constexpr double kRangeCap = 10.0;

  std::vector<Entry> fSets;
  std::vector<int> fVisible;    // indices into fSets passing the filter
  std::deque<int> fLoadedOrder; // LRU of entries holding a full set
  int fSelSet = 0;
  int fSelDiff = 0;
  MapCache fCache;
  Loader *fLoader = nullptr;
  std::filesystem::path fMapsDir;
  std::filesystem::path fThumbDir;
  Sort fSort = Sort::kTitle;
  double fRangeMin = 0.0;
  double fRangeMax = kRangeCap;
  bool fRanked = false; // which of the two ratings is the one being shown
  bool fDirty = true;   // the visible set has to be worked out again
  std::function<void()> fSync; // persist the maps directory (wasm)
};

} // namespace client::library
