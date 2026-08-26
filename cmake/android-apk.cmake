function(osu_add_android_apk target)
  if(NOT ANDROID)
    message(FATAL_ERROR "osu_add_android_apk is only available for Android builds")
  endif()

  set(OSU_ANDROID_API "27" CACHE STRING "Android target SDK API")
  set(OSU_ANDROID_MIN_API "${OSU_ANDROID_API}" CACHE STRING
    "Minimum Android SDK API")
  set(OSU_ANDROID_ABI "arm64-v8a" CACHE STRING "Android APK ABI")
  set(OSU_ANDROID_PLATFORM_JAR "" CACHE FILEPATH
    "Source-built Android platform android.jar")
  set(OSU_ANDROID_APKSIGNER_JAR "" CACHE FILEPATH
    "Source-built apksigner executable jar")
  set(OSU_ANDROID_KEY_ALIAS "androiddebugkey" CACHE STRING "APK signing key alias")
  set(OSU_ANDROID_KEY_PASSWORD "android" CACHE STRING "APK signing key password")
  set(OSU_ANDROID_KEYSTORE "${CMAKE_BINARY_DIR}/apk/debug.keystore"
    CACHE FILEPATH "APK signing keystore")

  find_program(aapt2 NAMES aapt2 REQUIRED)
  find_program(zipalign NAMES zipalign REQUIRED)
  find_program(java NAMES java REQUIRED)
  find_program(jar NAMES jar REQUIRED)
  find_program(keytool NAMES keytool REQUIRED)
  if(NOT EXISTS "${OSU_ANDROID_PLATFORM_JAR}")
    message(FATAL_ERROR "Set OSU_ANDROID_PLATFORM_JAR")
  endif()
  if(NOT EXISTS "${OSU_ANDROID_APKSIGNER_JAR}")
    message(FATAL_ERROR "Set OSU_ANDROID_APKSIGNER_JAR")
  endif()

  set(apk_dir "${CMAKE_BINARY_DIR}/apk")
  set(stage_dir "${apk_dir}/stage")
  set(unsigned_apk "${apk_dir}/osu-cpp-unsigned.apk")
  set(aligned_apk "${apk_dir}/osu-cpp-aligned.apk")
  set(signed_apk "${apk_dir}/osu-cpp.apk")
  set(native_dir "${stage_dir}/lib/${OSU_ANDROID_ABI}")

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
    ${copy_libraries}
    COMMAND "${CMAKE_COMMAND}" -E rm -f
      "${unsigned_apk}" "${aligned_apk}" "${signed_apk}"
    COMMAND "${aapt2}" link
      -o "${unsigned_apk}"
      -I "${OSU_ANDROID_PLATFORM_JAR}"
      --manifest "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../android/AndroidManifest.xml"
      --min-sdk-version "${OSU_ANDROID_MIN_API}"
      --target-sdk-version "${OSU_ANDROID_API}"
      -A "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    COMMAND "${jar}" uf "${unsigned_apk}" -C "${stage_dir}" lib
    COMMAND "${zipalign}" -f 4 "${unsigned_apk}" "${aligned_apk}"
    COMMAND "${java}" -jar "${OSU_ANDROID_APKSIGNER_JAR}" sign
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
