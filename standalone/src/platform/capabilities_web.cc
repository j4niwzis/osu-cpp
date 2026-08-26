export module platform.capabilities;

export namespace platform::capabilities {
inline constexpr bool kBrowser = true;
inline constexpr bool kNativeFileDialogs = false;
inline constexpr bool kAudioProvidesTimeline = false;
inline constexpr bool kThreadedWindowLoop = false;
} // namespace platform::capabilities
