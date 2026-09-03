module;

// Same story as the portal: sd-bus is C, and comes from whichever of the
// three libraries provides it.
#ifdef OSU_HAVE_SDBUS
extern "C" {
#if defined(OSU_SDBUS_BASU)
#include <basu/sd-bus.h>
#elif defined(OSU_SDBUS_ELOGIND)
#include <elogind/sd-bus.h>
#else
#include <systemd/sd-bus.h>
#endif
}
#endif

export module platform.freedesktop.keyboard_backend;

import std;
import platform.clock;
import platform.input;
import platform.keyboard.types;

// The on-screen keyboard, asked for over D-Bus.
//
// There is no standard for this. A Wayland client that speaks text-input-v3
// gets a keyboard raised for it automatically -- that is how KWin and Phosh
// prefer to work -- but GLFW does not expose that protocol, so a client built
// on it has to ask the shell directly. Every shell asks differently.
//
// So this tries the ones that exist, in order, and remembers which answered.
// A machine running none of them gets silence, which is correct: a desktop
// with a hardware keyboard has nothing to raise.
export namespace platform::keyboard::backend {

#ifdef OSU_HAVE_SDBUS

namespace detail {

inline double wallMs() { return platform::clock::milliseconds(); }

inline void appendUtf8(std::vector<Event> &events, std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    std::uint32_t codepoint = first;
    std::size_t count = 1;
    if ((first & 0xe0u) == 0xc0u) {
      codepoint = first & 0x1fu;
      count = 2;
    } else if ((first & 0xf0u) == 0xe0u) {
      codepoint = first & 0x0fu;
      count = 3;
    } else if ((first & 0xf8u) == 0xf0u) {
      codepoint = first & 0x07u;
      count = 4;
    }
    if (i + count > text.size()) {
      break;
    }
    bool valid = count == 1;
    for (std::size_t j = 1; j < count; ++j) {
      const auto next = static_cast<unsigned char>(text[i + j]);
      if ((next & 0xc0u) != 0x80u) {
        valid = false;
        break;
      }
      codepoint = (codepoint << 6u) | (next & 0x3fu);
      valid = true;
    }
    if (valid) {
      events.push_back(
          {wallMs(), EventType::kCharacter, static_cast<std::int32_t>(codepoint)});
    }
    i += count;
  }
}

// Ubuntu Touch's Maliit server does not expose a show/hide method on the
// session bus. The session bus only publishes the address of a private peer
// connection. A text client connects there, exports its input-context object,
// and receives committed text and editing keys back from the keyboard.
class Maliit {
public:
  Maliit() = default;
  Maliit(const Maliit &) = delete;
  Maliit &operator=(const Maliit &) = delete;
  ~Maliit() { this->close(); }

  [[nodiscard]] bool setVisible(bool visible) {
    if (!this->connect()) {
      return false;
    }
    if (visible) {
      if (!this->call("activateContext")) {
        this->close();
        return false;
      }
      if (!this->call("showInputMethod")) {
        this->close();
        return false;
      }
      if (!fAnnounced) {
        std::println(std::cerr, "[osk] using maliit");
        fAnnounced = true;
      }
    } else if (!this->call("hideInputMethod")) {
      this->close();
      return false;
    }
    return true;
  }

  std::vector<Event> poll() {
    if (fPeer != nullptr) {
      int result = 0;
      while ((result = sd_bus_process(fPeer, nullptr)) > 0) {
      }
      if (result < 0) {
        this->close();
      }
    }
    return std::exchange(fEvents, {});
  }

private:
  static int acknowledge(sd_bus_message *message, void *, sd_bus_error *) {
    return sd_bus_reply_method_return(message, "");
  }

  static int commitString(sd_bus_message *message, void *userdata,
                          sd_bus_error *) {
    const char *text = nullptr;
    int replacementStart = 0;
    int replacementLength = 0;
    int cursor = 0;
    const int result = sd_bus_message_read(message, "siii", &text,
                                           &replacementStart,
                                           &replacementLength, &cursor);
    if (result < 0) {
      return result;
    }
    (void)replacementStart;
    (void)replacementLength;
    (void)cursor;
    auto &self = *static_cast<Maliit *>(userdata);
    appendUtf8(self.fEvents, text != nullptr ? std::string_view(text)
                                             : std::string_view{});
    return sd_bus_reply_method_return(message, "");
  }

