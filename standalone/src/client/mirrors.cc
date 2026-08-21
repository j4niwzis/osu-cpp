export module client.mirrors;

import std;
import skia;
import skin;
import bjson;
import client.audio;
import client.http;
import client.listing;
import client.palette;

// The beatmap mirrors: searching them, paging through what they answer,
// fetching cover art and previews, and downloading a set.
//
// None of the three serves the whole filter set, so each says what it can do
// and the rest is applied to the page after it arrives. What the results look
// like is client.listing's; what happens to a downloaded archive is the
// client's. This is the part in between, and it is all network.
namespace http = client::http;
namespace listing = client::listing;
namespace palette = client::palette;

export namespace client::mirrors {

class Mirrors {
public:
  // What this cannot do for itself. All of them belong to the client: the
  // library it imports into, the notification corner, the menu music that
  // steps aside for a preview.
  struct Hooks {
    std::function<void(std::string, skia::SkColor)> fNotify;
    std::function<bool(long)> fOwned; // already in the library
    std::function<bool(const std::filesystem::path &)> fImport;
    std::function<void(int)> fEntryChanged; // one card's state moved
    std::function<void()> fSearchStarted;   // the listing scrolls back up
    std::function<float()> fGain;
    std::function<bool()> fMusicPlaying;
    std::function<void()> fDuck;
    std::function<void()> fRestore;
    std::function<void()> fSync; // persist the maps directory (wasm)
  };

  void configure(Hooks hooks, std::filesystem::path mapsDir) {
    fHooks = std::move(hooks);
    fMapsDir = std::move(mapsDir);
  }

  [[nodiscard]] const std::vector<listing::Entry> &results() const {
    return fFound;
  }
  [[nodiscard]] std::vector<listing::Entry> &results() { return fFound; }
  [[nodiscard]] const std::string &status() const { return fDownloadStatus; }
  [[nodiscard]] bool searching() const { return fSearchPending; }
  [[nodiscard]] bool previewPending() const { return fPreviewPending; }
  [[nodiscard]] long previewId() const { return fPreviewId; }
  [[nodiscard]] bool transferring() const { return !fTransfers.empty(); }
  [[nodiscard]] bool busy() const {
    return fSearchPending || fPreviewPending || !fTransfers.empty();
  }

  // Progress lives on the transfer handles; a card just reads a float, and
  // one whose number moved says so. That is what keeps a download from being
  // worth the whole screen on every frame of it.
  void pollProgress() {
    for (std::size_t i = 0; i < fFound.size(); ++i) {
      auto &e = fFound[i];
      const auto it = fTransfers.find(e.fSetId);
      if (it == fTransfers.end() || !it->second) {
        continue;
      }
      const float progress =
          it->second->fProgress.load(std::memory_order_relaxed);
      if (progress != e.fProgress) {
        e.fProgress = progress;
        fHooks.fEntryChanged(static_cast<int>(i));
      }
    }
  }

  // The clip ran out on its own; the button goes back to play.
  void pollPreview() {
    if (fPreviewId >= 0 && !fPreviewPending && !fPreview.playing()) {
      fPreviewId = -1;
      this->restoreMusic();
    }
  }

  void stopPreview() {
    fPreview.stop();
    fPreviewId = -1;
  }

  void forgetPreview() {
    fPreview.stop();
    fPreviewId = -1;
    fMusicDucked = false; // gameplay takes the track over anyway
  }

  void setVolume(float gain) { fPreview.setVolume(gain); }

  [[nodiscard]] bool empty() const { return fFound.empty(); }

  // The menu music is paused while a preview plays.
  [[nodiscard]] bool ducked() const { return fMusicDucked; }

  // Marks the results that are already installed, so they read as owned and
  // cannot be downloaded a second time.
  void markOwnedResults() {
    for (auto &e : fFound) {
      if (e.fSt == listing::Entry::St::kFetching) {
        continue;
      }
      e.fSt = fHooks.fOwned(e.fSetId) ? listing::Entry::St::kDone
                                      : listing::Entry::St::kIdle;
    }
  }

