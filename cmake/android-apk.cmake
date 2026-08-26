function(osu_add_android_apk target)
  if(NOT ANDROID)
    message(FATAL_ERROR "osu_add_android_apk is only available for Android builds")
  endif()

  set(OSU_ANDROID_SDK "$ENV{ANDROID_HOME}" CACHE PATH "Android SDK root")
  if(NOT OSU_ANDROID_SDK)
    set(OSU_ANDROID_SDK "$ENV{ANDROID_SDK_ROOT}" CACHE PATH "Android SDK root" FORCE)
  endif()
  if(NOT IS_DIRECTORY "${OSU_ANDROID_SDK}")
    message(FATAL_ERROR "Set ANDROID_HOME or OSU_ANDROID_SDK to the Android SDK")
  endif()

  set(OSU_ANDROID_API "35" CACHE STRING "Android target SDK API")
  set(OSU_ANDROID_KEY_ALIAS "androiddebugkey" CACHE STRING "APK signing key alias")
  set(OSU_ANDROID_KEY_PASSWORD "android" CACHE STRING "APK signing key password")
  set(OSU_ANDROID_KEYSTORE "${CMAKE_BINARY_DIR}/apk/debug.keystore"
    CACHE FILEPATH "APK signing keystore")

  file(GLOB build_tools LIST_DIRECTORIES TRUE
    "${OSU_ANDROID_SDK}/build-tools/*")
  if(NOT build_tools)
    message(FATAL_ERROR "No Android SDK build-tools are installed")
  endif()
  list(SORT build_tools COMPARE NATURAL ORDER DESCENDING)
  list(GET build_tools 0 build_tools_dir)

  find_program(aapt2 NAMES aapt2 HINTS "${build_tools_dir}"
    NO_DEFAULT_PATH REQUIRED)
  find_program(zipalign NAMES zipalign HINTS "${build_tools_dir}"
    NO_DEFAULT_PATH REQUIRED)
  find_program(apksigner NAMES apksigner HINTS "${build_tools_dir}"
    NO_DEFAULT_PATH REQUIRED)
  find_program(jar NAMES jar REQUIRED)
  find_program(keytool NAMES keytool REQUIRED)

  set(platform_jar
    "${OSU_ANDROID_SDK}/platforms/android-${OSU_ANDROID_API}/android.jar")
  if(NOT EXISTS "${platform_jar}")
    message(FATAL_ERROR "Android platform ${OSU_ANDROID_API} is not installed")
  endif()

  if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(android_triple aarch64-linux-android)
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
    set(android_triple arm-linux-androideabi)
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
    set(android_triple i686-linux-android)
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(android_triple x86_64-linux-android)
  else()
    message(FATAL_ERROR "Unsupported Android ABI: ${CMAKE_ANDROID_ARCH_ABI}")
  endif()
  file(GLOB cxx_shared
    "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/*/sysroot/usr/lib/${android_triple}/libc++_shared.so")
  list(LENGTH cxx_shared cxx_shared_count)
  if(NOT cxx_shared_count EQUAL 1)
    message(FATAL_ERROR "Cannot locate the NDK libc++_shared.so")
  endif()

  set(apk_dir "${CMAKE_BINARY_DIR}/apk")
  set(stage_dir "${apk_dir}/stage")
  set(unsigned_apk "${apk_dir}/osu-cpp-unsigned.apk")
  set(aligned_apk "${apk_dir}/osu-cpp-aligned.apk")
  set(signed_apk "${apk_dir}/osu-cpp.apk")
  set(native_dir "${stage_dir}/lib/${CMAKE_ANDROID_ARCH_ABI}")

  # Prefix libraries are optional: fully static dependency builds have none.
  set(prefix_libraries)
  foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
    file(GLOB libraries CONFIGURE_DEPENDS
      "${prefix}/lib/*.so" "${prefix}/lib/*.so.*")
    list(APPEND prefix_libraries ${libraries})
  endforeach()
  list(REMOVE_DUPLICATES prefix_libraries)
  set(copy_libraries)
  foreach(library IN LISTS prefix_libraries)
    list(APPEND copy_libraries
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${library}" "${native_dir}/")
  endforeach()

  file(GLOB_RECURSE packaged_assets CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")

  add_custom_command(
    OUTPUT "${OSU_ANDROID_KEYSTORE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${apk_dir}"
    COMMAND "${keytool}" -genkeypair -noprompt
      -keystore "${OSU_ANDROID_KEYSTORE}"
      -storepass "${OSU_ANDROID_KEY_PASSWORD}"
      -alias "${OSU_ANDROID_KEY_ALIAS}"
      -keypass "${OSU_ANDROID_KEY_PASSWORD}"
      -dname "CN=osu-cpp debug,O=osu-cpp,C=XX"
      -keyalg RSA -keysize 2048 -validity 10000
    VERBATIM)

  add_custom_command(
    OUTPUT "${signed_apk}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${stage_dir}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${native_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:${target}>" "${native_dir}/libosu_client.so"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${cxx_shared}" "${native_dir}/libc++_shared.so"
    ${copy_libraries}
    COMMAND "${CMAKE_COMMAND}" -E rm -f
      "${unsigned_apk}" "${aligned_apk}" "${signed_apk}"
    COMMAND "${aapt2}" link
      -o "${unsigned_apk}"
      -I "${platform_jar}"
      --manifest "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../android/AndroidManifest.xml"
      --min-sdk-version 26
      --target-sdk-version "${OSU_ANDROID_API}"
      -A "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    COMMAND "${jar}" uf "${unsigned_apk}" -C "${stage_dir}" lib
    COMMAND "${zipalign}" -f 4 "${unsigned_apk}" "${aligned_apk}"
    COMMAND "${apksigner}" sign
      --ks "${OSU_ANDROID_KEYSTORE}"
      --ks-key-alias "${OSU_ANDROID_KEY_ALIAS}"
      --ks-pass "pass:${OSU_ANDROID_KEY_PASSWORD}"
      --key-pass "pass:${OSU_ANDROID_KEY_PASSWORD}"
      --out "${signed_apk}" "${aligned_apk}"
    DEPENDS ${target} "${OSU_ANDROID_KEYSTORE}"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../android/AndroidManifest.xml"
      ${packaged_assets} ${prefix_libraries}
    VERBATIM
    COMMENT "Packaging signed Android APK")

  add_custom_target(apk DEPENDS "${signed_apk}")
  message(STATUS "Android APK target: ${signed_apk}")
endfunction()
