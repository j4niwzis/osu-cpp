module;

// sd-bus is C, and D-Bus is POSIX territory: both come in through the global
// fragment rather than through import std. Without libsystemd the module
// still builds and every call answers "no portal here", so the caller falls
// back to whatever dialog binaries the machine has.
#ifdef OSU_HAVE_SDBUS
extern "C" {
#include <systemd/sd-bus.h>
}
#endif

export module client.portal;

import std;

// xdg-desktop-portal: the desktop's own file dialog, asked for over D-Bus.
//
// Three reasons this exists rather than shelling out to zenity. Inside a
// Flatpak it is the only way to reach a file the sandbox does not already
// grant -- the portal hands back a document the app is then allowed to open.
// Outside one it still shows the dialog that belongs to the desktop, GTK on
// GNOME and Qt on KDE, instead of whichever binary happens to be installed.
// And it needs nothing linked but D-Bus itself.
export namespace client::portal {

#ifdef OSU_HAVE_SDBUS

namespace detail {

// "file:///home/user/My%20Maps/x.osz" -> the path it names. The portal only
// ever returns the file:// scheme, and percent-encodes everything outside
// the unreserved set.
[[nodiscard]] inline std::filesystem::path fromFileUri(std::string_view uri) {
  constexpr std::string_view kScheme = "file://";
  if (uri.starts_with(kScheme)) {
    uri.remove_prefix(kScheme.size());
  }
  std::string out;
  out.reserve(uri.size());
  for (std::size_t i = 0; i < uri.size(); ++i) {
    if (uri[i] != '%' || i + 2 >= uri.size()) {
      out.push_back(uri[i]);
      continue;
    }
    const auto hex = [](char c) -> int {
      if (c >= '0' && c <= '9') {
        return c - '0';
      }
      if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
      }
      if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
      }
      return -1;
    };
    const int hi = hex(uri[i + 1]);
    const int lo = hex(uri[i + 2]);
    if (hi < 0 || lo < 0) {
      out.push_back(uri[i]);
      continue;
    }
    out.push_back(static_cast<char>(hi * 16 + lo));
    i += 2;
  }
  return std::filesystem::path(out);
}

struct Answer {
  bool fArrived = false;
  std::optional<std::filesystem::path> fPath;
};

// org.freedesktop.portal.Request::Response(u response, a{sv} results).
// A response of 0 is a choice; 1 is the user cancelling and 2 is the request
// ending some other way, and neither carries a file.
inline int onResponse(sd_bus_message *m, void *userdata, sd_bus_error *) {
  auto *answer = static_cast<Answer *>(userdata);
  answer->fArrived = true;

  std::uint32_t response = 2;
  if (sd_bus_message_read(m, "u", &response) < 0 || response != 0) {
    return 0;
  }
  if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) {
    return 0;
  }
  while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
    const char *key = nullptr;
    if (sd_bus_message_read(m, "s", &key) < 0 || key == nullptr) {
      sd_bus_message_exit_container(m);
      break;
    }
    if (std::string_view(key) == "uris") {
      if (sd_bus_message_enter_container(m, 'v', "as") > 0) {
        if (sd_bus_message_enter_container(m, 'a', "s") > 0) {
          const char *uri = nullptr;
          // Only the first: this asks for one file and never sets multiple.
          if (sd_bus_message_read(m, "s", &uri) > 0 && uri != nullptr) {
            answer->fPath = fromFileUri(uri);
          }
          sd_bus_message_exit_container(m);
        }
        sd_bus_message_exit_container(m);
      }
    } else {
      sd_bus_message_skip(m, "v");
    }
    sd_bus_message_exit_container(m);
  }
  sd_bus_message_exit_container(m);
  return 0;
}

struct Bus {
  sd_bus *fBus = nullptr;
  ~Bus() {
    if (fBus != nullptr) {
      sd_bus_flush_close_unref(fBus);
    }
  }
};

struct Slot {
  sd_bus_slot *fSlot = nullptr;
  ~Slot() {
    if (fSlot != nullptr) {
      sd_bus_slot_unref(fSlot);
    }
  }
};

struct Message {
  sd_bus_message *fMessage = nullptr;
  ~Message() {
    if (fMessage != nullptr) {
      sd_bus_message_unref(fMessage);
    }
  }
};

} // namespace detail