  // Mirrors, in the order they are tried. None of them serves the whole
  // filter set, so each says what it can do and the rest is applied to the
  // page after it arrives.
  //
  // Verified by hand against each API:
  //   nerinyan   q, m, s (name), sort, e (video/storyboard), nsfw, p/ps
  //   osu.direct q, mode, status (id), sort (<field>:<dir>), amount/offset
  //   mino       query, mode, status (id), limit/offset
  enum class MirrorStyle : std::uint8_t { kNerinyan, kOsuDirect, kMino };
  struct Mirror {
    const char *fName;
    MirrorStyle fStyle;
    const char *fDownload; // {} takes the set id
  };
  static constexpr std::array<Mirror, 3> kMirrors{
      Mirror{"nerinyan", MirrorStyle::kNerinyan,
             "https://api.nerinyan.moe/d/{}"},
      Mirror{"osu.direct", MirrorStyle::kOsuDirect,
             "https://osu.direct/api/d/{}"},
      Mirror{"mino", MirrorStyle::kMino, "https://catboy.best/d/{}"}};
  std::size_t fMirror = 0;
  static constexpr int kSearchPageSize = 50;

  // "2020-04-24T18:08:56+02:00" -> a number that sorts by date. Only the
  // ordering matters, so the digits are simply concatenated.
  [[nodiscard]] static std::int64_t dateStamp(std::string_view iso) {
    std::int64_t v = 0;
    int digits = 0;
    for (const char c : iso) {
      if (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        if (++digits == 14) {
          break;
        }
      }
    }
    return v;
  }

  // osu! status ids, which two of the three mirrors take directly.
  [[nodiscard]] static int statusId(listing::Category c) {
    using Category = listing::Category;
    switch (c) {
    case Category::kRanked:
      return 1;
    case Category::kQualified:
      return 3;
    case Category::kLoved:
      return 4;
    case Category::kPending:
      return 0;
    case Category::kWip:
      return -1;
    case Category::kGraveyard:
      return -2;
    default:
      return 100; // no server-side status
    }
  }

  [[nodiscard]] static const char *statusName(listing::Category c) {
    using Category = listing::Category;
    switch (c) {
    case Category::kRanked:
      return "ranked";
    case Category::kQualified:
      return "qualified";
    case Category::kLoved:
      return "loved";
    case Category::kPending:
      return "pending";
    case Category::kWip:
      return "wip";
    case Category::kGraveyard:
      return "graveyard";
    case Category::kLeaderboard:
      return "leaderboard";
    default:
      return "";
    }
  }

  // lazer's criteria mapped onto what each mirror calls them. An empty string
  // means the mirror cannot sort by it and the listing does it itself.
  [[nodiscard]] static std::string
  sortParam(listing::Sort sort, bool descending, MirrorStyle style) {
    using Sort = listing::Sort;
    const char *field = nullptr;
    switch (sort) {
    case Sort::kTitle:
      field = "title";
      break;
    case Sort::kArtist:
      field = "artist";
      break;
    case Sort::kRanked:
      field = style == MirrorStyle::kOsuDirect ? "ranked_date" : "ranked";
      break;
    case Sort::kUpdated:
      field = style == MirrorStyle::kOsuDirect ? "last_updated" : "updated";
      break;
    case Sort::kPlays:
      field = style == MirrorStyle::kOsuDirect ? "play_count" : "plays";
      break;
    case Sort::kFavourites:
      field =
          style == MirrorStyle::kOsuDirect ? "favourite_count" : "favourites";
      break;
    default:
      return {}; // difficulty, rating, relevance, nominations
    }
    const char *dir = descending ? "desc" : "asc";
    return style == MirrorStyle::kOsuDirect ? std::format("{}:{}", field, dir)
                                            : std::format("{}_{}", field, dir);
  }

