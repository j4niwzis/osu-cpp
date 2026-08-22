export module client.judgements;

import std;
import osu;
import archive;
import skin;
import client.audio;
import client.gameplayview;
import client.hitsoundmix;

export namespace client {

class JudgementPresenter {
public:
  void reset() {
    fPlayedEvents = 0;
    fCombo = 0;
  }

  void setGain(float gain) {
    fGain = gain;
    for (auto &[name, player] : fSamples) {
      (void)name;
      player.setVolume(gain);
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
  void playSample(const std::string &name, const osu::BeatmapSet &set,
                  const Skin &skin) {
    if (name.empty()) {
      return;
    }
    for (const std::string_view extension : {".wav", ".ogg"}) {
      const std::string key = name + std::string(extension);
      const auto bytes = set.findFile(key);
      if (!bytes.empty()) {
        auto &player = fSamples[key];
        if (!player.loaded()) {
          player.load(bytes, std::string(extension));
          player.setVolume(fGain);
        }
        player.play();
        return;
      }
    }
    for (const std::string_view extension : {".wav", ".ogg"}) {
      const auto path = skin.root() / (name + std::string(extension));
      if (!std::filesystem::exists(path)) {
        continue;
      }
      auto &player = fSamples[path.string()];
      if (!player.loaded()) {
        player.load(path);
        player.setVolume(fGain);
      }
      player.play();
      return;
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

  std::unordered_map<std::string, audio_client::SamplePlayer> fSamples;
  std::size_t fPlayedEvents = 0;
  int fCombo = 0;
  float fGain = 1.0f;
};

} // namespace client
