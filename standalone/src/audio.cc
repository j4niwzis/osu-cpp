module;

#include <AL/al.h>
#include <AL/alc.h>
#include <mpg123.h>
#include <sndfile.h>

export module audio;

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
};

static sf_count_t sndfileGetFilelen(void *user) {
  const auto *m = static_cast<const SndfileMemory *>(user);
  return static_cast<sf_count_t>(m->data.size());
}

static sf_count_t sndfileSeek(sf_count_t offset, int whence, void *user) {
  auto *m = static_cast<SndfileMemory *>(user);
  switch (whence) {
  case SEEK_SET:
    m->offset = offset;
    break;
  case SEEK_CUR:
    m->offset += offset;
    break;
  case SEEK_END:
    m->offset = static_cast<sf_count_t>(m->data.size()) + offset;
    break;
  }
  return m->offset;
}

static sf_count_t sndfileRead(void *ptr, sf_count_t count, void *user) {
  auto *m = static_cast<SndfileMemory *>(user);
  const sf_count_t available =
      static_cast<sf_count_t>(m->data.size()) - m->offset;
  const sf_count_t toRead = std::min(count, available);
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

[[nodiscard]] inline std::vector<std::int16_t>
decode_sndfile(const std::filesystem::path &path, int &rate, int &channels) {
  std::cerr << "[audio] decode_sndfile: opening " << path << '\n';
  SF_INFO info{};
  SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
  if (file == nullptr) {
    std::cerr << "[audio] decode_sndfile: sf_open failed\n";
    return {};
  }
  // libsndfile converts float data to short without clipping unless it is
  // asked to: a sample above full scale wraps instead of being limited, so
  // the loudest peaks come back inverted. Ogg Vorbis (which is what osu!
  // serves previews as, and what plenty of beatmap audio is) decodes to
  // float, and modern masters sit at or above 0 dBFS, so this is heard as
  // the sound tearing at its loudest -- as if it were cut off, leaving only
  // what was quiet enough to survive.
  sf_command(file, SFC_SET_CLIPPING, nullptr, SF_TRUE);

  rate = info.samplerate;
  channels = info.channels;
  const auto frames = static_cast<sf_count_t>(info.frames);
  std::cerr << "[audio] decode_sndfile: " << frames << " frames, " << rate
            << " Hz, " << channels << " ch\n";
  std::vector<std::int16_t> out(
      static_cast<std::size_t>(frames * info.channels));
  const auto read = sf_readf_short(file, out.data(), frames);
  out.resize(static_cast<std::size_t>(read * info.channels));
  sf_close(file);
  std::cerr << "[audio] decode_sndfile: returned " << out.size()
            << " samples\n";
  return out;
}

[[nodiscard]] inline std::vector<std::int16_t>
decode_mp3(const std::filesystem::path &path, int &rate, int &channels) {
  std::cerr << "[audio] decode_mp3: starting " << path << '\n';
  if (!ensureMpg123Init()) {
    std::cerr << "[audio] decode_mp3: mpg123_init failed\n";
    return {};
  }
  std::cerr << "[audio] decode_mp3: creating handle\n";
  mpg123_handle *handle = mpg123_new(nullptr, nullptr);
  if (handle == nullptr) {
    std::cerr << "[audio] decode_mp3: mpg123_new failed\n";
    return {};
  }
  std::cerr << "[audio] decode_mp3: opening file\n";
  if (mpg123_open(handle, path.c_str()) != MPG123_OK) {
    std::cerr << "[audio] decode_mp3: mpg123_open failed\n";
    mpg123_delete(handle);
    return {};
  }

  long nativeRate = 0;
  int nativeChannels = 0;
  int encoding = 0;
  std::cerr << "[audio] decode_mp3: getting format\n";
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
  std::cerr << "[audio] decode_mp3: entering decode loop\n";
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
      std::cerr << "[audio] decode_mp3: MPG123_DONE\n";
      break;
    }
    if (err != MPG123_OK && err != MPG123_NEW_FORMAT) {
      std::cerr << "[audio] decode_mp3: error " << err << '\n';
      break;
    }
  }

  std::cerr << "[audio] decode_mp3: closing, produced " << out.size()
            << " samples\n";
  mpg123_close(handle);
  mpg123_delete(handle);
  return out;
}

[[nodiscard]] inline std::vector<std::int16_t>
decode_sndfile_memory(std::span<const std::uint8_t> data, int &rate,
                      int &channels) {
  std::cerr << "[audio] decode_sndfile_memory: " << data.size() << " bytes\n";
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
  // libsndfile converts float data to short without clipping unless it is
  // asked to: a sample above full scale wraps instead of being limited, so
  // the loudest peaks come back inverted. Ogg Vorbis (which is what osu!
  // serves previews as, and what plenty of beatmap audio is) decodes to
  // float, and modern masters sit at or above 0 dBFS, so this is heard as
  // the sound tearing at its loudest -- as if it were cut off, leaving only
  // what was quiet enough to survive.
  sf_command(file, SFC_SET_CLIPPING, nullptr, SF_TRUE);

  rate = info.samplerate;
  channels = info.channels;
  const auto frames = static_cast<sf_count_t>(info.frames);
  std::cerr << "[audio] decode_sndfile_memory: " << frames << " frames, "
            << rate << " Hz, " << channels << " ch\n";
  std::vector<std::int16_t> out(
      static_cast<std::size_t>(frames * info.channels));
  const auto read = sf_readf_short(file, out.data(), frames);
  out.resize(static_cast<std::size_t>(read * info.channels));
  sf_close(file);
  std::cerr << "[audio] decode_sndfile_memory: returned " << out.size()
            << " samples\n";
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

// How hot the decoded material is. A track mastered to full scale plus a
// generous gain is what an output limiter reacts to, and that is heard as
// the sound ducking; this says which side the problem is on.
inline void report_pcm_level(std::span<const std::int16_t> samples,
                             const char *what) {
  if (samples.empty()) {
    return;
  }
  int peak = 0;
  std::size_t hot = 0;
  for (const std::int16_t s : samples) {
    const int magnitude = s == -32768 ? 32767 : (s < 0 ? -s : s);
    peak = magnitude > peak ? magnitude : peak;
    if (magnitude > 32000) {
      ++hot;
    }
  }
  std::cerr << "[audio] " << what << ": peak " << peak << " ("
            << (100.0 * static_cast<double>(hot) /
                static_cast<double>(samples.size()))
            << "% near full scale)\n";
}

} // namespace audio
