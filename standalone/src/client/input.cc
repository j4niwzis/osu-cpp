export module client.input;

import std;

export namespace client {

// Raw input event, timestamped on the event-pump (main) thread the moment
// The platform backend delivers it. Wall time is platform::clock::milliseconds(); the render thread maps
// it onto the game timeline with client::AnchoredClock when consuming.
enum class EventType : std::uint8_t {
  kKey,
  kMouseButton,
  kCursorMove,
  kScroll,
  kResize,
  kChar,
  kWindowVisible,
};

struct Event {
  double fWallMs = 0.0;
  EventType fType = EventType::kKey;
  std::int32_t fA = 0; // key/button/action or width
  std::int32_t fB = 0; // action or height
  float fX = 0.0f;     // cursor x / scroll delta
  float fY = 0.0f;     // cursor y
};

// Single-producer single-consumer lock-free ring buffer.
//
// Producer: the GLFW event-pump (main) thread. Consumer: the render thread.
// Head/tail are monotonically increasing; the slot index is masked. Indices
// live on separate cache lines so producer and consumer do not false-share.
template <std::size_t N> class SpscQueue {
  static_assert((N & (N - 1)) == 0, "capacity must be a power of two");

public:
  // Returns false when full (event dropped; producer never blocks).
  bool tryPush(const Event &ev) noexcept {
    const std::size_t head = fHead.load(std::memory_order_relaxed);
    const std::size_t tail = fTail.load(std::memory_order_acquire);
    if (head - tail >= N) {
      fDropped.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    fSlots[head & (N - 1)] = ev;
    fHead.store(head + 1, std::memory_order_release);
    return true;
  }

  bool tryPop(Event &out) noexcept {
    const std::size_t tail = fTail.load(std::memory_order_relaxed);
    const std::size_t head = fHead.load(std::memory_order_acquire);
    if (tail == head) {
      return false;
    }
    out = fSlots[tail & (N - 1)];
    fTail.store(tail + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t dropped() const noexcept {
    return fDropped.load(std::memory_order_relaxed);
  }

private:
  // Avoid std::hardware_destructive_interference_size: keep the constant
  // local so the module builds identically everywhere.
  static constexpr std::size_t kCacheLine = 64;

  alignas(kCacheLine) std::atomic<std::size_t> fHead{0};
  alignas(kCacheLine) std::atomic<std::size_t> fTail{0};
  alignas(kCacheLine) std::atomic<std::size_t> fDropped{0};
  std::array<Event, N> fSlots{};
};

} // namespace client
