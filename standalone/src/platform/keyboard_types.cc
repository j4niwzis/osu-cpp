export module platform.keyboard.types;

import std;

export namespace platform::keyboard {
enum class EventType : std::uint8_t { kCharacter, kKey };
struct Event {
  double fWallMs = 0.0;
  EventType fType = EventType::kCharacter;
  std::int32_t fCode = 0;
  std::int32_t fAction = 0;
};
} // namespace platform::keyboard
