module;

#include <AL/al.h>
#include <AL/alc.h>
#include <mpg123.h>
#include <sndfile.h>

export module platform.audio;

import std;

export namespace audio {

// OpenAL types
using ::ALCboolean;
using ::ALCchar;
using ::ALCcontext;
using ::ALCdevice;
using ::ALCenum;
using ::ALCint;
using ::ALCsizei;
using ::ALdouble;
using ::ALenum;
using ::ALfloat;
using ::ALint;
using ::ALsizei;
using ::ALuint;

// OpenAL device/context
using ::alcCloseDevice;
using ::alcCreateContext;
using ::alcDestroyContext;
using ::alcGetError;
using ::alcGetIntegerv;
using ::alcGetString;
using ::alcMakeContextCurrent;
using ::alcOpenDevice;

// OpenAL buffers/sources
using ::alBufferData;
using ::alDeleteBuffers;
using ::alDeleteSources;
using ::alGenBuffers;
using ::alGenSources;
using ::alGetError;
using ::alGetSourcef;
using ::alGetSourcei;
using ::alListenerf;
using ::alSourcef;
using ::alSourcei;
using ::alSourcePause;
using ::alSourcePlay;
using ::alSourceStop;

// mpg123 types
using ::mpg123_handle;

// Constants
inline constexpr ALenum kNoError = AL_NO_ERROR;
inline constexpr ALCint kAlcMonoSources = ALC_MONO_SOURCES;
inline constexpr ALCenum kAlcFrequency = ALC_FREQUENCY;
inline constexpr ALCenum kAlcRefresh = ALC_REFRESH;
inline constexpr ALCenum kAlcSync = ALC_SYNC;
inline constexpr ALCenum kAlcDeviceSpecifier = ALC_DEVICE_SPECIFIER;
inline constexpr ALCint kAlcStereoSources = ALC_STEREO_SOURCES;
inline constexpr ALenum kFormatMono8 = AL_FORMAT_MONO8;
inline constexpr ALenum kFormatMono16 = AL_FORMAT_MONO16;
inline constexpr ALenum kFormatStereo8 = AL_FORMAT_STEREO8;
inline constexpr ALenum kFormatStereo16 = AL_FORMAT_STEREO16;
inline constexpr ALenum kSourceState = AL_SOURCE_STATE;
inline constexpr ALenum kBuffersQueued = AL_BUFFERS_QUEUED;
inline constexpr ALenum kBuffersProcessed = AL_BUFFERS_PROCESSED;
inline constexpr ALenum kBuffer = AL_BUFFER;
inline constexpr ALenum kLooping = AL_LOOPING;
inline constexpr ALenum kGain = AL_GAIN;
inline constexpr ALenum kListenerGain = AL_GAIN;
inline constexpr ALenum kSecOffset = AL_SEC_OFFSET;
inline constexpr ALint kPlaying = AL_PLAYING;
inline constexpr ALint kPaused = AL_PAUSED;
inline constexpr ALint kStopped = AL_STOPPED;
inline constexpr ALint kInitial = AL_INITIAL;

inline constexpr int kMpg123Ok = MPG123_OK;
inline constexpr int kMpg123Done = MPG123_DONE;
inline constexpr int kMpg123NewFormat = MPG123_NEW_FORMAT;
inline constexpr long kMpg123EncSigned16 = MPG123_ENC_SIGNED_16;

} // namespace audio

namespace audio {

class Mpg123Init {
public:
  Mpg123Init() { fOk = (mpg123_init() == MPG123_OK); }
  ~Mpg123Init() {
    if (fOk) {
      mpg123_exit();
    }
  }
  Mpg123Init(const Mpg123Init &) = delete;
  Mpg123Init &operator=(const Mpg123Init &) = delete;