  [[nodiscard]] std::string searchUrl(int offset) const {
    const auto &f = fFilters;
    const auto &mirror = kMirrors[fMirror];
    const std::string q = http::urlEncode(f.fQuery);
    const std::string sort = sortParam(f.fSort, f.fDescending, mirror.fStyle);
    const int status = statusId(f.fCategory);
    std::string url;
    switch (mirror.fStyle) {
    case MirrorStyle::kNerinyan: {
      url =
          std::format("https://api.nerinyan.moe/search?q={}&m={}&ps={}&p={}", q,
                      f.fRuleset, kSearchPageSize, offset / kSearchPageSize);
      if (const char *name = statusName(f.fCategory); *name != '\0') {
        url += std::format("&s={}", name);
      }
      // Extra and explicit content are server-side here, and only here.
      std::string extra;
      if (f.fExtra[0]) {
        extra += "video";
      }
      if (f.fExtra[1]) {
        extra += extra.empty() ? "storyboard" : ".storyboard";
      }
      if (!extra.empty()) {
        url += "&e=" + extra;
      }
      url += f.fExplicit == listing::Explicit::kShow ? "&nsfw=true"
                                                     : "&nsfw=false";
      break;
    }
    case MirrorStyle::kOsuDirect:
      url = std::format(
          "https://osu.direct/api/v2/search?q={}&mode={}&amount={}&offset={}",
          q, f.fRuleset, kSearchPageSize, offset);
      if (status != 100) {
        url += std::format("&status={}", status);
      }
      break;
    case MirrorStyle::kMino:
      url = std::format("https://catboy.best/api/v2/"
                        "search?query={}&mode={}&limit={}&offset={}",
                        q, f.fRuleset, kSearchPageSize, offset);
      if (status != 100) {
        url += std::format("&status={}", status);
      }
      break;
    }
    if (!sort.empty()) {
      url += "&sort=" + sort;
    }
    return url;
  }

  // A fresh query. The criteria are kept: paging asks the same mirror the
  // same question with a larger offset.
  void startSearch(const listing::Filters &filters) {
    fFilters = filters;
    fSearchOffset = 0;
    fMoreAvailable = true;
    // Pages already in flight belong to the previous query; without this they
    // arrive afterwards and are appended to the new results.
    ++fSearchGeneration;
    fSearchPending = false;
    fHooks.fSearchStarted();
    fFound.clear();
    this->fetchPage();
  }

  // The next page of the current search, appended to what is already there.
  void fetchPage() {
    if (fSearchPending || !fMoreAvailable) {
      return;
    }
    fSearchPending = true;
    fDownloadStatus = fSearchOffset == 0 ? "Searching..." : "Loading more...";
    const int offset = fSearchOffset;
    const std::uint32_t generation = fSearchGeneration;
    auto handle = std::make_shared<http::Handle>();
    http::get(this->searchUrl(offset), std::move(handle),
              [this, offset, generation](http::Response r) {
                if (generation != fSearchGeneration) {
                  return; // the query moved on while this was in flight
                }
                this->onSearchDone(offset, std::move(r));
              });
  }

