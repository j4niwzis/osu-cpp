export module platform.web_runtime;

import std;

export namespace platform::web {

using FrameCallback = void (*)(void *);
struct CanvasExtent { int fWidth = 1280; int fHeight = 720; };

inline void initializeMapStorage() {}
[[nodiscard]] inline bool mapStorageReady() { return true; }
inline void syncMapStorage() {}
inline void requestBeatmapArchive() {}
[[nodiscard]] inline std::vector<std::string> takePendingImports() {
  return {};
}
inline void lockOrientation(int) {}
[[nodiscard]] inline bool offerDownload(const std::string &) { return false; }
inline void setCursorVisible(bool) {}
inline void wantPointerLock(bool) {}
inline void runMainLoop(FrameCallback, void *) {}
inline void cancelMainLoop() {}
[[nodiscard]] inline CanvasExtent canvasExtent() { return {}; }
// How far behind the reported position the listener hears it. Only a browser
// has a number to give here; a desktop device reports what it is playing,
// within the few milliseconds nobody sees on a visualiser.
[[nodiscard]] inline double outputLatencySec() { return 0.0; }

} // namespace platform::web
