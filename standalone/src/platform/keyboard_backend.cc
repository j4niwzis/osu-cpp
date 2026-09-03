export module platform.keyboard.backend;

import std;
import platform.keyboard.types;

export namespace platform::keyboard::backend {
inline void setVisible(bool) {}
inline std::vector<Event> poll() { return {}; }
} // namespace platform::keyboard::backend
