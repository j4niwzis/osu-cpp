module;

#ifndef OSU_ANDROID_SYSTEM_FILE_PICKER
#define OSU_ANDROID_SYSTEM_FILE_PICKER 0
#endif

export module platform.capabilities;

export namespace platform::capabilities {
inline constexpr bool kBrowser = false;
inline constexpr bool kNativeFileDialogs = OSU_ANDROID_SYSTEM_FILE_PICKER != 0;
inline constexpr bool kAudioProvidesTimeline = true;
inline constexpr bool kThreadedWindowLoop = true;
} // namespace platform::capabilities
