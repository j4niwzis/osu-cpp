export module platform.input;

// Stable input vocabulary shared by every backend. Values intentionally
// match GLFW's public key/action ABI so the desktop backend can forward
// events without a translation table. Android and web translate into these
// values at their platform boundary.
export namespace platform::input {

inline constexpr int kRelease = 0;
inline constexpr int kPress = 1;
inline constexpr int kRepeat = 2;

inline constexpr int kModShift = 0x0001;
inline constexpr int kModControl = 0x0002;

inline constexpr int kMouseButtonLeft = 0;
inline constexpr int kMouseButtonRight = 1;

enum class CursorMode { kNormal, kHidden, kDisabled };

inline constexpr int kKeySpace = 32;
inline constexpr int kKeyA = 65;
inline constexpr int kKeyB = 66;
inline constexpr int kKeyD = 68;
inline constexpr int kKeyI = 73;
inline constexpr int kKeyO = 79;
inline constexpr int kKeyP = 80;
inline constexpr int kKeyQ = 81;
inline constexpr int kKeyR = 82;
inline constexpr int kKeyS = 83;
inline constexpr int kKeyW = 87;
inline constexpr int kKeyX = 88;
inline constexpr int kKeyZ = 90;

inline constexpr int kKeyEscape = 256;
inline constexpr int kKeyEnter = 257;
inline constexpr int kKeyTab = 258;
inline constexpr int kKeyBackspace = 259;
inline constexpr int kKeyDelete = 261;
inline constexpr int kKeyRight = 262;
inline constexpr int kKeyLeft = 263;
inline constexpr int kKeyDown = 264;
inline constexpr int kKeyUp = 265;
inline constexpr int kKeyPageUp = 266;
inline constexpr int kKeyPageDown = 267;

inline constexpr int kKeyF1 = 290;
inline constexpr int kKeyF2 = 291;
inline constexpr int kKeyF3 = 292;
inline constexpr int kKeyF4 = 293;
inline constexpr int kKeyF5 = 294;
inline constexpr int kKeyF11 = 300;

inline constexpr int kKeyLeftShift = 340;
inline constexpr int kKeyLeftControl = 341;
inline constexpr int kKeyRightControl = 345;

} // namespace platform::input
