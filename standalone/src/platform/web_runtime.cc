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
inline void setCursorVisible(bool) {}
inline void wantPointerLock(bool) {}
inline void runMainLoop(FrameCallback, void *) {}
inline void cancelMainLoop() {}
[[nodiscard]] inline CanvasExtent canvasExtent() { return {}; }

} // namespace platform::web
