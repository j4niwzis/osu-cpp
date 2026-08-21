export module osu.replay_file;

import std;
import lzma;
import osu.types;
import osu.rules;
import osu.engine;

export namespace osu {

// LegacyScoreEncoder.LATEST_VERSION and FIRST_LAZER_VERSION. The version field
// is what lazer reads to decide whether a replay came from stable: anything
// below the first lazer version has the Classic mod appended on import, which
// changes slider scoring and hit windows, so a replay written here claims a
// lazer version and is played back by lazer as it was recorded.
inline constexpr std::int32_t kLazerScoreVersion = 30000019;
inline constexpr std::int32_t kFirstLazerScoreVersion = 30000000;

// The per-result counts lazer keeps, which ride along at the end of the file
// as JSON so that an import gets the real accuracy and rank rather than the
// four legacy counts. Empty means the block is not written.
struct ReplayStatistics {
  int fGreat = 0;
  int fOk = 0;
  int fMeh = 0;
  int fMiss = 0;
  int fLargeTickHit = 0;
  int fLargeTickMiss = 0;
  int fSliderTailHit = 0;
  int fSmallBonus = 0;
  int fLargeBonus = 0;
  int fMaxGreat = 0;
  int fMaxLargeTick = 0;
  int fMaxSliderTail = 0;
  int fMaxSmallBonus = 0;
  int fMaxLargeBonus = 0;
  std::string fRank;
  std::int64_t fTotalScore = 0;
  bool fPresent = false;
};

// The .osr header carries the score alongside the input events; these are
// those fields, in the order the format stores them.
struct ReplayScore {
  std::uint16_t f300 = 0;
  std::uint16_t f100 = 0;
  std::uint16_t f50 = 0;
  std::uint16_t fGeki = 0;
  std::uint16_t fKatu = 0;
  std::uint16_t fMiss = 0;
  std::int32_t fTotalScore = 0;
  std::uint16_t fMaxCombo = 0;
  bool fPerfect = false;

  [[nodiscard]] int totalHits() const noexcept {
    return f300 + f100 + f50 + fMiss;
  }

