export module platform.keyboard;

export import platform.keyboard.types;
import platform.keyboard.backend;
import std;

export namespace platform::keyboard {
inline void setVisible(bool visible) { backend::setVisible(visible); }
inline std::vector<Event> poll() { return backend::poll(); }
} // namespace platform::keyboard