  void onSearchDone(int offset, http::Response r) {
    fSearchPending = false;
    if (!r.fOk) {
      // A mirror that will not answer is replaced by the next one; the same
      // page is then asked of it.
      if (fMirror + 1 < kMirrors.size()) {
        ++fMirror;
        std::println(std::cerr, "[listing] {} failed ({}), falling back to {}",
                     kMirrors[fMirror - 1].fName, r.fError,
                     kMirrors[fMirror].fName);
        this->fetchPage();
        return;
      }
      fDownloadStatus = "Search failed: " + r.fError;
      fMoreAvailable = false; // stop the scroll from asking again every frame
      fHooks.fNotify("search failed: " + r.fError,
                     skia::colorSetARGB(255, 255, 110, 110));
      return;
    }
    const auto parsed = bjson::tryParse(r.fBody);
    if (!parsed) {
      fDownloadStatus = "Search failed: malformed JSON";
      fMoreAvailable = false;
      return;
    }
    const bjson::array *arr = parsed->if_array();
    if (arr == nullptr) {
      // Some mirrors wrap the array in an envelope object.
      if (const bjson::object *obj = parsed->if_object()) {
        if (const bjson::value *data = obj->if_contains("data")) {
          arr = data->if_array();
        }
      }
    }
    if (arr == nullptr) {
      fDownloadStatus = "Search failed: unexpected response shape";
      fMoreAvailable = false;
      return;
    }

    const auto getNum = [](const bjson::object &o,
                           std::string_view key) -> double {
      if (const bjson::value *v = o.if_contains(key)) {
        if (v->is_number()) {
          return v->to_number<double>();
        }
      }
      return 0.0;
    };
    const auto getBool = [](const bjson::object &o,
                            std::string_view key) -> bool {
      if (const bjson::value *v = o.if_contains(key)) {
        if (const bool *b = v->if_bool()) {
          return *b;
        }
      }
      return false;
    };
    const auto getStr = [](const bjson::object &o,
                           std::string_view key) -> std::string {
      if (const bjson::value *v = o.if_contains(key)) {
        if (const bjson::string *str = v->if_string()) {
          return std::string(str->begin(), str->end());
        }
      }
      return {};
    };

    if (offset == 0) {
      fFound.clear();
    }
    const std::size_t before = fFound.size();
    for (const auto &e : *arr) {
      const bjson::object *o = e.if_object();
      if (o == nullptr) {
        continue;
      }
      listing::Entry d;
      const bjson::value *id = o->if_contains("id");
      if (id == nullptr || !id->is_int64()) {
        continue;
      }
      d.fSetId = static_cast<long>(id->as_int64());
      d.fTitle = getStr(*o, "title");
      d.fTitleUnicode = getStr(*o, "title_unicode");
      d.fArtist = getStr(*o, "artist");
      d.fArtistUnicode = getStr(*o, "artist_unicode");
      d.fCreator = getStr(*o, "creator");
      d.fStatus = getStr(*o, "status");
      d.fUpdated = getStr(*o, "last_updated").substr(0, 10);
      d.fUpdatedDate = dateStamp(getStr(*o, "last_updated"));
      d.fRankedDate = dateStamp(getStr(*o, "ranked_date"));
      d.fSource = getStr(*o, "source");
      if (const bjson::value *covers = o->if_contains("covers")) {
        if (const bjson::object *co = covers->if_object()) {
          // card@2x for the cards, cover@2x for the set page: the sizes
          // lazer's BeatmapSetCoverType picks for each.
          d.fCardCover = getStr(*co, "card@2x");
          if (d.fCardCover.empty()) {
            d.fCardCover = getStr(*co, "card");
          }
          d.fFullCover = getStr(*co, "cover@2x");
          if (d.fFullCover.empty()) {
            d.fFullCover = getStr(*co, "cover");
          }
        }
      }
      if (const bjson::value *ratings = o->if_contains("ratings")) {
        if (const bjson::array *ra = ratings->if_array()) {
          for (const auto &v : *ra) {
            d.fRatings.push_back(
                v.is_number() ? static_cast<int>(v.to_number<double>()) : 0);
          }
        }
      }
      d.fTags = getStr(*o, "tags");
      d.fBpm = getNum(*o, "bpm");
      d.fRating = getNum(*o, "rating");
      d.fPlayCount = static_cast<long>(getNum(*o, "play_count"));
      d.fFavouriteCount = static_cast<long>(getNum(*o, "favourite_count"));
      d.fVideo = getBool(*o, "video");
      d.fStoryboard = getBool(*o, "storyboard");
      d.fNsfw = getBool(*o, "nsfw");
      d.fSpotlight = getBool(*o, "spotlight");
      if (const bjson::value *track = o->if_contains("track_id")) {
        d.fFeatured = !track->is_null();
      }
      if (const bjson::value *g = o->if_contains("genre")) {
        if (const bjson::object *go = g->if_object()) {
          d.fGenre = static_cast<int>(getNum(*go, "id"));
        }
      }
      if (const bjson::value *l = o->if_contains("language")) {
        if (const bjson::object *lo = l->if_object()) {
          d.fLanguage = static_cast<int>(getNum(*lo, "id"));
        }
      }
      if (const bjson::value *bms = o->if_contains("beatmaps")) {
        if (const bjson::array *ba = bms->if_array()) {
          for (const auto &bm : *ba) {
            const bjson::object *bo = bm.if_object();
            if (bo == nullptr) {
              continue;
            }
            const bjson::value *sr = bo->if_contains("difficulty_rating");
            if (sr == nullptr || !sr->is_number()) {
              continue;
            }
            const auto v = static_cast<float>(sr->to_number<double>());
            listing::Entry::Difficulty diff;
            diff.fStars = v;
            diff.fVersion = getStr(*bo, "version");
            diff.fMode = static_cast<int>(getNum(*bo, "mode_int"));
            diff.fLengthMs = getNum(*bo, "total_length") * 1000.0;
            diff.fCs = getNum(*bo, "cs");
            diff.fAr = getNum(*bo, "ar");
            diff.fOd = getNum(*bo, "accuracy");
            diff.fHp = getNum(*bo, "drain");
            diff.fMaxCombo = static_cast<int>(getNum(*bo, "max_combo"));
            d.fDiffs.push_back(std::move(diff));
            if (d.fDiffCount == 0) {
              d.fStarsMin = d.fStarsMax = v;
            } else {
              d.fStarsMin = std::min(d.fStarsMin, v);
              d.fStarsMax = std::max(d.fStarsMax, v);
            }
            ++d.fDiffCount;
          }
          std::ranges::sort(d.fDiffs, {}, &listing::Entry::Difficulty::fStars);
        }
      }
      fFound.push_back(std::move(d));
    }
    this->markOwnedResults();
    const std::size_t added = fFound.size() - before;
    fSearchOffset = offset + kSearchPageSize;
    fMoreAvailable = added >= static_cast<std::size_t>(kSearchPageSize) / 2;
    fDownloadStatus = std::format("{} results", fFound.size());
  }