  [[nodiscard]] bool ok() const noexcept { return fOk; }

private:
  bool fOk = false;
};

[[nodiscard]] bool ensureMpg123Init() {
  static Mpg123Init init;
  return init.ok();
}

struct SndfileMemory {
  std::span<const std::uint8_t> data;
  sf_count_t offset = 0;
  bool clamped = false; // libsndfile asked for something outside the buffer
};

static sf_count_t sndfileGetFilelen(void *user) {
  const auto *m = static_cast<const SndfileMemory *>(user);
  return static_cast<sf_count_t>(m->data.size());
}

static sf_count_t sndfileSeek(sf_count_t offset, int whence, void *user) {
  auto *m = static_cast<SndfileMemory *>(user);
  const auto size = static_cast<sf_count_t>(m->data.size());
  sf_count_t target = m->offset;
  switch (whence) {
  case SEEK_SET:
    target = offset;
    break;
  case SEEK_CUR:
    target += offset;
    break;
  case SEEK_END:
    target = size + offset;
    break;
  }
  // A real file lets the cursor sit past its end and reads nothing there;
  // this has to behave the same. Without the clamp the offset goes out of
  // range and the read below computes a negative count.
  if (target < 0 || target > size) {
    m->clamped = true;
  }
  m->offset = std::clamp(target, sf_count_t{0}, size);
  return m->offset;
}

static sf_count_t sndfileRead(void *ptr, sf_count_t count, void *user) {
  auto *m = static_cast<SndfileMemory *>(user);
  const sf_count_t available =
      static_cast<sf_count_t>(m->data.size()) - m->offset;
  // Anything but a count in [0, requested] is a lie to libsndfile: it takes
  // the return value as the number of bytes it may now read out of the
  // buffer it handed over, so a negative or clamped-wrong value leaves it
  // decoding whatever that buffer happened to contain.
  if (available < 0) {
    m->clamped = true;
  }
  const sf_count_t toRead =
      std::max(sf_count_t{0}, std::min(count, available));
  if (toRead > 0) {
    std::memcpy(ptr, m->data.data() + m->offset,
                static_cast<std::size_t>(toRead));
    m->offset += toRead;
  }
  return toRead;
}

static sf_count_t sndfileWrite(const void *, sf_count_t, void *) { return 0; }

static sf_count_t sndfileTell(void *user) {
  const auto *m = static_cast<const SndfileMemory *>(user);
  return m->offset;
}

} // namespace audio

