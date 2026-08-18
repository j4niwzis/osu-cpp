export module client.http;

import std;
#ifdef __EMSCRIPTEN__
import emscripten;
#else
import beast;
#endif

export namespace client::http {

struct Response {
  bool fOk = false;
  long fStatus = 0;
  std::string fBody; // bytes; std::string avoids pointer reinterpretation
  std::string fError;
};

// Progress/liveness handle shared with the UI.
struct Handle {
  std::atomic<float> fProgress{0.0f}; // 0..1, best effort
  std::atomic<bool> fDone{false};
};

using Callback = std::function<void(Response)>;

} // namespace client::http

namespace client::http {

namespace detail {

struct Completed {
  Callback fCb;
  Response fResp;
};

// Function-local statics instead of mutable inline module-scope globals:
// the latter produce the most exotic IR this TU has, and LLVM's bitcode
// writer under LTO is exactly the wrong audience for exotic.
inline std::mutex &queueMutex() {
  static std::mutex m;
  return m;
}

inline std::vector<Completed> &completedQueue() {
  static std::vector<Completed> q;
  return q;
}

inline void complete(Callback cb, Response resp) {
  const std::scoped_lock lock(queueMutex());
  completedQueue().push_back({std::move(cb), std::move(resp)});
}

#ifdef __EMSCRIPTEN__
struct FetchCtx {
  Callback fCb;
  std::shared_ptr<Handle> fHandle;
};
#else
// Worker body as a named function: keeps the thread entry point out of the
// exported inline get() and its lambda out of the BMI.
inline void runWorker(std::string url, std::shared_ptr<Handle> handle,
                      Callback cb) {
  beastnet::FetchResult r = beastnet::fetch(
      url, [&handle](std::size_t got, std::size_t total) {
        if (total > 0) {
          handle->fProgress.store(static_cast<float>(got) /
                                      static_cast<float>(total),
                                  std::memory_order_relaxed);
        }
      });
  Response resp;
  resp.fOk = r.fOk;
  resp.fStatus = r.fStatus;
  resp.fBody = std::move(r.fBody);
  resp.fError = std::move(r.fError);
  handle->fProgress.store(1.0f);
  handle->fDone.store(true);
  complete(std::move(cb), std::move(resp));
}
#endif

} // namespace detail

} // namespace client::http

export namespace client::http {

// Fire an async GET. The callback runs on the thread that calls poll().
inline void get(const std::string &url, std::shared_ptr<Handle> handle,
                Callback cb) {
#ifdef __EMSCRIPTEN__
  emscripten::emscripten_fetch_attr_t attr;
  emscripten::emscripten_fetch_attr_init(&attr);
  std::strcpy(attr.requestMethod, "GET");
  attr.attributes = emscripten::kFetchLoadToMemory;
  auto *ctx = new detail::FetchCtx{std::move(cb), std::move(handle)};
  attr.userData = ctx;
  attr.onsuccess = [](emscripten::emscripten_fetch_t *fetch) {
    auto *ctx = static_cast<detail::FetchCtx *>(fetch->userData);
    Response resp;
    resp.fStatus = fetch->status;
    resp.fOk = fetch->status >= 200 && fetch->status < 300;
    resp.fBody.assign(fetch->data, static_cast<std::size_t>(fetch->numBytes));
    if (!resp.fOk) {
      resp.fError = "HTTP " + std::to_string(fetch->status);
    }
    ctx->fHandle->fProgress.store(1.0f);
    ctx->fHandle->fDone.store(true);
    detail::complete(std::move(ctx->fCb), std::move(resp));
    delete ctx;
    emscripten::emscripten_fetch_close(fetch);
  };
  attr.onerror = [](emscripten::emscripten_fetch_t *fetch) {
    auto *ctx = static_cast<detail::FetchCtx *>(fetch->userData);
    Response resp;
    resp.fStatus = fetch->status;
    resp.fError = "network error (HTTP " + std::to_string(fetch->status) +
                  "); possibly CORS";
    ctx->fHandle->fDone.store(true);
    detail::complete(std::move(ctx->fCb), std::move(resp));
    delete ctx;
    emscripten::emscripten_fetch_close(fetch);
  };
  attr.onprogress = [](emscripten::emscripten_fetch_t *fetch) {
    auto *ctx = static_cast<detail::FetchCtx *>(fetch->userData);
    if (fetch->totalBytes > 0) {
      ctx->fHandle->fProgress.store(static_cast<float>(fetch->dataOffset) /
                                    static_cast<float>(fetch->totalBytes));
    }
  };
  emscripten::emscripten_fetch(&attr, url.c_str());
#else
  std::thread(&detail::runWorker, url, std::move(handle), std::move(cb))
      .detach();
#endif
}

// Run completed callbacks on the calling thread. Call once per frame.
inline void poll() {
  std::vector<detail::Completed> ready;
  {
    const std::scoped_lock lock(detail::queueMutex());
    ready.swap(detail::completedQueue());
  }
  for (auto &c : ready) {
    c.fCb(std::move(c.fResp));
  }
}

// Percent-encode a query component.
[[nodiscard]] inline std::string urlEncode(std::string_view s) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
        (u >= '0' && u <= '9') || u == '-' || u == '_' || u == '.' ||
        u == '~') {
      out.push_back(c);
    } else if (u == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[u >> 4]);
      out.push_back(kHex[u & 0xF]);
    }
  }
  return out;
}

} // namespace client::http
