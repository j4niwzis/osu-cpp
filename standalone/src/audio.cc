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
  std::cerr << "[audio] decode_mp3: " << nativeRate << " Hz, "
            << nativeChannels << " ch\n";
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
      const auto *pcm = reinterpret_cast<const std::int16_t *>(buffer.data());
      out.insert(out.end(), pcm, pcm + samples);
    } else if (err == MPG123_OK) {
      // Some decoders return OK with no data for a few iterations; prevent
      // an infinite loop by bailing out after repeated empty reads.
      if (++emptyReads >= kMaxEmptyReads) {
        std::cerr << "[audio] decode_mp3: no data for " << kMaxEmptyReads
                  << " consecutive reads, aborting decode of " << path
                  << '\n';
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

} // namespace audio