export namespace audio {

// libsndfile's own float-to-short conversion wraps.
//
// Its Ogg/Vorbis reader multiplies the decoded float by 32767 and stores the
// result in a short without limiting it, and SFC_SET_CLIPPING -- which is
// accepted, and which SFC_GET_CLIPPING then confirms as enabled -- does not
// reach that path. Vorbis decodes to float and modern masters run past full
// scale (this preview peaks at 1.31, some 2.3 dB over), so every peak came
// back with its sign flipped: the sound tore exactly where it was loudest.
//
// Reading floats and converting here gives samples identical to ffmpeg's,
// and it is done in chunks because a float copy of a whole track would cost
// four times what the result does.
[[nodiscard]] inline std::vector<std::int16_t>
readAsShort(SNDFILE *file, sf_count_t frames, int channels) {
  std::vector<std::int16_t> out;
  if (channels <= 0) {
    return out;
  }
  const auto stride = static_cast<std::size_t>(channels);
  out.reserve(static_cast<std::size_t>(frames) * stride);

  constexpr sf_count_t kChunkFrames = 1 << 15;
  std::vector<float> chunk(static_cast<std::size_t>(kChunkFrames) * stride);
  while (true) {
    const sf_count_t read = sf_readf_float(file, chunk.data(), kChunkFrames);
    if (read <= 0) {
      break;
    }
    const auto count = static_cast<std::size_t>(read) * stride;
    for (std::size_t i = 0; i < count; ++i) {
      const float scaled = chunk[i] * 32767.0f;
      const float limited = std::clamp(scaled, -32768.0f, 32767.0f);
      out.push_back(static_cast<std::int16_t>(std::lrintf(limited)));
    }
  }
  return out;
}

[[nodiscard]] inline std::vector<std::int16_t>
decode_sndfile(const std::filesystem::path &path, int &rate, int &channels) {
  SF_INFO info{};
  SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
  if (file == nullptr) {
    std::cerr << "[audio] decode_sndfile: sf_open failed\n";
    return {};
  }
  rate = info.samplerate;
  channels = info.channels;
  const auto frames = static_cast<sf_count_t>(info.frames);
  std::cerr << "[audio] decode_sndfile: " << frames << " frames, " << rate
            << " Hz, " << channels << " ch\n";
  auto out = readAsShort(file, frames, info.channels);
  sf_close(file);
  return out;
}

[[nodiscard]] inline std::vector<std::int16_t>
decode_mp3(const std::filesystem::path &path, int &rate, int &channels) {
  if (!ensureMpg123Init()) {
    std::cerr << "[audio] decode_mp3: mpg123_init failed\n";
    return {};
  }
  mpg123_handle *handle = mpg123_new(nullptr, nullptr);
  if (handle == nullptr) {
    std::cerr << "[audio] decode_mp3: mpg123_new failed\n";
    return {};
  }
  if (mpg123_open(handle, path.c_str()) != MPG123_OK) {
    std::cerr << "[audio] decode_mp3: mpg123_open failed\n";
    mpg123_delete(handle);
    return {};
  }

  long nativeRate = 0;
  int nativeChannels = 0;
  int encoding = 0;
  if (mpg123_getformat(handle, &nativeRate, &nativeChannels, &encoding) !=
      MPG123_OK) {
    std::cerr << "[audio] decode_mp3: mpg123_getformat failed\n";
    mpg123_close(handle);
    mpg123_delete(handle);
    return {};
  }
  std::cerr << "[audio] decode_mp3: " << nativeRate << " Hz, " << nativeChannels
            << " ch\n";
  mpg123_format_none(handle);
  mpg123_format(handle, nativeRate, nativeChannels, MPG123_ENC_SIGNED_16);
  rate = static_cast<int>(nativeRate);
  channels = nativeChannels;

  std::vector<std::int16_t> out;
  if (const off_t length = mpg123_length(handle); length > 0) {
    out.reserve(static_cast<std::size_t>(length * nativeChannels));
    std::cerr << "[audio] decode_mp3: reserved " << length * nativeChannels
              << " samples\n";
  }

  constexpr std::size_t kBlock = 256 * 1024;
  std::vector<unsigned char> buffer(kBlock);
  std::size_t done = 0;
  int emptyReads = 0;
  constexpr int kMaxEmptyReads = 16;
  while (true) {
    const int err = mpg123_read(handle, buffer.data(), buffer.size(), &done);
    if (done > 0) {
      emptyReads = 0;
      const std::size_t samples = done / sizeof(std::int16_t);
      const std::size_t oldSize = out.size();
      out.resize(oldSize + samples);
      std::memcpy(out.data() + oldSize, buffer.data(),
                  samples * sizeof(std::int16_t));
    } else if (err == MPG123_OK) {
      // Some decoders return OK with no data for a few iterations; prevent
      // an infinite loop by bailing out after repeated empty reads.
      if (++emptyReads >= kMaxEmptyReads) {
        std::cerr << "[audio] decode_mp3: no data for " << kMaxEmptyReads
                  << " consecutive reads, aborting decode of " << path << '\n';
        break;
      }
    }
    if (err == MPG123_DONE) {
      break;
    }
    if (err != MPG123_OK && err != MPG123_NEW_FORMAT) {
      std::cerr << "[audio] decode_mp3: error " << err << '\n';
      break;
    }
  }

  std::cerr << "[audio] decode_mp3: " << out.size()
            << " samples\n";
  mpg123_close(handle);
  mpg123_delete(handle);
  return out;
}

[[nodiscard]] inline std::vector<std::int16_t>
decode_sndfile_memory(std::span<const std::uint8_t> data, int &rate,
                      int &channels) {
  SF_VIRTUAL_IO io{};
  io.get_filelen = sndfileGetFilelen;
  io.seek = sndfileSeek;
  io.read = sndfileRead;
  io.write = sndfileWrite;
  io.tell = sndfileTell;

  SndfileMemory mem{data, 0};
  SF_INFO info{};
  SNDFILE *file = sf_open_virtual(&io, SFM_READ, &info, &mem);
  if (file == nullptr) {
    std::cerr << "[audio] decode_sndfile_memory: sf_open_virtual failed\n";
    return {};
  }
  rate = info.samplerate;
  channels = info.channels;
  const auto frames = static_cast<sf_count_t>(info.frames);
  std::cerr << "[audio] decode_sndfile_memory: " << frames << " frames, "
            << rate << " Hz, " << channels << " ch\n";
  auto out = readAsShort(file, frames, info.channels);
  sf_close(file);
  if (mem.clamped) {
    std::cerr << "[audio] decode_sndfile_memory: reads outside the buffer "
                 "were requested and refused\n";
  }
  return out;
}

[[nodiscard]] inline std::vector<std::int16_t>
decode_mp3_memory(std::span<const std::uint8_t> data, int &rate,
                  int &channels) {
  if (!ensureMpg123Init()) {
    std::cerr << "[audio] decode_mp3_memory: mpg123_init failed\n";
    return {};
  }
  mpg123_handle *handle = mpg123_new(nullptr, nullptr);
  if (handle == nullptr) {
    std::cerr << "[audio] decode_mp3_memory: mpg123_new failed\n";
    return {};
  }

  // The output format is fixed before a single frame is decoded. Letting
  // mpg123 pick means it picks its own preference -- float32 on a build with
  // float support -- and the first block comes back in that encoding, which
  // the old code then copied into an int16 buffer as if it were samples.
  // Changing the format afterwards only affects what follows, so the track
  // began with a burst of noise and could stay in the wrong encoding
  // entirely.
  mpg123_format_none(handle);
  const long *rates = nullptr;
  std::size_t rateCount = 0;
  mpg123_rates(&rates, &rateCount);
  for (std::size_t i = 0; i < rateCount; ++i) {
    mpg123_format(handle, rates[i], MPG123_MONO | MPG123_STEREO,
                  MPG123_ENC_SIGNED_16);
  }

  if (mpg123_open_feed(handle) != MPG123_OK) {
    std::cerr << "[audio] decode_mp3_memory: mpg123_open_feed failed\n";
    mpg123_delete(handle);
    return {};
  }
  if (mpg123_feed(handle, data.data(), data.size()) != MPG123_OK) {
    std::cerr << "[audio] decode_mp3_memory: mpg123_feed failed\n";
    mpg123_close(handle);
    mpg123_delete(handle);
    return {};
  }

  std::vector<std::int16_t> out;
  constexpr std::size_t kBlock = 256 * 1024;
  std::vector<unsigned char> buffer(kBlock);
  long nativeRate = 0;
  int nativeChannels = 0;
  int encoding = 0;
  bool haveFormat = false;
  int err = MPG123_OK;
  while (err != MPG123_DONE) {
    std::size_t done = 0;
    err = mpg123_read(handle, buffer.data(), buffer.size(), &done);
    if (err == MPG123_NEW_FORMAT || (!haveFormat && err == MPG123_OK)) {
      if (mpg123_getformat(handle, &nativeRate, &nativeChannels, &encoding) ==
          MPG123_OK) {
        haveFormat = true;
        rate = static_cast<int>(nativeRate);
        channels = nativeChannels;
      }
    }
    if (done > 0) {
      const std::size_t samples = done / sizeof(std::int16_t);
      const std::size_t oldSize = out.size();
      out.resize(oldSize + samples);
      std::memcpy(out.data() + oldSize, buffer.data(),
                  samples * sizeof(std::int16_t));
    }
    if (err == MPG123_NEED_MORE) {
      break; // everything was fed up front, so this is the end of the stream
    }
    if (err != MPG123_OK && err != MPG123_NEW_FORMAT && err != MPG123_DONE) {
      std::cerr << "[audio] decode_mp3_memory: error " << err << " ("
                << mpg123_plain_strerror(err) << ")\n";
      break;
    }
  }
  if (!haveFormat) {
    std::cerr << "[audio] decode_mp3_memory: format detection failed\n";
  }

  mpg123_close(handle);
  mpg123_delete(handle);
  std::cerr << "[audio] decode_mp3_memory: " << rate << " Hz, " << channels
            << " ch, " << out.size() << " samples, encoding 0x" << std::hex
            << encoding << std::dec << '\n';
  return out;
}

// What the decoded material looks like before anything plays it.
//
// Two numbers separate the two possible culprits. The peak (and how much of
// the material sits near full scale) says whether the source is hot enough
// for a mixer to react to it. The discontinuity count -- adjacent samples
// that jump by more than half of full scale -- says whether the samples
// themselves are torn: wrapped conversions and mis-sized reads leave a trail
// of those, while clean audio has almost none however loud it is.
inline void report_pcm_level(std::span<const std::int16_t> samples,
                             const char *what, int channels = 1) {
  if (samples.empty()) {
    return;
  }
  const auto stride = static_cast<std::size_t>(channels > 0 ? channels : 1);
  int peak = 0;
  std::size_t hot = 0;
  std::size_t jumps = 0;
  std::size_t lastTearFrame = 0;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const int value = samples[i];
    const int magnitude = value == -32768 ? 32767 : (value < 0 ? -value : value);
    peak = magnitude > peak ? magnitude : peak;
    if (magnitude > 32000) {
      ++hot;
    }
    // Neighbours within a channel; comparing across the interleave would
    // count ordinary stereo width as tearing.
    if (i >= stride) {
      const int delta = value - samples[i - stride];
      if (delta > 32000 || delta < -32000) {
        ++jumps;
        // Where the tears sit says what made them: a regular spacing points
        // at block boundaries, a random one at the conversion.
        if (jumps <= 6) {
          std::cerr << "[audio]   tear at frame " << (i / stride) << " ch "
                    << (i % stride) << ": " << samples[i - stride] << " -> "
                    << value << " (gap " << (i / stride - lastTearFrame)
                    << " frames)\n";
        }
        lastTearFrame = i / stride;
      }
    }
  }
  // Silent when the material is intact: this exists to catch a decoder
  // mangling the samples, which is what it was written for, not to narrate
  // every track that loads.
  if (jumps > 0 || std::getenv("OSU_AUDIO_DUMP") != nullptr) {
    std::cerr << "[audio] " << what << ": peak " << peak << ", "
              << (100.0 * static_cast<double>(hot) /
                  static_cast<double>(samples.size()))
              << "% near full scale, " << jumps << " discontinuities in "
              << samples.size() << " samples\n";
  }
}

// Writes what was decoded, so it can be listened to outside the client: a
// clean file means the tearing happens downstream of the decoder. Off unless
// OSU_AUDIO_DUMP names a directory.
inline void dump_pcm(std::span<const std::int16_t> samples, int rate,
                     int channels) {
  const char *dir = std::getenv("OSU_AUDIO_DUMP");
  if (dir == nullptr || samples.empty() || channels <= 0) {
    return;
  }
  static int counter = 0;
  const std::string path =
      std::string(dir) + "/decoded-" + std::to_string(counter++) + ".wav";
  SF_INFO info{};
  info.samplerate = rate;
  info.channels = channels;
  info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  SNDFILE *file = sf_open(path.c_str(), SFM_WRITE, &info);
  if (file == nullptr) {
    std::cerr << "[audio] dump: cannot write " << path << '\n';
    return;
  }
  sf_writef_short(file, samples.data(),
                  static_cast<sf_count_t>(samples.size() /
                                          static_cast<std::size_t>(channels)));
  sf_close(file);
  std::cerr << "[audio] dump: wrote " << path << '\n';
}

} // namespace audio