  [[nodiscard]] double accuracy() const noexcept {
    const int total = totalHits();
    if (total == 0) {
      return 1.0;
    }
    return (300.0 * f300 + 100.0 * f100 + 50.0 * f50) / (300.0 * total);
  }
};

// Everything the .osr carries before the compressed input events. Reading it
// costs a few hundred bytes and no decompression, which is what indexing a
// replay directory needs.
struct ReplayHeader {
  std::string fBeatmapMd5;
  std::string fPlayerName;
  ModSet fMods = mod::kNone;
  ReplayScore fScore;
  std::int32_t fVersion = 0; // the game version field, which is a rules stamp
};

struct ReplayData {
  std::vector<InputEvent> fEvents;
  // No seed frame: one of the files this client wrote before the format was
  // fixed. Absolute frame times, .xz where LZMA belongs, and the old scoring
  // model.
  bool fLegacyFormat = false;
  std::string fBeatmapMd5;
  std::string fPlayerName;
  ModSet fMods = mod::kNone;
  ReplayScore fScore;
  std::int32_t fVersion = 0;
};

[[nodiscard]] inline std::string
md5HashString(std::span<const std::uint8_t> data);

namespace detail {
namespace md5 {
using Word = std::uint32_t;

inline constexpr Word F(Word x, Word y, Word z) { return (x & y) | (~x & z); }
inline constexpr Word G(Word x, Word y, Word z) { return (x & z) | (y & ~z); }
inline constexpr Word H(Word x, Word y, Word z) { return x ^ y ^ z; }
inline constexpr Word I(Word x, Word y, Word z) { return y ^ (x | ~z); }

inline constexpr Word rotl(Word v, int s) { return (v << s) | (v >> (32 - s)); }

inline constexpr std::array<Word, 64> kK = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

inline constexpr int kS[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

struct State {
  Word a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
};

inline void transform(State &st, std::span<const std::uint8_t, 64> block) {
  Word m[16];
  for (int i = 0; i < 16; ++i) {
    m[i] = (static_cast<Word>(block[i * 4 + 3]) << 24) |
           (static_cast<Word>(block[i * 4 + 2]) << 16) |
           (static_cast<Word>(block[i * 4 + 1]) << 8) |
           static_cast<Word>(block[i * 4]);
  }
  Word a = st.a, b = st.b, c = st.c, d = st.d;
  for (int i = 0; i < 64; ++i) {
    Word f, g;
    if (i < 16)
      f = F(b, c, d), g = i;
    else if (i < 32)
      f = G(b, c, d), g = (5 * i + 1) % 16;
    else if (i < 48)
      f = H(b, c, d), g = (3 * i + 5) % 16;
    else
      f = I(b, c, d), g = (7 * i) % 16;
    f += a + kK[i] + m[g];
    a = d, d = c, c = b, b += rotl(f, kS[i]);
  }
  st.a += a, st.b += b, st.c += c, st.d += d;
}

inline std::array<std::uint8_t, 16> hash(std::span<const std::uint8_t> input) {
  State st;
  std::uint64_t bits = static_cast<std::uint64_t>(input.size()) * 8;
  std::vector<std::uint8_t> buf(input.begin(), input.end());
  buf.push_back(0x80);
  while ((buf.size() % 64) != 56)
    buf.push_back(0);
  for (int i = 0; i < 8; ++i)
    buf.push_back(static_cast<std::uint8_t>(bits >> (i * 8)));
  for (std::size_t off = 0; off < buf.size(); off += 64) {
    std::array<std::uint8_t, 64> block;
    std::copy_n(buf.data() + off, 64, block.begin());
    transform(st, block);
  }
  std::array<std::uint8_t, 16> out;
  auto put = [&](Word v, int base) {
    for (int i = 0; i < 4; ++i)
      out[base + i] = static_cast<std::uint8_t>(v >> (i * 8));
  };
  put(st.a, 0), put(st.b, 4), put(st.c, 8), put(st.d, 12);
  return out;
}

inline std::string hexString(std::span<const std::uint8_t, 16> digest) {
  std::string s;
  s.reserve(32);
  for (std::uint8_t b : digest) {
    s += "0123456789abcdef"[b >> 4];
    s += "0123456789abcdef"[b & 0xf];
  }
  return s;
}
} // namespace md5

inline void writeUleb(std::vector<std::uint8_t> &out, std::uint64_t val) {
  do {
    std::uint8_t b = val & 0x7f;
    val >>= 7;
    if (val)
      b |= 0x80;
    out.push_back(b);
  } while (val);
}
inline std::uint64_t readUleb(std::span<const std::uint8_t> &data) {
  std::uint64_t val = 0;
  int shift = 0;
  while (!data.empty()) {
    std::uint8_t b = data[0];
    data = data.subspan(1);
    val |= (static_cast<std::uint64_t>(b & 0x7f) << shift);
    if (!(b & 0x80))
      return val;
    shift += 7;
  }
  return val;
}

inline void writeByte(std::vector<std::uint8_t> &out, std::uint8_t v) {
  out.push_back(v);
}
inline void writeShort(std::vector<std::uint8_t> &out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}
inline void writeInt(std::vector<std::uint8_t> &out, std::int32_t v) {
  const auto u = static_cast<std::uint32_t>(v);
  for (int i = 0; i < 4; ++i)
    out.push_back(static_cast<std::uint8_t>(u >> (i * 8)));
}
inline void writeLong(std::vector<std::uint8_t> &out, std::int64_t v) {
  const auto u = static_cast<std::uint64_t>(v);
  for (int i = 0; i < 8; ++i)
    out.push_back(static_cast<std::uint8_t>(u >> (i * 8)));
}
inline void writeString(std::vector<std::uint8_t> &out, std::string_view str) {
  out.push_back(0x0b);
  writeUleb(out, str.size());
  out.insert(out.end(), str.begin(), str.end());
}

inline std::uint8_t readByte(std::span<const std::uint8_t> &data) {
  std::uint8_t v = data[0];
  data = data.subspan(1);
  return v;
}
inline std::uint16_t readShort(std::span<const std::uint8_t> &data) {
  std::uint16_t v = static_cast<std::uint16_t>(data[0]) |
                    (static_cast<std::uint16_t>(data[1]) << 8);
  data = data.subspan(2);
  return v;
}
inline std::int32_t readInt(std::span<const std::uint8_t> &data) {
  std::uint32_t v = static_cast<std::uint32_t>(data[0]) |
                    (static_cast<std::uint32_t>(data[1]) << 8) |
                    (static_cast<std::uint32_t>(data[2]) << 16) |
                    (static_cast<std::uint32_t>(data[3]) << 24);
  data = data.subspan(4);
  return static_cast<std::int32_t>(v);
}
inline std::int64_t readLong(std::span<const std::uint8_t> &data) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= (static_cast<std::uint64_t>(data[i]) << (i * 8));
  data = data.subspan(8);
  return static_cast<std::int64_t>(v);
}
inline std::string readString(std::span<const std::uint8_t> &data) {
  std::uint8_t present = readByte(data);
  if (present == 0x00)
    return {};
  std::uint64_t len = readUleb(data);
  std::string s(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(len));
  data = data.subspan(static_cast<std::size_t>(len));
  return s;
}

// The replay payload is LZMA1 in the alone format -- five bytes of
// properties, eight of uncompressed size, then the stream -- because that is
// what an .osr holds and what every other reader of one expects. This wrote
// .xz here before, which liblzma was happy to produce and nothing else in the
// world would open.
inline std::vector<std::uint8_t>
lzmaCompress(std::span<const std::uint8_t> input) {
  std::vector<std::uint8_t> out(input.size() + input.size() / 3 + 128);
  lzma::lzma_stream strm = lzma::kStreamInit;
  lzma::lzma_options_lzma options{};
  if (lzma::lzma_lzma_preset(&options, lzma::kPresetDefault))
    return {};
  if (lzma::lzma_alone_encoder(&strm, &options) != lzma::kOk)
    return {};
  strm.next_in = input.data();
  strm.avail_in = input.size();
  strm.next_out = out.data();
  strm.avail_out = out.size();
  lzma::lzma_ret ret = lzma::lzma_code(&strm, lzma::kFinish);
  if (ret != lzma::kStreamEnd) {
    lzma::lzma_end(&strm);
    return {};
  }
  out.resize(strm.total_out);
  lzma::lzma_end(&strm);
  // liblzma streams, so it writes "size unknown" and an end marker. Every
  // decoder copes with that, but the ones that read a length and stop cope
  // better, and the length is known here.
  if (out.size() >= 13) {
    auto size = static_cast<std::uint64_t>(input.size());
    for (int i = 0; i < 8; ++i) {
      out[5 + static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>((size >> (8 * i)) & 0xFF);
    }
  }
  return out;
}

inline std::vector<std::uint8_t>
lzmaDecompress(std::span<const std::uint8_t> input) {
  std::vector<std::uint8_t> out(1024 * 1024);
  lzma::lzma_stream strm = lzma::kStreamInit;
  // Auto rather than alone: real replays and the ones this wrote before the
  // format was fixed both open.
  if (lzma::lzma_auto_decoder(
          &strm, std::numeric_limits<std::uint64_t>::max(), 0) != lzma::kOk)
    return {};
  strm.next_in = input.data();
  strm.avail_in = input.size();
  strm.next_out = out.data();
  strm.avail_out = out.size();
  while (true) {
    lzma::lzma_ret ret = lzma::lzma_code(&strm, lzma::kFinish);
    if (ret == lzma::kStreamEnd)
      break;
    if (ret != lzma::kOk) {
      lzma::lzma_end(&strm);
      return {};
    }
    if (strm.avail_out == 0) {
      std::size_t sz = out.size();
      out.resize(sz * 2);
      strm.next_out = out.data() + sz;
      strm.avail_out = sz;
    }
  }
  out.resize(strm.total_out);
  lzma::lzma_end(&strm);
  return out;
}

// Frames are "since the last one", not "since the start": the first field of
// an .osr frame is a delta. Writing absolute times here produced files that
// only this reader could make sense of, since anything else accumulates them.
//
// The last frame is the format's odd one out -- time -12345 carrying the
// score's random seed in the key field -- and readers skip it by its negative
// time.
inline std::vector<std::uint8_t>
encodeReplayData(std::span<const InputEvent> events, std::int32_t seed = 0) {
  std::string s;
  int keys = 0;
  std::int64_t last = 0;
  for (const auto &ev : events) {
    if (ev.fAction == InputAction::kPress)
      keys |= 5;
    else if (ev.fAction == InputAction::kRelease)
      keys &= ~5;
    const auto now = static_cast<std::int64_t>(ev.fTime);
    s += std::format("{}|{:.7f}|{:.7f}|{},", now - last, ev.fPos.fX,
                     ev.fPos.fY, keys);
    last = now;
  }
  s += std::format("-12345|0|0|{},", seed);
  if (!s.empty())
    s.pop_back();
  return std::vector<std::uint8_t>(s.begin(), s.end());
}

// The seed frame is what tells a replay's vintage. Everything osu! has
// written since 2013 ends with one and carries frame times as deltas; the
// files this client wrote before the format was fixed have no seed frame,
// carry absolute times, and were scored by the old model. One frame answers
// all three questions, so nothing else is consulted.
[[nodiscard]] inline bool hasSeedFrame(std::string_view replayStr) noexcept {
  return replayStr.find("-12345|") != std::string_view::npos;
}

inline std::vector<InputEvent> decodeReplayData(std::string_view replayStr) {
  std::vector<InputEvent> events;
  int keys = 0;
  std::int64_t now = 0;
  std::size_t pos = 0;
  const bool deltas = hasSeedFrame(replayStr);
  while (pos < replayStr.size()) {
    std::size_t s1 = replayStr.find('|', pos);
    if (s1 == std::string::npos)
      break;
    std::size_t s2 = replayStr.find('|', s1 + 1);
    if (s2 == std::string::npos)
      break;
    std::size_t s3 = replayStr.find('|', s2 + 1);
    if (s3 == std::string::npos)
      break;
    std::size_t s4 = replayStr.find(',', s3 + 1);
    if (s4 == std::string::npos)
      s4 = replayStr.size();

    const auto delta =
        std::stoll(std::string(replayStr.substr(pos, s1 - pos)));
    double x = std::stod(std::string(replayStr.substr(s1 + 1, s2 - s1 - 1)));
    double y = std::stod(std::string(replayStr.substr(s2 + 1, s3 - s2 - 1)));
    int newKeys = std::stoi(std::string(replayStr.substr(s3 + 1, s4 - s3 - 1)));

    // -12345 is the seed frame, and stable also writes a couple of frames at
    // -1 before the map starts. Neither is input.
    if (delta == -12345) {
      pos = s4 + 1;
      continue;
    }
    now = deltas ? now + delta : delta;
    const auto ts = static_cast<double>(now);
    if (now < 0) {
      keys = newKeys;
      pos = s4 + 1;
      continue;
    }

    // Two logical buttons out of the four bits: K1 is written together with
    // M1 and K2 with M2, so 5 is "left" and 10 is "right". A press of either
    // is a press -- a stream alternating between them is two taps, not one
    // held key -- and only letting go of both is a release.
    const bool leftWas = (keys & 5) != 0;
    const bool rightWas = (keys & 10) != 0;
    const bool leftNow = (newKeys & 5) != 0;
    const bool rightNow = (newKeys & 10) != 0;
    const int pressed = (leftNow && !leftWas ? 1 : 0) +
                        (rightNow && !rightWas ? 1 : 0);
    if (pressed > 0) {
      for (int k = 0; k < pressed; ++k)
        events.push_back({ts, {x, y}, InputAction::kPress});
    } else if ((leftWas || rightWas) && !leftNow && !rightNow)
      events.push_back({ts, {x, y}, InputAction::kRelease});
    else
      events.push_back({ts, {x, y}, InputAction::kMove});
    keys = newKeys;
    pos = s4 + 1;
  }
  return events;
}

// The mod acronyms lazer knows this client's mods by. The legacy bitfield in
// the header is enough for stable, but a lazer-version replay has its mods
// read back out of the JSON block instead, so they have to agree.
[[nodiscard]] inline std::vector<std::string> modAcronyms(ModSet mods) {
  std::vector<std::string> out;
  const auto add = [&out, mods](ModSet flag, const char *acronym) {
    if (hasMod(mods, flag)) {
      out.emplace_back(acronym);
    }
  };
  add(mod::kNoFail, "NF");
  add(mod::kEasy, "EZ");
  add(mod::kHidden, "HD");
  add(mod::kHardRock, "HR");
  add(mod::kDoubleTime, "DT");
  add(mod::kHalfTime, "HT");
  add(mod::kAuto, "AT");
  add(mod::kAutopilot, "AP");
  return out;
}

// ScoreRank has no SS: an SS is an X, and X and S go silver under Hidden or
// Flashlight (ModHidden.AdjustRank). The name has to be one lazer's enum
// knows -- a rank it cannot parse throws while deserialising and takes the
// whole import down with it, rather than being skipped.
[[nodiscard]] inline std::string scoreRankName(std::string_view grade,
                                               ModSet mods) {
  const bool silver = hasMod(mods, mod::kHidden);
  if (grade == "SS") {
    return silver ? "XH" : "X";
  }
  if (grade == "S") {
    return silver ? "SH" : "S";
  }
  if (grade == "A" || grade == "B" || grade == "C" || grade == "D" ||
      grade == "F") {
    return std::string(grade);
  }
  return "D";
}

// LegacyReplaySoloScoreInfo, serialised the way osu!'s global JSON settings
// serialise it: snake_case keys, zero counts left out entirely.
[[nodiscard]] inline std::string soloScoreJson(const ReplayStatistics &s,
                                               ModSet mods) {
  std::string out = "{";
  out += R"("client_version":"","rank":")" + scoreRankName(s.fRank, mods) +
         R"(","user_id":-1,)";
  out += R"("online_id":-1,"mods":[)";
  bool first = true;
  for (const auto &acronym : modAcronyms(mods)) {
    if (!first) {
      out += ",";
    }
    first = false;
    out += R"({"acronym":")" + acronym + R"("})";
  }
  out += "],\"statistics\":{";
  first = true;
  const auto pair = [&out, &first](const char *key, int value) {
    if (value == 0) {
      return;
    }
    if (!first) {
      out += ",";
    }
    first = false;
    out += std::format("\"{}\":{}", key, value);
  };
  pair("great", s.fGreat);
  pair("ok", s.fOk);
  pair("meh", s.fMeh);
  pair("miss", s.fMiss);
  pair("large_tick_hit", s.fLargeTickHit);
  pair("large_tick_miss", s.fLargeTickMiss);
  pair("slider_tail_hit", s.fSliderTailHit);
  pair("small_bonus", s.fSmallBonus);
  pair("large_bonus", s.fLargeBonus);
  out += "},\"maximum_statistics\":{";
  first = true;
  pair("great", s.fMaxGreat);
  pair("large_tick_hit", s.fMaxLargeTick);
  pair("slider_tail_hit", s.fMaxSliderTail);
  pair("small_bonus", s.fMaxSmallBonus);
  pair("large_bonus", s.fMaxLargeBonus);
  out += "},";
  // Written only when there is one; lazer's own encoder sends null otherwise
  // and works out the value itself on import.
  if (s.fTotalScore > 0) {
    out += std::format("\"total_score_without_mods\":{},", s.fTotalScore);
  }
  out += "\"pauses\":[]}";
  return out;
}

} // namespace detail