  static int keyEvent(sd_bus_message *message, void *userdata,
                      sd_bus_error *) {
    int type = 0;
    int key = 0;
    int modifiers = 0;
    const char *text = nullptr;
    int autoRepeat = 0;
    int count = 0;
    std::uint8_t request = 0;
    const int result = sd_bus_message_read(message, "iiisbiy", &type, &key,
                                           &modifiers, &text, &autoRepeat,
                                           &count, &request);
    if (result < 0) {
      return result;
    }
    (void)modifiers;
    (void)text;
    (void)count;
    (void)request;
    // Qt key constants used by Maliit. Text itself arrives through
    // commitString; accepting it here too would type every character twice.
    int glfwKey = 0;
    switch (key) {
    case 0x01000000:
      glfwKey = platform::input::kKeyEscape;
      break;
    case 0x01000003:
      glfwKey = platform::input::kKeyBackspace;
      break;
    case 0x01000004:
    case 0x01000005:
      glfwKey = platform::input::kKeyEnter;
      break;
    case 0x01000007:
      glfwKey = platform::input::kKeyDelete;
      break;
    case 0x01000012:
      glfwKey = platform::input::kKeyLeft;
      break;
    case 0x01000013:
      glfwKey = platform::input::kKeyUp;
      break;
    case 0x01000014:
      glfwKey = platform::input::kKeyRight;
      break;
    case 0x01000015:
      glfwKey = platform::input::kKeyDown;
      break;
    default:
      break;
    }
    if (glfwKey != 0) {
      const int action = type == 7 ? platform::input::kRelease
                                   : (autoRepeat != 0 ? platform::input::kRepeat
                                                      : platform::input::kPress);
      static_cast<Maliit *>(userdata)->fEvents.push_back(
          {wallMs(), EventType::kKey, glfwKey, action});
    }
    return sd_bus_reply_method_return(message, "");
  }

  static int preeditRectangle(sd_bus_message *message, void *, sd_bus_error *) {
    return sd_bus_reply_method_return(message, "biiii", 0, 0, 0, 0, 0);
  }

  static int selection(sd_bus_message *message, void *, sd_bus_error *) {
    return sd_bus_reply_method_return(message, "bs", 0, "");
  }

