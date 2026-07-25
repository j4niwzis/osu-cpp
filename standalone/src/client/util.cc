export module client.util;

import std;

export namespace client {
namespace detail {

template <class F> class ScopeGuard {
public:
  explicit ScopeGuard(F f) : fFunc(std::move(f)) {}
  ~ScopeGuard() {
    if (fActive) {
      fFunc();
    }
  }
  ScopeGuard(const ScopeGuard &) = delete;
  ScopeGuard &operator=(const ScopeGuard &) = delete;
  ScopeGuard(ScopeGuard &&other) noexcept
      : fFunc(std::move(other.fFunc)), fActive(other.fActive) {
    other.fActive = false;
  }
  ScopeGuard &operator=(ScopeGuard &&) = delete;
  void dismiss() noexcept { fActive = false; }

private:
  F fFunc;
  bool fActive = true;
};

template <class F> ScopeGuard<F> scopeGuard(F f) {
  return ScopeGuard<F>(std::move(f));
}

inline std::string toLower(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

inline std::string lowerExtension(const std::filesystem::path &path) {
  auto ext = path.extension().string();
  std::ranges::transform(ext, ext.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

inline std::string fileExtension(std::string_view name) {
  std::string ext;
  if (const auto dot = name.rfind('.'); dot != std::string_view::npos) {
    ext = name.substr(dot);
  }
  std::ranges::transform(ext, ext.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

inline bool isOsz(const std::filesystem::path &path) {
  return toLower(path.extension().string()) == ".osz";
}

inline bool isOsu(std::string_view name) {
  return toLower(std::string(name)).ends_with(".osu");
}

[[nodiscard]] inline std::vector<std::uint8_t>
readFile(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

} // namespace detail
} // namespace client
