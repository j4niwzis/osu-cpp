export module client.audio;

import std;
import audio;
import client.util;

export namespace client {
namespace audio_client {

// OpenAL Soft mixes every source into one buffer and runs an output limiter
// over the sum. Sources at full gain add up past full scale in a hurry --
// music at 0.8 plus two hitsounds at 1.0 is already 2.8 -- and the limiter
// then ducks the whole mix, which is heard as the sound cutting out and
// coming back. These leave it room: the settings sliders stay the
// user-facing 0..1 scale and are folded into the gain here.
inline constexpr float kMusicHeadroom = 0.75f;
inline constexpr float kEffectHeadroom = 0.45f;

// OpenAL reports failures out of band; a source that could not be generated
// looks exactly like one that simply refuses to play.
inline bool alFailed(const char *what) {
  const audio::ALenum err = audio::alGetError();
  if (err == audio::kNoError) {
    return false;
  }
  std::println(std::cerr, "[audio] {} failed: 0x{:x}", what, err);
  return true;
}

inline audio::ALenum alFormat(int channels) {
  return channels == 1 ? audio::kFormatMono16 : audio::kFormatStereo16;
}

class AudioContext {
public:
  AudioContext() { this->init(); }
  ~AudioContext() { this->shutdown(); }

  [[nodiscard]] bool ok() const noexcept { return fContext != nullptr; }

private:
  audio::ALCdevice *fDevice = nullptr;
  audio::ALCcontext *fContext = nullptr;

  void init() {
    fDevice = audio::alcOpenDevice(nullptr);
    if (fDevice == nullptr)
      return;
    // Every SamplePlayer keeps a pool of sources and each track player owns
    // one, so the default limit (256 on OpenAL Soft) is reachable with a
    // skin's worth of hitsounds -- and once it is reached, alGenSources
    // silently hands back nothing and the next sound never plays.
    const audio::ALCint attrs[] = {audio::kAlcMonoSources, 1024,
                                   audio::kAlcStereoSources, 128, 0};
    fContext = audio::alcCreateContext(fDevice, attrs);
    if (fContext == nullptr) {
      audio::alcCloseDevice(fDevice);
      fDevice = nullptr;
      return;
    }
    audio::alcMakeContextCurrent(fContext);
    audio::alListenerf(audio::kListenerGain, 1.0f);

    // What the device actually gave us, which is not necessarily what was
    // asked for: a mismatched rate means everything is resampled, and the
    // source counts say whether the pools above will fit.
    audio::ALCint frequency = 0;
    audio::ALCint refresh = 0;
    audio::ALCint mono = 0;
    audio::ALCint stereo = 0;
    audio::alcGetIntegerv(fDevice, audio::kAlcFrequency, 1, &frequency);
    audio::alcGetIntegerv(fDevice, audio::kAlcRefresh, 1, &refresh);
    audio::alcGetIntegerv(fDevice, audio::kAlcMonoSources, 1, &mono);
    audio::alcGetIntegerv(fDevice, audio::kAlcStereoSources, 1, &stereo);
    const char *name =
        audio::alcGetString(fDevice, audio::kAlcDeviceSpecifier);
    std::println(std::cerr,
                 "[audio] device \"{}\": {} Hz, refresh {}, {} mono / {} "
                 "stereo sources",
                 name != nullptr ? name : "?", frequency, refresh, mono,
                 stereo);
  }