  inline static const sd_bus_vtable kVtable[] = {
      SD_BUS_VTABLE_START(0),
      SD_BUS_METHOD("activationLostEvent", "", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("imInitiatedHide", "", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("commitString", "siii", "", commitString,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("updatePreedit", "sa(iii)iii", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("keyEvent", "iiisbiy", "", keyEvent,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("updateInputMethodArea", "iiii", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("setGlobalCorrectionEnabled", "b", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("preeditRectangle", "", "biiii", preeditRectangle,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("setRedirectKeys", "b", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("setDetectableAutoRepeat", "b", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("setSelection", "ii", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("selection", "", "bs", selection,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_METHOD("setLanguage", "s", "", acknowledge,
                    SD_BUS_VTABLE_UNPRIVILEGED),
      SD_BUS_VTABLE_END};

  [[nodiscard]] bool connect() {
    if (fPeer != nullptr) {
      return true;
    }
    sd_bus *session = nullptr;
    char *address = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    const int property = sd_bus_open_user(&session) < 0 || session == nullptr
                             ? -1
                             : sd_bus_get_property_string(
                                   session, "org.maliit.server",
                                   "/org/maliit/server/address",
                                   "org.maliit.Server.Address", "address",
                                   &error, &address);
    if (session != nullptr) {
      sd_bus_flush_close_unref(session);
    }
    sd_bus_error_free(&error);
    if (property < 0 || address == nullptr || address[0] == '\0') {
      std::free(address);
      return false;
    }
    int result = sd_bus_new(&fPeer);
    if (result >= 0) {
      result = sd_bus_set_address(fPeer, address);
    }
    if (result >= 0) {
      result = sd_bus_set_bus_client(fPeer, 0);
    }
    if (result >= 0) {
      result = sd_bus_start(fPeer);
    }
    if (result >= 0) {
      result = sd_bus_add_object_vtable(
          fPeer, &fSlot, "/com/meego/inputmethod/inputcontext",
          "com.meego.inputmethod.inputcontext1", kVtable, this);
    }
    std::free(address);
    if (result < 0) {
      this->close();
      return false;
    }
    return true;
  }

  [[nodiscard]] bool call(const char *method) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = nullptr;
    const int result = sd_bus_call_method(
        fPeer, nullptr, "/com/meego/inputmethod/uiserver1",
        "com.meego.inputmethod.uiserver1", method, &error, &reply, "");
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return result >= 0;
  }

  void close() {
    fSlot = sd_bus_slot_unref(fSlot);
    if (fPeer != nullptr) {
      sd_bus_flush_close_unref(fPeer);
      fPeer = nullptr;
    }
  }

  sd_bus *fPeer = nullptr;
  sd_bus_slot *fSlot = nullptr;
  std::vector<Event> fEvents;
  bool fAnnounced = false;
};

inline Maliit &maliit() {
  static Maliit instance;
  return instance;
}

struct Bus {
  sd_bus *fBus = nullptr;
  ~Bus() {
    if (fBus != nullptr) {
      sd_bus_flush_close_unref(fBus);
    }
  }
};

// One shell's way of being asked. Either a method taking a boolean, or a
// pair of methods taking nothing -- KWin spells it enable/disable.
struct Shell {
  const char *fName;
  const char *fService;
  const char *fPath;
  const char *fInterface;
  const char *fShow;
  const char *fHide; // null when fShow takes a boolean instead
};

// KWin covers Plasma and Plasma Mobile; squeekboard covers Phosh, which is
// what postmarketOS and the Librem ship. Maliit is left out deliberately:
// its interface is a socket address handed over for an input method to
// connect to, not a request to show a keyboard, and pretending otherwise
// would be worse than not trying.
inline constexpr Shell kShells[] = {
    {"kwin", "org.kde.KWin", "/VirtualKeyboard", "org.kde.kwin.VirtualKeyboard",
     "enable", "disable"},
    {"squeekboard", "sm.puri.OSK0", "/sm/puri/OSK0", "sm.puri.OSK0",
     "SetVisible", nullptr},
};

// Which one answered last time. -1 is "not asked yet", -2 is "none of them".
inline int &known() {
  static int index = -1;
  return index;
}

[[nodiscard]] inline bool ask(sd_bus *bus, const Shell &shell, bool visible) {
  const char *method = visible ? shell.fShow : shell.fHide;
  sd_bus_error error = SD_BUS_ERROR_NULL;
  sd_bus_message *reply = nullptr;
  int r = 0;
  if (shell.fHide == nullptr) {
    r = sd_bus_call_method(bus, shell.fService, shell.fPath, shell.fInterface,
                           shell.fShow, &error, &reply, "b",
                           visible ? 1 : 0);
  } else {
    r = sd_bus_call_method(bus, shell.fService, shell.fPath, shell.fInterface,
                           method, &error, &reply, "");
  }
  if (reply != nullptr) {
    sd_bus_message_unref(reply);
  }
  sd_bus_error_free(&error);
  return r >= 0;
}

} // namespace detail

// Blocking, but only as far as a D-Bus round trip to the session bus, which
// is a local socket. Called when focus enters or leaves a text field, not
// per frame.
inline void setVisible(bool visible) {
  if (detail::maliit().setVisible(visible)) {
    return;
  }
  detail::Bus bus;
  if (sd_bus_open_user(&bus.fBus) < 0 || bus.fBus == nullptr) {
    return;
  }
  int &known = detail::known();
  if (known >= 0) {
    if (!detail::ask(bus.fBus, detail::kShells[known], visible)) {
      known = -1; // it stopped answering; look again next time
    }
    return;
  }
  if (known == -2) {
    return;
  }
  for (int i = 0; i < static_cast<int>(std::size(detail::kShells)); ++i) {
    if (detail::ask(bus.fBus, detail::kShells[i], visible)) {
      known = i;
      std::println(std::cerr, "[osk] using {}", detail::kShells[i].fName);
      return;
    }
  }
  known = -2;
  std::println(std::cerr,
               "[osk] no on-screen keyboard service answered; typing needs a "
               "hardware keyboard here");
}

inline std::vector<Event> poll() { return detail::maliit().poll(); }

#else

inline void setVisible(bool) {} // built without D-Bus
inline std::vector<Event> poll() { return {}; }

#endif

} // namespace platform::keyboard::backend
