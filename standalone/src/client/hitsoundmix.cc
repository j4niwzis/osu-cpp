export module client.hitsoundmix;

import std;
import osu;
import skin;
import platform.audio_engine;
import client.util;

export namespace client {

namespace hitsound_detail {

[[nodiscard]] const char *sampleSetName(const osu::SampleSet &set) noexcept {
  return std::visit(
      osu::Overloaded{
          [](osu::sampleSet::None) -> const char * { return nullptr; },
          [](osu::sampleSet::Normal) -> const char * { return "normal"; },
          [](osu::sampleSet::Soft) -> const char * { return "soft"; },
          [](osu::sampleSet::Drum) -> const char * { return "drum"; }},
      set);
}

[[nodiscard]] const char *sampleSetNameOrDefault(const osu::SampleSet &set,
                                                 double time,
                                                 const osu::Beatmap &map) {
  if (const char *name = sampleSetName(set)) {
    return name;
  }
  if (const auto *timing = map.activeTiming(time)) {
    if (const char *name = sampleSetName(timing->fSet)) {
      return name;
    }
  }
  return "normal";
}

[[nodiscard]] std::vector<std::string>
sampleNames(double time, osu::HitSound sound, const osu::HitSample &sample,
            const osu::Beatmap &map) {
  const std::string normal = sampleSetNameOrDefault(sample.fNormalSet, time, map);
  const std::string addition =
      sampleSetNameOrDefault(sample.fAdditionSet, time, map);
  std::vector<std::string> names{normal + "-hitnormal"};
  if ((sound & osu::HitSound::kWhistle) != osu::HitSound::kNone) {
    names.push_back(addition + "-hitwhistle");
  }
  if ((sound & osu::HitSound::kFinish) != osu::HitSound::kNone) {
    names.push_back(addition + "-hitfinish");
  }
  if ((sound & osu::HitSound::kClap) != osu::HitSound::kNone) {
    names.push_back(addition + "-hitclap");
  }
  return names;
}

[[nodiscard]] platform::sound::DecodedAudio
loadSample(std::string_view name, const osu::BeatmapSet &set,
           const Skin &skin) {
  for (const std::string_view extension : {".wav", ".ogg"}) {
    const std::string key = std::string(name) + std::string(extension);
    const auto bytes = set.findFile(key);
    if (!bytes.empty()) {
      return platform::sound::decodeAudio(bytes, extension);
    }
  }
  for (const std::string_view extension : {".wav", ".ogg"}) {
    const auto path = skin.root() /
                      (std::string(name) + std::string(extension));
    if (!std::filesystem::exists(path)) {
      continue;
    }
    return platform::sound::decodeAudio(detail::readFile(path), extension);
  }
  return {};
}

void addPcm(std::vector<float> &mix, int outputRate, std::size_t atFrame,
            const platform::sound::DecodedAudio &sample, float gain) {
  if (sample.fSamples.empty() || sample.fRate <= 0 ||
      sample.fChannels <= 0 || outputRate <= 0) {
    return;
  }
  const std::size_t sourceFrames =
      sample.fSamples.size() / static_cast<std::size_t>(sample.fChannels);
  const std::size_t outputFrames = static_cast<std::size_t>(std::ceil(
      static_cast<double>(sourceFrames) * outputRate / sample.fRate));
  if (mix.size() < (atFrame + outputFrames) * 2) {
    mix.resize((atFrame + outputFrames) * 2, 0.0f);
  }
  for (std::size_t frame = 0; frame < outputFrames; ++frame) {
    const std::size_t source = std::min(
        sourceFrames - 1,
        static_cast<std::size_t>(static_cast<double>(frame) * sample.fRate /
                                 outputRate));
    for (int channel = 0; channel < 2; ++channel) {
      const int sourceChannel = sample.fChannels == 1
                                    ? 0
                                    : std::min(channel, sample.fChannels - 1);
      const std::size_t index =
          source * static_cast<std::size_t>(sample.fChannels) +
          static_cast<std::size_t>(sourceChannel);
      mix[(atFrame + frame) * 2 + static_cast<std::size_t>(channel)] +=
          static_cast<float>(sample.fSamples[index]) * gain;
    }
  }
}

void writeLe16(std::ostream &out, std::uint16_t value) {
  out.put(static_cast<char>(value & 0xff));
  out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeLe32(std::ostream &out, std::uint32_t value) {
  writeLe16(out, static_cast<std::uint16_t>(value & 0xffff));
  writeLe16(out, static_cast<std::uint16_t>(value >> 16));
}

[[nodiscard]] bool writeWav(const std::filesystem::path &path,
                            std::span<const float> samples, int rate) {
  if (samples.empty() || rate <= 0 ||
      samples.size() > std::numeric_limits<std::uint32_t>::max() / 2u) {
    return false;
  }
  std::ofstream out(path, std::ios::binary);
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * 2u);
  out.write("RIFF", 4);
  writeLe32(out, 36u + dataBytes);
  out.write("WAVEfmt ", 8);
  writeLe32(out, 16u);
  writeLe16(out, 1u);
  writeLe16(out, 2u);
  writeLe32(out, static_cast<std::uint32_t>(rate));
  writeLe32(out, static_cast<std::uint32_t>(rate * 4));
  writeLe16(out, 4u);
  writeLe16(out, 16u);
  out.write("data", 4);
  writeLe32(out, dataBytes);
  for (const float sample : samples) {
    const auto value = static_cast<std::int16_t>(
        std::clamp(std::lrint(sample), -32768l, 32767l));
    writeLe16(out, static_cast<std::uint16_t>(value));
  }
  return static_cast<bool>(out);
}

} // namespace hitsound_detail

// Builds the soundtrack used by offline replay rendering. OpenAL is a live
// device mixer and has no deterministic capture path, so exported video mixes
// the decoded music and exactly the successful replay judgements in memory.
[[nodiscard]] std::optional<std::filesystem::path>
mixReplayAudio(std::span<const std::uint8_t> musicBytes,
               std::string_view musicExtension, const osu::Beatmap &map,
               std::span<const osu::InputEvent> inputs, osu::ModSet mods,
               osu::RuleSet rules, const osu::BeatmapSet &set,
               const Skin &skin, float musicGain, float effectGain,
               const std::filesystem::path &output) {
  auto music = platform::sound::decodeAudio(musicBytes, musicExtension);
  if (music.fSamples.empty() || music.fRate <= 0 || music.fChannels <= 0) {
    return std::nullopt;
  }

  std::vector<float> mixed;
  hitsound_detail::addPcm(mixed, music.fRate, 0, music, musicGain);

  osu::Engine engine(map, mods, rules);
  for (const auto &input : inputs) {
    engine.submit(input);
  }
  engine.advance(map.lastObjectEndTime() + 1500.0);

  std::unordered_map<std::string, platform::sound::DecodedAudio> samples;
  bool addedHitsound = false;
  for (const auto &event : engine.events()) {
    if (event.fKind != osu::HitKind::kBasic ||
        std::holds_alternative<osu::judgement::Miss>(event.fResult) ||
        event.fIndex >= map.fObjects.size()) {
      continue;
    }
    std::visit(
        [&](const auto &object) {
          double at = osu::startTime(map.fObjects[event.fIndex]);
          if constexpr (std::same_as<std::remove_cvref_t<decltype(object)>,
                                    osu::Spinner>) {
            at = object.fEnd;
          }
          const auto names = hitsound_detail::sampleNames(
              at, object.fSound, object.fSample, map);
          for (const auto &name : names) {
            auto [sample, inserted] = samples.try_emplace(name);
            if (inserted) {
              sample->second = hitsound_detail::loadSample(name, set, skin);
            }
            if (sample->second.fSamples.empty()) {
              continue;
            }
            const std::size_t frame = static_cast<std::size_t>(
                std::max(0.0, at) * music.fRate / 1000.0);
            hitsound_detail::addPcm(mixed, music.fRate, frame, sample->second,
                                    effectGain);
            addedHitsound = true;
          }
        },
        map.fObjects[event.fIndex]);
  }

  if (!addedHitsound || !hitsound_detail::writeWav(output, mixed, music.fRate)) {
    return std::nullopt;
  }
  return output;
}

} // namespace client