  void shutdown() {
    if (fContext != nullptr) {
      audio::alcMakeContextCurrent(nullptr);
      audio::alcDestroyContext(fContext);
      fContext = nullptr;
    }
    if (fDevice != nullptr) {
      audio::alcCloseDevice(fDevice);
      fDevice = nullptr;
    }
  }
};

inline AudioContext &audioContext() {
  static AudioContext ctx;
  return ctx;
}

// The container, from the bytes rather than the file name. osu! serves its
// track previews as Ogg Vorbis under a .mp3 URL, and beatmap archives are
// full of .mp3 files that are really Ogg or WAV, so the extension is only a
// fallback for formats without a recognisable magic number.
[[nodiscard]] inline std::string_view
sniffAudioExtension(std::span<const std::uint8_t> data,
                    std::string_view fallback) {
  if (data.size() >= 4) {
    if (data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S') {
      return ".ogg";
    }
    if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') {
      return ".wav";
    }
    if (data[0] == 'f' && data[1] == 'L' && data[2] == 'a' && data[3] == 'C') {
      return ".flac";
    }
  }
  if (data.size() >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
    return ".mp3";
  }
  // MPEG frame sync: eleven set bits.
  if (data.size() >= 2 && data[0] == 0xff && (data[1] & 0xe0) == 0xe0) {
    return ".mp3";
  }
  return fallback;
}

// Decodes with the decoder the bytes call for, and if that comes up empty,
// with the other one -- the two libraries cover disjoint formats.
[[nodiscard]] inline std::vector<std::int16_t>
decodePcm(std::span<const std::uint8_t> data, std::string_view ext, int &rate,
          int &channels) {
  const std::string_view actual = sniffAudioExtension(data, ext);
  std::vector<std::int16_t> samples;
  // Reported once per decode so a hot master is distinguishable from the
  // mixer misbehaving.
  if (actual == ".mp3") {
    samples = audio::decode_mp3_memory(data, rate, channels);
    if (samples.empty()) {
      samples = audio::decode_sndfile_memory(data, rate, channels);
    }
  } else {
    samples = audio::decode_sndfile_memory(data, rate, channels);
    if (samples.empty()) {
      samples = audio::decode_mp3_memory(data, rate, channels);
    }
  }
  audio::report_pcm_level(samples, "decoded", channels);
  audio::dump_pcm(samples, rate, channels);
  return samples;
}

// Decoded PCM handed between the loader thread and the UI thread: decoding
// is pure computation and must not sit in a frame, while the OpenAL upload
// has to happen where the context is current.
struct DecodedAudio {
  std::vector<std::int16_t> fSamples;
  int fRate = 0;
  int fChannels = 0;
};

[[nodiscard]] inline DecodedAudio decodeAudio(std::span<const std::uint8_t> data,
                                              std::string_view ext) {
  DecodedAudio out;
  out.fRate = 44100;
  out.fChannels = 2;
  out.fSamples = decodePcm(data, ext, out.fRate, out.fChannels);
  return out;
}

class AudioPlayer {
public:
  ~AudioPlayer() { this->shutdown(); }

  bool load(const std::filesystem::path &path) {
    const auto ext = detail::lowerExtension(path);
    const auto data = detail::readFile(path);
    return this->load(data, ext);
  }

  bool load(std::span<const std::uint8_t> data, std::string_view ext) {
    if (!audioContext().ok())
      return false;

    // Loading over an existing track (retry / new map) must release the old
    // buffer and source, or every restart leaks an OpenAL buffer.
    this->shutdown();

    int rate = 44100;
    int channels = 2;
    const auto samples = decodePcm(data, ext, rate, channels);
    if (samples.empty())
      return false;

    return this->upload(samples, rate, channels);
  }

  // Upload PCM decoded elsewhere (see decodeAudio).
  bool adopt(DecodedAudio pcm) {
    if (!audioContext().ok() || pcm.fSamples.empty()) {
      return false;
    }
    this->shutdown();
    audio::report_pcm_level(pcm.fSamples, "uploading", pcm.fChannels);
    return this->upload(pcm.fSamples, pcm.fRate, pcm.fChannels);
  }

  void play() {
    if (fSource == 0) {
      std::println(std::cerr, "[audio] play with no source");
      return;
    }
    audio::alSourcePlay(fSource);
    alFailed("alSourcePlay");
  }

  void setVolume(float gain) {
    if (fSource != 0)
      audio::alSourcef(fSource, audio::kGain, gain);
  }

  void setLooping(bool loop) {
    if (fSource != 0)
      audio::alSourcei(fSource, audio::kLooping, loop ? 1 : 0);
  }

  // Pause only if actually playing; resume only if actually paused.
  // alSourcePlay on a *stopped* source would restart it from zero, which is
  // exactly wrong when the map outlived its music.
  void pause() {
    if (fSource == 0)
      return;
    audio::ALint state = audio::kInitial;
    audio::alGetSourcei(fSource, audio::kSourceState, &state);
    if (state == audio::kPlaying)
      audio::alSourcePause(fSource);
  }

  void resume() {
    if (fSource == 0)
      return;
    audio::ALint state = audio::kInitial;
    audio::alGetSourcei(fSource, audio::kSourceState, &state);
    if (state == audio::kPaused)
      audio::alSourcePlay(fSource);
  }

  void stop() {
    if (fSource != 0)
      audio::alSourceStop(fSource);
  }

  // Decoded track kept as mono PCM so the menu visualiser can run its own
  // FFT (OpenAL exposes no spectrum API, unlike the BASS backend lazer uses).
  [[nodiscard]] std::span<const std::int16_t> monoSamples() const noexcept {
    return fMono;
  }
  [[nodiscard]] int sampleRate() const noexcept { return fRate; }

  [[nodiscard]] double durationSec() const noexcept {
    return fRate > 0 ? static_cast<double>(fMono.size()) /
                           static_cast<double>(fRate)
                     : 0.0;
  }

  [[nodiscard]] double positionSec() const {
    if (fSource == 0)
      return 0.0;
    audio::ALfloat sec = 0.0f;
    audio::alGetSourcef(fSource, audio::kSecOffset, &sec);
    return static_cast<double>(sec);
  }

  [[nodiscard]] bool playing() const {
    if (fSource == 0)
      return false;
    audio::ALint state = audio::kInitial;
    audio::alGetSourcei(fSource, audio::kSourceState, &state);
    return state == audio::kPlaying;
  }

private:
  audio::ALuint fBuffer = 0;
  audio::ALuint fSource = 0;
  std::vector<std::int16_t> fMono;
  int fRate = 0;

