export module client.http;

import std;
import platform.network;
import platform.clock;

export namespace client::http {
using Response = platform::network::Response;
using Handle = platform::network::Handle;
using Callback = platform::network::Callback;

namespace detail {

// A request that was refused in a way worth asking about again, and when to
// ask.
struct Repeat {
  double fDueMs = 0.0;
  std::string fUrl;
  std::shared_ptr<Handle> fHandle;
  Callback fDone;
  int fAttempt = 0;
  int fAttempts = 0;
};

inline std::mutex &mutex() {
  static std::mutex it;
  return it;
}
inline std::vector<Repeat> &waiting() {
  static std::vector<Repeat> it;
  return it;
}

// Which refusals are about the request, and which are about the moment.
//
// A 404 is an answer: the address is wrong, and asking again only wastes
// somebody's server. Nothing at all -- a dropped connection, or a front end
// that answered for something it never passed on -- a 5xx, a 429 and a
// timeout are the moment, and a mirror does all of those when several
// requests from one page arrive at once. Those are worth waiting a moment
// and asking again, which is the difference between a listing of blank
// panels and a listing that fills in a second later.
[[nodiscard]] inline bool worthRepeating(const Response &response) {
  return !response.fOk &&
         (response.fStatus == 0 || response.fStatus == 408 ||
          response.fStatus == 429 || response.fStatus >= 500);
}

// How many times a request is made in total, unless the caller says
// otherwise. Three is for things that are worth waiting for: artwork, a
// preview, a download. Something choosing between hosts says one, because
// asking a host that is not answering three times is three delays before
// trying the host that would have answered.
inline constexpr int kAttempts = 3;

[[nodiscard]] inline double backoffMs(int attempt) {
  return 400.0 * static_cast<double>(1 << attempt); // 400, 800
}

inline void start(std::string url, std::shared_ptr<Handle> handle,
                  Callback done, int attempt, int attempts) {
  std::string again = url;
  platform::network::get(
      std::move(url), handle,
      [again = std::move(again), handle, done, attempt,
       attempts](Response r) mutable {
        if (attempt + 1 < attempts && worthRepeating(r)) {
          // The transport marks the handle finished when it gives up; this
          // request has not finished, it is waiting.
          if (handle) {
            handle->fDone.store(false, std::memory_order_release);
          }
          const std::scoped_lock lock(mutex());
          waiting().push_back(Repeat{
              platform::clock::milliseconds() + backoffMs(attempt),
              std::move(again), std::move(handle), std::move(done),
              attempt + 1, attempts});
          return;
        }
        done(std::move(r));
      });
}

} // namespace detail

inline void get(std::string url, std::shared_ptr<Handle> handle,
                Callback callback, int attempts = detail::kAttempts) {
  detail::start(std::move(url), std::move(handle), std::move(callback), 0,
                attempts < 1 ? 1 : attempts);
}

inline std::size_t poll() {
  const std::size_t answered = platform::network::poll();
  std::vector<detail::Repeat> due;
  {
    const std::scoped_lock lock(detail::mutex());
    const double now = platform::clock::milliseconds();
    auto &waiting = detail::waiting();
    for (std::size_t i = waiting.size(); i > 0; --i) {
      auto &one = waiting[i - 1];
      if (one.fDueMs > now) {
        continue;
      }
      due.push_back(std::move(one));
      waiting.erase(waiting.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
  }
  for (auto &one : due) {
    detail::start(std::move(one.fUrl), std::move(one.fHandle),
                  std::move(one.fDone), one.fAttempt, one.fAttempts);
  }
  return answered + due.size();
}

// Query formatting belongs to the client protocol, not the transport.
[[nodiscard]] inline std::string urlEncode(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
        byte == '.' || byte == '~') {
      out.push_back(c);
    } else if (byte == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[byte >> 4]);
      out.push_back(kHex[byte & 0x0f]);
    }
  }
  return out;
}
} // namespace client::http