// Blocks until the user answers, so it belongs on a worker rather than on the
// thread that draws. Returns nothing when the user cancelled, and nothing
// when there is no portal to ask -- the caller cannot tell those apart and
// does not need to: falling back to a local dialog after a cancellation
// simply asks again, and the second dialog is the one the user closes.
[[nodiscard]] inline std::optional<std::filesystem::path>
openArchive(const std::string &title) {
  detail::Bus bus;
  if (sd_bus_open_user(&bus.fBus) < 0 || bus.fBus == nullptr) {
    return std::nullopt;
  }

  const char *unique = nullptr;
  if (sd_bus_get_unique_name(bus.fBus, &unique) < 0 || unique == nullptr) {
    return std::nullopt;
  }
  // The reply's object path is predictable: the caller's unique name with the
  // leading ':' removed and every '.' turned into '_', plus the token handed
  // over below. Knowing it in advance is what lets the match be in place
  // before the call is made, so the answer cannot arrive first and be missed.
  std::string sender(unique[0] == ':' ? unique + 1 : unique);
  std::ranges::replace(sender, '.', '_');

  static std::atomic<unsigned> sCounter{0};
  const std::string token = std::format(
      "osu_client_{}", sCounter.fetch_add(1, std::memory_order_relaxed));
  const std::string expected = std::format(
      "/org/freedesktop/portal/desktop/request/{}/{}", sender, token);

  detail::Answer answer;
  detail::Slot slot;
  if (sd_bus_match_signal(bus.fBus, &slot.fSlot,
                          "org.freedesktop.portal.Desktop", expected.c_str(),
                          "org.freedesktop.portal.Request", "Response",
                          detail::onResponse, &answer) < 0) {
    return std::nullopt;
  }

  detail::Message call;
  if (sd_bus_message_new_method_call(
          bus.fBus, &call.fMessage, "org.freedesktop.portal.Desktop",
          "/org/freedesktop/portal/desktop",
          "org.freedesktop.portal.FileChooser", "OpenFile") < 0) {
    return std::nullopt;
  }
  // parent_window is left empty: tying the dialog to this window means
  // exporting a handle for it (xdg-foreign on Wayland, the XID on X11), and
  // an unparented dialog is a smaller thing to be wrong about.
  if (sd_bus_message_append(call.fMessage, "ss", "", title.c_str()) < 0) {
    return std::nullopt;
  }

  const auto fail = [](int r) { return r < 0; };
  int r = sd_bus_message_open_container(call.fMessage, 'a', "{sv}");
  if (fail(r)) {
    return std::nullopt;
  }
  r = sd_bus_message_open_container(call.fMessage, 'e', "sv");
  if (fail(r) ||
      fail(sd_bus_message_append(call.fMessage, "s", "handle_token")) ||
      fail(sd_bus_message_append(call.fMessage, "v", "s", token.c_str())) ||
      fail(sd_bus_message_close_container(call.fMessage))) {
    return std::nullopt;
  }
  // filters is a(sa(us)): a named set of patterns, where 0 is a shell glob
  // and 1 would be a MIME type.
  r = sd_bus_message_open_container(call.fMessage, 'e', "sv");
  if (fail(r) || fail(sd_bus_message_append(call.fMessage, "s", "filters")) ||
      fail(sd_bus_message_open_container(call.fMessage, 'v', "a(sa(us))")) ||
      fail(sd_bus_message_open_container(call.fMessage, 'a', "(sa(us))")) ||
      fail(sd_bus_message_open_container(call.fMessage, 'r', "sa(us)")) ||
      fail(sd_bus_message_append(call.fMessage, "s", "osu! beatmap")) ||
      fail(sd_bus_message_open_container(call.fMessage, 'a', "(us)")) ||
      fail(sd_bus_message_append(call.fMessage, "(us)", 0u, "*.osz")) ||
      fail(sd_bus_message_append(call.fMessage, "(us)", 0u, "*.zip")) ||
      fail(sd_bus_message_close_container(call.fMessage)) ||
      fail(sd_bus_message_close_container(call.fMessage)) ||
      fail(sd_bus_message_close_container(call.fMessage)) ||
      fail(sd_bus_message_close_container(call.fMessage)) ||
      fail(sd_bus_message_close_container(call.fMessage))) {
    return std::nullopt;
  }
  if (fail(sd_bus_message_close_container(call.fMessage))) {
    return std::nullopt;
  }

  detail::Message reply;
  sd_bus_error error = SD_BUS_ERROR_NULL;
  r = sd_bus_call(bus.fBus, call.fMessage, 0, &error, &reply.fMessage);
  const bool called = r >= 0;
  const std::string message = error.message != nullptr ? error.message : "";
  sd_bus_error_free(&error);
  if (!called) {
    if (!message.empty()) {
      std::println(std::cerr, "[portal] OpenFile refused: {}", message);
    }
    return std::nullopt; // no portal on this machine, or it said no
  }

  // Older portals ignored handle_token and made up their own path. Match on
  // what came back as well, rather than waiting for a signal that will be
  // delivered somewhere else.
  const char *handle = nullptr;
  detail::Slot fallback;
  if (sd_bus_message_read(reply.fMessage, "o", &handle) > 0 &&
      handle != nullptr && expected != handle) {
    sd_bus_match_signal(bus.fBus, &fallback.fSlot,
                        "org.freedesktop.portal.Desktop", handle,
                        "org.freedesktop.portal.Request", "Response",
                        detail::onResponse, &answer);
  }

  while (!answer.fArrived) {
    const int processed = sd_bus_process(bus.fBus, nullptr);
    if (processed < 0) {
      return std::nullopt;
    }
    if (processed > 0) {
      continue; // there may be more queued
    }
    if (sd_bus_wait(bus.fBus, UINT64_MAX) < 0) {
      return std::nullopt;
    }
  }
  return answer.fPath;
}

#else

[[nodiscard]] inline std::optional<std::filesystem::path>
openArchive(const std::string &) {
  return std::nullopt; // built without D-Bus
}

#endif

} // namespace client::portal