  bool upload(const std::vector<std::int16_t> &samples, int rate,
              int channels) {
    // Downmix a mono copy for analysis before handing the interleaved data
    // to OpenAL (one extra int16 per frame; a five-minute track costs ~26 MB).
    fRate = rate;
    fMono.clear();
    if (channels <= 1) {
      fMono = samples;
    } else {
      fMono.reserve(samples.size() / static_cast<std::size_t>(channels));
      for (std::size_t i = 0; i + static_cast<std::size_t>(channels) <=
                              samples.size();
           i += static_cast<std::size_t>(channels)) {
        int sum = 0;
        for (int c = 0; c < channels; ++c) {
          sum += samples[i + static_cast<std::size_t>(c)];
        }
        fMono.push_back(static_cast<std::int16_t>(sum / channels));
      }
    }

    audio::alGetError(); // clear anything left by an earlier call
    audio::alGenBuffers(1, &fBuffer);
    audio::alBufferData(
        fBuffer, alFormat(channels), samples.data(),
        static_cast<audio::ALsizei>(samples.size() * sizeof(std::int16_t)),
        rate);
    if (alFailed("alBufferData")) {
      return false;
    }
    audio::alGenSources(1, &fSource);
    if (alFailed("alGenSources") || fSource == 0) {
      return false;
    }
    audio::alSourcei(fSource, audio::kBuffer,
                     static_cast<audio::ALint>(fBuffer));
    return !alFailed("alSourcei(buffer)");
  }

  void shutdown() {
    if (fSource != 0) {
      audio::alDeleteSources(1, &fSource);
      fSource = 0;
    }
    if (fBuffer != 0) {
      audio::alDeleteBuffers(1, &fBuffer);
      fBuffer = 0;
    }
    fMono.clear();
    fMono.shrink_to_fit();
    fRate = 0;
  }
};

class SamplePlayer {
public:
  ~SamplePlayer() { this->shutdown(); }

  bool load(const std::filesystem::path &path) {
    const auto ext = detail::lowerExtension(path);
    const auto data = detail::readFile(path);
    return this->load(data, ext);
  }

  bool load(std::span<const std::uint8_t> data, std::string_view ext) {
    if (fBuffer != 0)
      return true;
    if (!audioContext().ok())
      return false;

    int rate = 44100;
    int channels = 2;
    const auto samples = decodePcm(data, ext, rate, channels);
    if (samples.empty())
      return false;

    return this->upload(samples, rate, channels);
  }

  [[nodiscard]] bool loaded() const noexcept { return fBuffer != 0; }

  void setVolume(float gain) {
    for (audio::ALuint source : fSources) {
      audio::alSourcef(source, audio::kGain, gain);
    }
  }

  void play() {
    if (fBuffer == 0 || fSources.empty() || !audioContext().ok())
      return;
    for (audio::ALuint source : fSources) {
      audio::ALint state = audio::kInitial;
      audio::alGetSourcei(source, audio::kSourceState, &state);
      if (state != audio::kPlaying) {
        audio::alSourcei(source, audio::kBuffer,
                         static_cast<audio::ALint>(fBuffer));
        audio::alSourcePlay(source);
        return;
      }
    }
  }

private:
  audio::ALuint fBuffer = 0;
  std::vector<audio::ALuint> fSources;

  bool upload(const std::vector<std::int16_t> &samples, int rate,
              int channels) {
    audio::alGetError();
    audio::alGenBuffers(1, &fBuffer);
    audio::alBufferData(
        fBuffer, alFormat(channels), samples.data(),
        static_cast<audio::ALsizei>(samples.size() * sizeof(std::int16_t)),
        rate);

    if (alFailed("alBufferData(sample)")) {
      return false;
    }
    // A pool per sample lets the same hitsound overlap with itself. If the
    // device will not give the whole pool, take what it will give rather
    // than leaving the sample silent.
    constexpr std::size_t kPoolSize = 8;
    fSources.assign(kPoolSize, 0);
    audio::alGenSources(static_cast<audio::ALsizei>(fSources.size()),
                        fSources.data());
    if (alFailed("alGenSources(pool)")) {
      fSources.assign(1, 0);
      audio::alGenSources(1, fSources.data());
      if (alFailed("alGenSources(single)") || fSources[0] == 0) {
        fSources.clear();
        return false;
      }
    }
    return true;
  }

  void shutdown() {
    if (!fSources.empty()) {
      audio::alDeleteSources(static_cast<audio::ALsizei>(fSources.size()),
                             fSources.data());
      fSources.clear();
    }
    if (fBuffer != 0) {
      audio::alDeleteBuffers(1, &fBuffer);
      fBuffer = 0;
    }
  }
};

} // namespace audio_client
} // namespace client