  // PlayButton on the card thumbnail: fetches the 10-second preview osu!
  // serves for every set and plays it on its own source.
  void togglePreview(std::size_t idx) {
    if (idx >= fFound.size()) {
      std::println(std::cerr, "[preview] no entry at {}", idx);
      return;
    }
    const long id = fFound[idx].fSetId;
    if (fPreviewId == id) {
      fPreview.stop();
      fPreviewId = -1;
      this->restoreMusic();
      return;
    }
    fPreview.stop();
    fPreviewId = -1;
    ++fPreviewGeneration; // whatever is in flight is no longer wanted
    const std::uint32_t generation = fPreviewGeneration;
    fPreviewPending = true;
    // A preview replaces the menu music while it runs, as lazer's does;
    // playing both at once doubles the level and the limiter clamps it.
    if (!fMusicDucked && fHooks.fMusicPlaying()) {
      fHooks.fDuck();
      fMusicDucked = true;
    }
    const std::string url = std::format("https://b.ppy.sh/preview/{}.mp3", id);
    std::println(std::cerr, "[preview] fetching {}", url);
    auto handle = std::make_shared<http::Handle>();
    http::get(url, std::move(handle), [this, id, generation](http::Response r) {
      if (generation != fPreviewGeneration) {
        return; // superseded by a later click
      }
      fPreviewPending = false;
      if (!r.fOk || r.fBody.size() < 1024) {
        std::println(std::cerr, "[preview] fetch failed: {} ({} bytes) {}",
                     r.fStatus, r.fBody.size(), r.fError);
        fHooks.fNotify("preview unavailable",
                       skia::colorSetARGB(255, 255, 110, 110));
        return;
      }
      const std::vector<std::uint8_t> bytes(r.fBody.begin(), r.fBody.end());
      if (!fPreview.load(bytes, ".mp3")) {
        std::println(std::cerr, "[preview] decode failed ({} bytes)",
                     bytes.size());
        return;
      }
      fPreview.setLooping(false);
      fPreview.setVolume(fHooks.fGain());
      fPreview.play();
      fPreviewId = id;
      std::println(std::cerr, "[preview] playing {} ({:.1f}s)", id,
                   fPreview.durationSec());
    });
  }

