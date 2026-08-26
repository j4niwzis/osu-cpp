export module client.appplayback;

import std;
import osu;
import platform.clock;
import client.input;
import client.playresult;

export namespace client {

// Owns the application policy around a running play: its audio-backed clock,
// replay input/output, and mapping window input into playfield coordinates.
// Host remains a compile-time policy, matching the other App controllers.
template <class Host> class AppPlayback {
public:
  explicit AppPlayback(Host &app) : fApp(app) {}

  void resetClockSync(
      double wall = std::numeric_limits<double>::lowest()) noexcept {
    fLastClockSyncWall = wall;
  }

  [[nodiscard]] double nowMs() {
#ifdef __EMSCRIPTEN__
    return platform::clock::milliseconds() - fApp.fPlay.fStartMs +
           fApp.fPlay.fAudioOffsetMs;
#else
    // Consult the audio device occasionally; extrapolate from the anchored
    // clock between those potentially blocking device queries.
    const double wall = fApp.wallMs();
    if (wall - fLastClockSyncWall >= kClockSyncIntervalMs) {
      fLastClockSyncWall = wall;
      if (fApp.fAudio.playing()) {
        fApp.fPlay.fClock.sync(wall, fApp.fAudio.positionSec() * 1000.0 +
                                        fApp.fPlay.fAudioOffsetMs);
      }
    }
    return fApp.fPlay.fClock.sample(wall);
#endif
  }

  [[nodiscard]] bool shouldStop(double now) const {
    return fApp.fPlay.fEngine->finished() &&
           now > fApp.fPlay.fMap->lastObjectEndTime() + 1000.0;
  }

  void submitAutoplay(double now) {
    if (!fApp.fAutoplay) {
      return;
    }
    while (fApp.fAutoplayIndex < fApp.fPlay.fAutoplayEvents.size() &&
           fApp.fPlay.fAutoplayEvents[fApp.fAutoplayIndex].fTime <= now) {
      const auto &event = fApp.fPlay.fAutoplayEvents[fApp.fAutoplayIndex];
      fApp.fPlay.fEngine->submit(event);
      if (fApp.fReplayPath.empty()) {
        fApp.fPlay.fRecordedEvents.push_back(event);
      }
      if (event.fAction == osu::InputAction::kMove) {
        fApp.fCursor = event.fPos;
        fApp.fView.addTrailPoint(fApp.fCursor, event.fTime);
      }
      ++fApp.fAutoplayIndex;
    }
  }

  [[nodiscard]] osu::Vec2 cursorFromEvent(const Event &event) {
    const auto raw = this->toPlayfield(event.fX, event.fY);
    if (!fApp.relativeCursor()) {
      fApp.fVirtualCursor = raw;
      fApp.fHasRawPrev = false;
      return raw;
    }
    const double sensitivity = fApp.fSettings.value("sensitivity");
    if (!fApp.fHasRawPrev) {
      fApp.fRawPrev = raw;
      fApp.fHasRawPrev = true;
    }
    const osu::Vec2 delta{(raw.fX - fApp.fRawPrev.fX) * sensitivity,
                          (raw.fY - fApp.fRawPrev.fY) * sensitivity};
    fApp.fRawPrev = raw;

    // Relative input is confined to the window mapped into playfield space,
    // not to the playfield itself, so low sensitivity creates no inner wall.
    const osu::Vec2 lo = this->toPlayfield(0.0f, 0.0f);
    const osu::Vec2 hi = this->toPlayfield(
        static_cast<float>(fApp.fWin.fScreenW),
        static_cast<float>(fApp.fWin.fScreenH));
    fApp.fVirtualCursor = {
        std::clamp(fApp.fVirtualCursor.fX + delta.fX, lo.fX, hi.fX),
        std::clamp(fApp.fVirtualCursor.fY + delta.fY, lo.fY, hi.fY)};
    return fApp.fVirtualCursor;
  }

  void printResult() const {
    if (fApp.fPlay.fEngine) {
      client::printResult(*fApp.fPlay.fEngine,
                          fApp.fWindowRuntime.droppedInput());
    }
  }

  [[nodiscard]] std::string beatmapMd5() const {
    return client::beatmapMd5(fApp.fSet, fApp.fBeatmapFilename);
  }

  void saveReplay() {
    if (!fApp.fPlay.fMap || !fApp.fPlay.fEngine ||
        !fApp.fReplayPath.empty()) {
      return;
    }
    const auto saved = client::saveReplay(
        fApp.fPlay.fRecordedEvents, *fApp.fPlay.fMap, *fApp.fPlay.fEngine,
        this->beatmapMd5(), fApp.fMods, fApp.fReplayDir);
    if (!saved) {
      return;
    }
    fApp.fPlay.fLastSavedReplay = *saved;
    fApp.fReplayBrowser.add(
        *saved, fApp.fPlay.fEngine->rules() == osu::RuleSet::kLegacyClient
                    ? 1
                    : 0);
    std::println(std::cerr, "[replay] saved {}", saved->string());
  }

private:
  [[nodiscard]] osu::Vec2 toPlayfield(float x, float y) const {
    return {(static_cast<double>(x) - fApp.fOffsetX) / fApp.fScale,
            (static_cast<double>(y) - fApp.fOffsetY) / fApp.fScale};
  }

  static constexpr double kClockSyncIntervalMs = 250.0;
  Host &fApp;
  double fLastClockSyncWall = std::numeric_limits<double>::lowest();
};

} // namespace client
