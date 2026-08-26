function(osu_add_android_apk target)
  if(NOT ANDROID)
    message(FATAL_ERROR "osu_add_android_apk is only available for Android builds")
  endif()

  set(OSU_ANDROID_API "27" CACHE STRING "Android native API level")
  set(OSU_ANDROID_MIN_API "${OSU_ANDROID_API}" CACHE STRING
    "Minimum Android SDK API")
  set(OSU_ANDROID_TARGET_API "35" CACHE STRING
    "Android target SDK API")
  set(OSU_ANDROID_ABI "arm64-v8a" CACHE STRING "Android APK ABI")
  set(OSU_ANDROID_FRAMEWORK_RES_APK "" CACHE FILEPATH
    "Source-built Android framework resource APK")
  set(OSU_ANDROID_PLATFORM_JAR "" CACHE FILEPATH
    "Deprecated alias for OSU_ANDROID_FRAMEWORK_RES_APK")
  if(NOT OSU_ANDROID_FRAMEWORK_RES_APK AND OSU_ANDROID_PLATFORM_JAR)
    set(OSU_ANDROID_FRAMEWORK_RES_APK "${OSU_ANDROID_PLATFORM_JAR}")
    message(DEPRECATION
      "OSU_ANDROID_PLATFORM_JAR is deprecated; use OSU_ANDROID_FRAMEWORK_RES_APK")
  endif()
  set(OSU_ANDROID_APKSIGNER_JAR "" CACHE FILEPATH
    "Source-built apksigner executable jar")
  set(OSU_ANDROID_KEY_ALIAS "androiddebugkey" CACHE STRING "APK signing key alias")
  set(OSU_ANDROID_KEY_PASSWORD "android" CACHE STRING "APK signing key password")
  set(OSU_ANDROID_KEYSTORE "${CMAKE_BINARY_DIR}/apk/debug.keystore"
    CACHE FILEPATH "APK signing keystore")
  option(OSU_ANDROID_SYSTEM_FILE_PICKER
    "Build the DEX bridge for Android's system document picker" ON)
  set(OSU_ANDROID_D8_JAR "" CACHE FILEPATH
    "R8 jar containing com.android.tools.r8.D8")

  find_program(aapt2 NAMES aapt2 REQUIRED)
  find_program(zipalign NAMES zipalign REQUIRED)
  find_program(java NAMES java REQUIRED)
  find_program(jar NAMES jar REQUIRED)
  find_program(keytool NAMES keytool REQUIRED)
  if(NOT EXISTS "${OSU_ANDROID_FRAMEWORK_RES_APK}")
    message(FATAL_ERROR "Set OSU_ANDROID_FRAMEWORK_RES_APK")
  endif()
  if(NOT EXISTS "${OSU_ANDROID_APKSIGNER_JAR}")
    message(FATAL_ERROR "Set OSU_ANDROID_APKSIGNER_JAR")
  endif()

  set(apk_dir "${CMAKE_BINARY_DIR}/apk")
  set(stage_dir "${apk_dir}/stage")
  set(unsigned_apk "${apk_dir}/osu-cpp-unsigned.apk")
  set(aligned_apk "${apk_dir}/osu-cpp-aligned.apk")
  set(signed_apk "${apk_dir}/osu-cpp.apk")
  set(compiled_resources "${apk_dir}/resources.zip")
  set(native_dir "${stage_dir}/lib/${OSU_ANDROID_ABI}")
  set(android_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../android")
  set(android_manifest "${apk_dir}/AndroidManifest.xml")
  file(MAKE_DIRECTORY "${apk_dir}")
  target_compile_definitions(${target} PRIVATE
    OSU_ANDROID_SYSTEM_FILE_PICKER=$<BOOL:${OSU_ANDROID_SYSTEM_FILE_PICKER}>)

  set(dex_commands)
  set(dex_dependencies)
  set(dex_package_command)
  if(OSU_ANDROID_SYSTEM_FILE_PICKER)
    find_program(javac NAMES javac REQUIRED)
    if(OSU_ANDROID_D8_JAR)
      if(NOT EXISTS "${OSU_ANDROID_D8_JAR}")
        message(FATAL_ERROR
          "OSU_ANDROID_D8_JAR does not exist: ${OSU_ANDROID_D8_JAR}")
      endif()
      set(d8_command "${java}" -cp "${OSU_ANDROID_D8_JAR}"
        com.android.tools.r8.D8)
    else()
      find_program(d8 NAMES d8 REQUIRED)
      set(d8_command "${d8}")
    endif()
    set(java_source
      "${android_dir}/java/io/github/j4niwzis/osu_cpp/OsuNativeActivity.java")
    file(GLOB_RECURSE java_stubs CONFIGURE_DEPENDS
      "${android_dir}/java-stubs/*.java")
    set(java_classes "${apk_dir}/java-classes")
    set(dex_dir "${apk_dir}/dex")
    set(classes_dex "${dex_dir}/classes.dex")
    set(OSU_ANDROID_ACTIVITY
      "io.github.j4niwzis.osu_cpp.OsuNativeActivity")
    set(OSU_ANDROID_HAS_CODE true)
    list(APPEND dex_commands
      COMMAND "${CMAKE_COMMAND}" -E rm -rf "${java_classes}" "${dex_dir}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${java_classes}" "${dex_dir}"
      COMMAND "${javac}" -encoding UTF-8 -source 8 -target 8
        -d "${java_classes}" "${java_source}" ${java_stubs}
      COMMAND ${d8_command} --release --min-api "${OSU_ANDROID_MIN_API}"
        --output "${dex_dir}"
        "${java_classes}/io/github/j4niwzis/osu_cpp/OsuNativeActivity.class"
        "${java_classes}/io/github/j4niwzis/osu_cpp/OsuNativeActivity\$1.class"
        "${java_classes}/io/github/j4niwzis/osu_cpp/OsuNativeActivity\$2.class")
    list(APPEND dex_dependencies "${java_source}" ${java_stubs})
    set(dex_package_command
      COMMAND "${jar}" uf "${unsigned_apk}" -C "${dex_dir}" classes.dex)
  else()
    set(OSU_ANDROID_ACTIVITY "android.app.NativeActivity")
    set(OSU_ANDROID_HAS_CODE false)
  endif()
  configure_file("${android_dir}/AndroidManifest.xml" "${android_manifest}" @ONLY)

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
  file(GLOB_RECURSE android_resources CONFIGURE_DEPENDS
    "${android_dir}/res/*")

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
    ${dex_commands}
    COMMAND "${CMAKE_COMMAND}" -E rm -f
      "${compiled_resources}" "${unsigned_apk}" "${aligned_apk}" "${signed_apk}"
    COMMAND "${aapt2}" compile
      --dir "${android_dir}/res"
      -o "${compiled_resources}"
    COMMAND "${aapt2}" link
      -o "${unsigned_apk}"
      -I "${OSU_ANDROID_FRAMEWORK_RES_APK}"
      --manifest "${android_manifest}"
      --min-sdk-version "${OSU_ANDROID_MIN_API}"
      --target-sdk-version "${OSU_ANDROID_TARGET_API}"
      -A "${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "${compiled_resources}"
    COMMAND "${jar}" uf "${unsigned_apk}" -C "${stage_dir}" lib
    ${dex_package_command}
    COMMAND "${zipalign}" -f 4 "${unsigned_apk}" "${aligned_apk}"
    COMMAND "${java}" -jar "${OSU_ANDROID_APKSIGNER_JAR}" sign
      --ks "${OSU_ANDROID_KEYSTORE}"
      --ks-key-alias "${OSU_ANDROID_KEY_ALIAS}"
      --ks-pass "pass:${OSU_ANDROID_KEY_PASSWORD}"
      --key-pass "pass:${OSU_ANDROID_KEY_PASSWORD}"
      --out "${signed_apk}" "${aligned_apk}"
    DEPENDS ${target} "${OSU_ANDROID_KEYSTORE}"
      "${android_manifest}" ${dex_dependencies}
      ${android_resources} ${packaged_assets} ${prefix_libraries}
    VERBATIM
    COMMENT "Packaging signed Android APK")

  add_custom_target(apk ALL DEPENDS "${signed_apk}")
  message(STATUS "Android APK target: ${signed_apk}")
endfunction()