  void restoreMusic() {
    if (fMusicDucked) {
      fMusicDucked = false;
      fHooks.fRestore();
    }
  }

  // How far through the preview is, for the ring around the play button.
  [[nodiscard]] float previewProgress() const {
    const double duration = fPreview.durationSec();
    if (fPreviewId < 0 || duration <= 0.0) {
      return 0.0f;
    }
    return static_cast<float>(fPreview.positionSec() / duration);
  }

  // The page shows covers.cover@2x (1920x360), not the card crop.
  void requestPageCover(std::size_t idx) {
    if (idx >= fFound.size()) {
      return;
    }
    auto &d = fFound[idx];
    if (d.fPageCoverSt != listing::Entry::Cover::kNone) {
      return;
    }
    d.fPageCoverSt = listing::Entry::Cover::kFetching;
    const long id = d.fSetId;
    const std::string url =
        d.fFullCover.empty()
            ? std::format(
                  "https://assets.ppy.sh/beatmaps/{}/covers/cover@2x.jpg", id)
            : d.fFullCover;
    auto handle = std::make_shared<http::Handle>();
    http::get(url, std::move(handle), [this, id](http::Response r) {
      for (auto &e : fFound) {
        if (e.fSetId != id) {
          continue;
        }
        if (r.fOk && r.fBody.size() > 256) {
          const std::vector<std::uint8_t> bytes(r.fBody.begin(), r.fBody.end());
          e.fPageCover = loadImage(bytes);
        }
        e.fPageCoverSt = e.fPageCover ? listing::Entry::Cover::kReady
                                      : listing::Entry::Cover::kFailed;
        break;
      }
    });
  }

  void requestThumb(std::size_t idx) {
    if (idx >= fFound.size()) {
      return;
    }
    auto &d = fFound[idx];
    if (d.fThumbSt != listing::Entry::Thumb::kNone) {
      return;
    }
    int inflight = 0;
    for (const auto &e : fFound) {
      if (e.fThumbSt == listing::Entry::Thumb::kFetching) {
        ++inflight;
      }
    }
    if (inflight >= 4) {
      return; // retry on a later frame
    }
    d.fThumbSt = listing::Entry::Thumb::kFetching;
    const long id = d.fSetId;
    const std::string url =
        d.fCardCover.empty()
            ? std::format(
                  "https://assets.ppy.sh/beatmaps/{}/covers/card@2x.jpg", id)
            : d.fCardCover;
    auto handle = std::make_shared<http::Handle>();
    http::get(url, std::move(handle), [this, id](http::Response r) {
      for (std::size_t i = 0; i < fFound.size(); ++i) {
        auto &e = fFound[i];
        if (e.fSetId != id) {
          continue;
        }
        if (r.fOk && r.fBody.size() > 256) {
          std::vector<std::uint8_t> bytes(r.fBody.begin(), r.fBody.end());
          e.fThumb = loadImage(bytes);
          e.fThumbSt = e.fThumb ? listing::Entry::Thumb::kReady
                                : listing::Entry::Thumb::kFailed;
        } else {
          e.fThumbSt = listing::Entry::Thumb::kFailed;
        }
        // The card draws the image out of the entry, so it cannot notice
        // this by itself: one card is marked, rather than the screen.
        fHooks.fEntryChanged(static_cast<int>(i));
        break;
      }
    });
  }

