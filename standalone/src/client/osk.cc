export module client.osk;

import std;
import client.input;
import platform.keyboard;

export namespace client::osk {
inline void setVisible(bool visible) {
  platform::keyboard::setVisible(visible);
}
inline std::vector<Event> poll() {
  std::vector<Event> result;
  for (const auto &event : platform::keyboard::poll()) {
    result.push_back({event.fWallMs,
                      event.fType == platform::keyboard::EventType::kCharacter
                          ? EventType::kChar
                          : EventType::kKey,
                      event.fCode, event.fAction});
  }
  return result;
}
} // namespace client::osk