[[nodiscard]] inline std::vector<std::uint8_t>
encodeReplay(std::span<const InputEvent> events, const std::string &beatmapMd5,
             const std::string &playerName, ModSet mods,
             const ReplayScore &score = {},
             const ReplayStatistics &stats = {}) {
  using namespace detail;
  std::vector<std::uint8_t> out;

  writeByte(out, 0);
  writeInt(out, kLazerScoreVersion);
  writeString(out, beatmapMd5);
  writeString(out, playerName);

  std::vector<std::uint8_t> replayBytes = encodeReplayData(events);
  auto replayHash = md5::hash(replayBytes);
  writeString(out, md5::hexString(replayHash));

  writeShort(out, static_cast<std::int16_t>(score.f300));
  writeShort(out, static_cast<std::int16_t>(score.f100));
  writeShort(out, static_cast<std::int16_t>(score.f50));
  writeShort(out, static_cast<std::int16_t>(score.fGeki));
  writeShort(out, static_cast<std::int16_t>(score.fKatu));
  writeShort(out, static_cast<std::int16_t>(score.fMiss));
  writeInt(out, score.fTotalScore);
  writeShort(out, static_cast<std::int16_t>(score.fMaxCombo));
  writeByte(out, score.fPerfect ? 1 : 0);

  writeInt(out, static_cast<std::int32_t>(mods.fValue));
  writeString(out, "");

  auto now = std::chrono::system_clock::now();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch())
                .count();
  std::int64_t ticks = ns / 100 + 116444736000000000LL;
  writeLong(out, ticks);

  auto compressed = lzmaCompress(replayBytes);
  writeInt(out, static_cast<std::int32_t>(compressed.size()));
  out.insert(out.end(), compressed.begin(), compressed.end());

  writeLong(out, -1); // LegacyOnlineID: this score was never uploaded

  // LegacyReplaySoloScoreInfo, which every reader past version 30000001 asks
  // for. It carries the counts the four legacy shorts cannot -- ticks, tails,
  // bonus spins -- and it replaces the mods outright on import, so the
  // acronyms have to be here even though the legacy bitfield is above. A
  // length of -1 is a null array, which is how the block is left out.
  if (stats.fPresent) {
    std::string json = detail::soloScoreJson(stats, mods);
    std::vector<std::uint8_t> bytes(json.begin(), json.end());
    auto blob = lzmaCompress(bytes);
    writeInt(out, static_cast<std::int32_t>(blob.size()));
    out.insert(out.end(), blob.begin(), blob.end());
  } else {
    writeInt(out, -1);
  }
  return out;
}

