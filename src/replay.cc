export module osu.replay;

import std;
import osu.types;
import osu.rules;
import osu.beatmap;
import osu.engine;
import osu.autopilot;

export namespace osu {

struct SimulationResult {
  ScoreState fScore;
  std::vector<HitEvent> fEvents;
};

[[nodiscard]] inline SimulationResult
runSimulation(const Beatmap &map, std::span<const InputEvent> inputs,
              ModSet mods = mod::kNone) {
  Engine engine(map, mods);
  for (const auto &ev : inputs) {
    engine.submit(ev);
  }
  engine.advance(map.lastObjectEndTime() + 500.0);
  return {engine.score(), std::vector<HitEvent>(engine.events().begin(),
                                                engine.events().end())};
}

[[nodiscard]] inline SimulationResult runAutoplay(const Beatmap &map,
                                                  ModSet mods = mod::kNone) {
  return runSimulation(map, buildAutoplay(map, mods), mods);
}

} // namespace osu
