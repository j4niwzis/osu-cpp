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

export module client.osk;

import std;

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
export namespace client::osk {

#ifdef OSU_HAVE_SDBUS

namespace detail {

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

#else

inline void setVisible(bool) {} // built without D-Bus

#endif

} // namespace client::osk