// The header, parsed with bounds checks so that a truncated buffer -- a
// prefix read for indexing, or a half-written file -- is rejected instead of
// running off the end. `sp` is left at the first byte after the header.
[[nodiscard]] inline ReplayHeader
parseReplayHeader(std::span<const std::uint8_t> &sp) {
  using namespace detail;
  const auto need = [&sp](std::size_t n) {
    if (sp.size() < n) {
      throw std::runtime_error{"replay header truncated"};
    }
  };
  const auto str = [&sp, &need]() {
    need(1);
    if (sp[0] == 0x00) {
      sp = sp.subspan(1);
      return std::string{};
    }
    auto probe = sp.subspan(1);
    // ULEB128 length, at most five bytes for the 32-bit lengths osu! writes.
    std::uint64_t len = 0;
    int shift = 0;
    std::size_t used = 0;
    while (true) {
      if (used >= probe.size() || used >= 5) {
        throw std::runtime_error{"replay header truncated"};
      }
      const std::uint8_t byte = probe[used++];
      len |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
      shift += 7;
      if ((byte & 0x80) == 0) {
        break;
      }
    }
    probe = probe.subspan(used);
    if (probe.size() < len) {
      throw std::runtime_error{"replay header truncated"};
    }
    std::string out(probe.begin(),
                    probe.begin() + static_cast<std::ptrdiff_t>(len));
    sp = probe.subspan(static_cast<std::size_t>(len));
    return out;
  };

  ReplayHeader h;
  need(5);
  readByte(sp); // mode
  h.fVersion = readInt(sp); // game version, and this client's rules stamp
  h.fBeatmapMd5 = str();
  h.fPlayerName = str();
  str(); // replay md5
  need(23);
  h.fScore.f300 = readShort(sp);
  h.fScore.f100 = readShort(sp);
  h.fScore.f50 = readShort(sp);
  h.fScore.fGeki = readShort(sp);
  h.fScore.fKatu = readShort(sp);
  h.fScore.fMiss = readShort(sp);
  h.fScore.fTotalScore = readInt(sp);
  h.fScore.fMaxCombo = readShort(sp);
  h.fScore.fPerfect = readByte(sp) != 0;
  h.fMods = osu::ModSet(static_cast<std::uint32_t>(readInt(sp)));
  str(); // life bar graph
  need(8);
  readLong(sp); // timestamp
  return h;
}

