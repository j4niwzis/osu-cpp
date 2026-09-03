export module client.judgements;

import std;
import osu;
import archive;
import skin;
import platform.audio_engine;
import client.gameplayview;
import client.hitsoundmix;

export namespace client {

class JudgementPresenter {
public:
  void reset() {
    fPlayedEvents = 0;
    fCombo = 0;
    fPlayed = 0;
    fSilentBusy = 0;
    fSilentUnplayable = 0;
    fMissing = 0;
  }

  // What the last play sounded like, in numbers. A map whose hitsounds go
  // quiet says here whether the samples were not found, not loaded, or found
  // and loaded and left with no free source to play through.
  void reportSounds() const {
    if (fSilentBusy == 0 && fSilentUnplayable == 0 && fMissing == 0) {
      return;
    }
    std::println(std::cerr,
                 "[hitsound] {} played, {} had no free source, {} could not "
                 "be loaded, {} not found; {} samples held",
                 fPlayed, fSilentBusy, fSilentUnplayable, fMissing,
                 fSetSamples.size() + fSkinSamples.size());
  }

  void setGain(float gain) {
    fGain = gain;
    for (auto *cache : {&fSetSamples, &fSkinSamples}) {
      for (auto &[name, player] : *cache) {
        (void)name;
        player.setVolume(gain);
      }
    }
  }

  void process(double now, const osu::Engine &engine,
               const osu::Beatmap &map, const osu::ComboInfo &combo,
               GameplayView &view, const osu::BeatmapSet &set,
               const Skin &skin) {
    const auto &events = engine.events();
    while (fPlayedEvents < events.size()) {
      const auto &event = events[fPlayedEvents++];
      const int previous = fCombo;
      this->useSet(set);
      fCombo = engine.score().fCombo;
      view.setCombo(fCombo);
      if (event.fKind != osu::HitKind::kBasic) {
        if (event.fKind == osu::HitKind::kLargeTick ||
            event.fKind == osu::HitKind::kSliderTail) {
          const bool tail = event.fKind == osu::HitKind::kSliderTail;
          const bool hit = !std::holds_alternative<osu::judgement::Miss>(
              event.fResult);
          const double when =
              tail && event.fIndex < map.fObjects.size()
                  ? osu::objectEnd(map.fObjects[event.fIndex], map).second
                  : now;
          view.noteSliderNested(event.fIndex, tail, hit, when);
        }
        continue;
      }

      const osu::Vec2 position =
          event.fIndex < map.fObjects.size()
              ? osu::objectPosition(map.fObjects[event.fIndex])
              : osu::kPlayfieldCenter;
      const bool counts =
          !std::holds_alternative<osu::judgement::Miss>(event.fResult) &&
          event.fIndex < combo.fIndices.size();
      view.addJudgement(event.fResult, event.fIndex, position, now,
                        counts ? combo.fIndices[event.fIndex] : 0, counts);
      if (std::holds_alternative<osu::judgement::Miss>(event.fResult)) {
        if (previous > 20) {
          this->playSample("combobreak", set, skin);
        }
        continue;
      }
      const double hitTime =
          event.fIndex < map.fObjects.size()
              ? osu::startTime(map.fObjects[event.fIndex])
              : now;
      this->playObject(hitTime, event.fIndex, map, set, skin);
    }
  }

private:
  // The samples a beatmap carries are cached under the name they have inside
  // it -- "normal-hitnormal.ogg" and nothing more -- so the cache belongs to
  // the set and is dropped when the set changes. Kept across sets, it would
  // both answer the next map with the previous map's sounds and hold every
  // set's sources open at once, which is how a device runs out of them.
  //
  // Skin samples are cached under their path, which is unambiguous, so they
  // survive.
  void useSet(const osu::BeatmapSet &set) {
    if (&set == fSetOfSamples) {
      return;
    }
    fSetOfSamples = &set;
    fSetSamples.clear();
  }

  void playSample(const std::string &name, const osu::BeatmapSet &set,
                  const Skin &skin) {
    if (name.empty()) {
      return;
    }
    for (const std::string_view extension : {".wav", ".ogg"}) {
      const std::string key = name + std::string(extension);
      const auto bytes = set.findFile(key);
      if (!bytes.empty()) {
        auto &player = fSetSamples[key];
        if (!player.loaded()) {
          player.load(bytes, std::string(extension));
          player.setVolume(fGain);
        }
        this->start(player);
        return;
      }
    }
    for (const std::string_view extension : {".wav", ".ogg"}) {
      const auto path = skin.root() / (name + std::string(extension));
      if (!std::filesystem::exists(path)) {
        continue;
      }
      auto &player = fSkinSamples[path.string()];
      if (!player.loaded()) {
        player.load(path);
        player.setVolume(fGain);
      }
      this->start(player);
      return;
    }
    ++fMissing;
  }

  void start(platform::sound::SamplePlayer &player) {
    if (!player.playable()) {
      ++fSilentUnplayable;
    } else if (player.play()) {
      ++fPlayed;
    } else {
      ++fSilentBusy;
    }
  }

  void playHit(double time, osu::HitSound sound,
               const osu::HitSample &sample, const osu::Beatmap &map,
               const osu::BeatmapSet &set, const Skin &skin) {
    for (const auto &name :
         hitsound_detail::sampleNames(time, sound, sample, map)) {
      this->playSample(name, set, skin);
    }
  }

  void playObject(double time, std::size_t index, const osu::Beatmap &map,
                  const osu::BeatmapSet &set, const Skin &skin) {
    if (index >= map.fObjects.size()) {
      return;
    }
    std::visit(osu::Overloaded{
                   [this, time, &map, &set, &skin](const osu::Circle &object) {
                     this->playHit(time, object.fSound, object.fSample, map, set,
                                   skin);
                   },
                   [this, time, &map, &set, &skin](const osu::Slider &object) {
                     this->playHit(time, object.fSound, object.fSample, map, set,
                                   skin);
                   },
                   [this, time, &map, &set, &skin](const osu::Spinner &object) {
                     this->playHit(time, object.fSound, object.fSample, map, set,
                                   skin);
                   },
               },
               map.fObjects[index]);
  }

  std::unordered_map<std::string, platform::sound::SamplePlayer> fSetSamples;
  std::unordered_map<std::string, platform::sound::SamplePlayer> fSkinSamples;
  const osu::BeatmapSet *fSetOfSamples = nullptr;
  std::size_t fPlayedEvents = 0;
  int fCombo = 0;
  float fGain = 1.0f;
  std::uint64_t fPlayed = 0;
  std::uint64_t fSilentBusy = 0;
  std::uint64_t fSilentUnplayable = 0;
  std::uint64_t fMissing = 0;
};

} // namespace client
