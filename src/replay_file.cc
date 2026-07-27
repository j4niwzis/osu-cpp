export module osu.replay_file;

import std;
import lzma;
import osu.types;
import osu.rules;
import osu.engine;

export namespace osu {

struct ReplayData {
  std::vector<InputEvent> fEvents;
  std::string fBeatmapMd5;
  std::string fPlayerName;
  ModSet fMods = mod::kNone;
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

inline std::vector<std::uint8_t>
lzmaCompress(std::span<const std::uint8_t> input) {
  std::vector<std::uint8_t> out(input.size() + input.size() / 3 + 128);
  lzma::lzma_stream strm = lzma::kStreamInit;
  if (lzma::lzma_easy_encoder(&strm, 6, lzma::kCheckCrc32) != lzma::kOk)
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
  return out;
}

inline std::vector<std::uint8_t>
lzmaDecompress(std::span<const std::uint8_t> input) {
  std::vector<std::uint8_t> out(1024 * 1024);
  lzma::lzma_stream strm = lzma::kStreamInit;
  if (lzma::lzma_stream_decoder(
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

inline std::vector<std::uint8_t>
encodeReplayData(std::span<const InputEvent> events) {
  std::string s;
  int keys = 0;
  for (const auto &ev : events) {
    if (ev.fAction == InputAction::kPress)
      keys |= 5;
    else if (ev.fAction == InputAction::kRelease)
      keys &= ~5;
    s +=
        std::format("{}|{:.7f}|{:.7f}|{},", static_cast<std::int64_t>(ev.fTime),
                    ev.fPos.fX, ev.fPos.fY, keys);
  }
  if (!s.empty())
    s.pop_back();
  return std::vector<std::uint8_t>(s.begin(), s.end());
}

inline std::vector<InputEvent> decodeReplayData(std::string_view replayStr) {
  std::vector<InputEvent> events;
  int keys = 0;
  std::size_t pos = 0;
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

    auto ts = static_cast<double>(
        std::stoll(std::string(replayStr.substr(pos, s1 - pos))));
    double x = std::stod(std::string(replayStr.substr(s1 + 1, s2 - s1 - 1)));
    double y = std::stod(std::string(replayStr.substr(s2 + 1, s3 - s2 - 1)));
    int newKeys = std::stoi(std::string(replayStr.substr(s3 + 1, s4 - s3 - 1)));

    if ((keys & 5) == 0 && (newKeys & 5) != 0)
      events.push_back({ts, {x, y}, InputAction::kPress});
    else if ((keys & 5) != 0 && (newKeys & 5) == 0)
      events.push_back({ts, {x, y}, InputAction::kRelease});
    else
      events.push_back({ts, {x, y}, InputAction::kMove});
    keys = newKeys;
    pos = s4 + 1;
  }
  return events;
}

} // namespace detail

[[nodiscard]] inline std::vector<std::uint8_t>
encodeReplay(std::span<const InputEvent> events, const std::string &beatmapMd5,
             const std::string &playerName, ModSet mods) {
  using namespace detail;
  std::vector<std::uint8_t> out;

  writeByte(out, 0);
  writeInt(out, 20250726);
  writeString(out, beatmapMd5);
  writeString(out, playerName);

  std::vector<std::uint8_t> replayBytes = encodeReplayData(events);
  auto replayHash = md5::hash(replayBytes);
  writeString(out, md5::hexString(replayHash));

  writeShort(out, 0);
  writeShort(out, 0);
  writeShort(out, 0);
  writeShort(out, 0);
  writeShort(out, 0);
  writeShort(out, 0);
  writeInt(out, 0);
  writeShort(out, 0);
  writeByte(out, 0);

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

  writeLong(out, 0);
  return out;
}

[[nodiscard]] inline ReplayData
decodeReplay(std::span<const std::uint8_t> data) {
  using namespace detail;
  ReplayData result;
  auto sp = data;

  readByte(sp);
  readInt(sp);
  result.fBeatmapMd5 = readString(sp);
  result.fPlayerName = readString(sp);
  readString(sp);
  readShort(sp);
  readShort(sp);
  readShort(sp);
  readShort(sp);
  readShort(sp);
  readShort(sp);
  readInt(sp);
  readShort(sp);
  readByte(sp);
  result.fMods = osu::ModSet(static_cast<std::uint32_t>(readInt(sp)));
  readString(sp);
  readLong(sp);

  std::int32_t replayLen = readInt(sp);
  auto replayBytes = sp.subspan(0, static_cast<std::size_t>(replayLen));

  auto decompressed = lzmaDecompress(replayBytes);
  std::string replayStr(decompressed.begin(), decompressed.end());
  result.fEvents = decodeReplayData(replayStr);
  return result;
}

[[nodiscard]] inline std::string
md5HashString(std::span<const std::uint8_t> data) {
  return detail::md5::hexString(detail::md5::hash(data));
}

} // namespace osu