[[nodiscard]] inline ReplayHeader
decodeReplayHeader(std::span<const std::uint8_t> data) {
  auto sp = data;
  return parseReplayHeader(sp);
}

// Whether a replay is one of the old ones, answered from the file. The events
// have to be decompressed to see the seed frame, so this is not free -- the
// index calls it once per new file and remembers the answer.
[[nodiscard]] inline bool replayIsLegacyFormat(
    std::span<const std::uint8_t> data) noexcept {
  using namespace detail;
  try {
    auto sp = data;
    // Parsed for the side effect: it leaves `sp` at the first byte past the
    // header, which is where the compressed events begin. The header itself
    // says nothing about which shape the replay is in.
    static_cast<void>(parseReplayHeader(sp));
    if (sp.size() < 4) {
      return false;
    }
    const std::int32_t len = readInt(sp);
    if (len <= 0 || sp.size() < static_cast<std::size_t>(len)) {
      return false;
    }
    auto events = lzmaDecompress(sp.subspan(0, static_cast<std::size_t>(len)));
    if (events.empty()) {
      return false;
    }
    return !hasSeedFrame(
        std::string_view(reinterpret_cast<const char *>(events.data()),
                         events.size()));
  } catch (const std::exception &) {
    return false;
  }
}

[[nodiscard]] inline ReplayData
decodeReplay(std::span<const std::uint8_t> data) {
  using namespace detail;
  ReplayData result;
  auto sp = data;

  auto header = parseReplayHeader(sp);
  result.fBeatmapMd5 = std::move(header.fBeatmapMd5);
  result.fPlayerName = std::move(header.fPlayerName);
  result.fMods = header.fMods;
  result.fScore = header.fScore;
  result.fVersion = header.fVersion;

  std::int32_t replayLen = readInt(sp);
  auto replayBytes = sp.subspan(0, static_cast<std::size_t>(replayLen));

  auto decompressed = lzmaDecompress(replayBytes);
  std::string replayStr(decompressed.begin(), decompressed.end());
  result.fLegacyFormat = !hasSeedFrame(replayStr);
  result.fEvents = decodeReplayData(replayStr);
  return result;
}

[[nodiscard]] inline std::string
md5HashString(std::span<const std::uint8_t> data) {
  return detail::md5::hexString(detail::md5::hash(data));
}

} // namespace osu