  [[nodiscard]] std::size_t indexOfSet(long id) const {
    for (std::size_t i = 0; i < fFound.size(); ++i) {
      if (fFound[i].fSetId == id) {
        return i;
      }
    }
    return fFound.size();
  }

  void startDownloadForSet(long id) {
    this->startDownload(this->indexOfSet(id));
  }

  void togglePreviewForSet(long id) {
    this->togglePreview(this->indexOfSet(id));
  }

  void startDownload(std::size_t idx) {
    if (idx >= fFound.size()) {
      return;
    }
    auto &d = fFound[idx];
    if (d.fSt == listing::Entry::St::kFetching) {
      return;
    }
    if (fHooks.fOwned(d.fSetId)) {
      d.fSt = listing::Entry::St::kDone;
      fHooks.fNotify("already in the library", palette::kAccent2);
      return;
    }
    if (d.fSt == listing::Entry::St::kDone) {
      return;
    }
    d.fSt = listing::Entry::St::kFetching;
    const long id = d.fSetId;
    auto handle = std::make_shared<http::Handle>();
    fTransfers[id] = handle;
    d.fProgress = 0.0f;
    const std::string url =
        std::vformat(kMirrors[fMirror].fDownload, std::make_format_args(id));
    http::get(url, std::move(handle), [this, id](http::Response r) {
      this->onDownloadDone(id, std::move(r));
    });
  }

  void onDownloadDone(long id, http::Response r) {
    fTransfers.erase(id);
    listing::Entry *d = nullptr;
    for (auto &e : fFound) {
      if (e.fSetId == id) {
        d = &e;
        break;
      }
    }
    if (!r.fOk || r.fBody.size() < 1024) {
      if (d != nullptr) {
        d->fSt = listing::Entry::St::kError;
      }
      fDownloadStatus =
          "Download failed: " + (r.fError.empty() ? "empty file" : r.fError);
      fHooks.fNotify(std::format("download failed: {}",
                                 r.fError.empty() ? "empty file" : r.fError),
                     skia::colorSetARGB(255, 255, 110, 110));
      return;
    }
    const auto path = fMapsDir / std::format("{}.osz", id);
    {
      std::ofstream out(path, std::ios::binary);
      out.write(r.fBody.data(), static_cast<std::streamsize>(r.fBody.size()));
    }
    fHooks.fSync();
    if (fHooks.fImport(path)) {
      if (d != nullptr) {
        d->fSt = listing::Entry::St::kDone;
      }
      fDownloadStatus = "Added to library: " +
                        (d != nullptr ? d->fTitle : std::to_string(id));
      fHooks.fNotify(std::format("imported {}",
                                 d != nullptr ? d->fTitle : std::to_string(id)),
                     skia::colorSetARGB(255, 120, 220, 120));
      this->markOwnedResults();
    } else if (d != nullptr) {
      d->fSt = listing::Entry::St::kError;
    }
  }

private:
  listing::Filters fFilters;
  std::vector<listing::Entry> fFound;
  std::map<long, std::shared_ptr<http::Handle>> fTransfers;
  bool fSearchPending = false;
  std::size_t fMirror = 0;
  int fSearchOffset = 0; // how much of the current search is loaded
  std::uint32_t fSearchGeneration = 0; // older queries are dropped
  bool fMoreAvailable = true; // a full page came back, so ask for the next
  std::string fDownloadStatus;
  // Track previews come from osu!'s own preview endpoint and play on their
  // own source, so the menu music is untouched.
  client::AudioPlayer fPreview;
  long fPreviewId = -1;
  bool fPreviewPending = false;
  std::uint32_t fPreviewGeneration = 0; // stale fetches must not start playing
  bool fMusicDucked = false;            // menu music paused for a preview
  std::filesystem::path fMapsDir;
  Hooks fHooks;
};

} // namespace client::mirrors
