export module platform.capabilities;

export namespace platform::capabilities {
inline constexpr bool kBrowser = false;
inline constexpr bool kNativeFileDialogs = false;
inline constexpr bool kAudioProvidesTimeline = true;
inline constexpr bool kThreadedWindowLoop = true;
} // namespace platform::capabilities
