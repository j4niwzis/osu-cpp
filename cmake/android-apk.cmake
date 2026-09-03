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
  # Whether this build makes its own key.
  #
  # A key made here is a test key: it lives in the build directory, it is
  # written down in this file, and anyone can produce a signature with it. It
  # says so in the certificate and in the name of the file it signs, because
  # a signature that looks like a release and is not is worse than no
  # signature. A real key is passed in, and never comes near this repository.
  option(OSU_ANDROID_TEST_KEY
    "Sign with a key this build generates, and say so in the certificate and \
the file name" ON)
  set(OSU_ANDROID_KEY_ALIAS "androiddebugkey" CACHE STRING "APK signing key alias")
  set(OSU_ANDROID_KEY_PASSWORD "android" CACHE STRING "APK signing key password")
  set(OSU_ANDROID_KEYSTORE "${CMAKE_BINARY_DIR}/apk/debug.keystore"
    CACHE FILEPATH "APK signing keystore")
  option(OSU_ANDROID_SYSTEM_FILE_PICKER
    "Build the DEX bridge for Android's system document picker" ON)
  set(OSU_ANDROID_D8_JAR "" CACHE FILEPATH
    "R8 jar containing com.android.tools.r8.D8")

  # The modification time every entry this build adds to the APK is given.
  #
  # Three builds of the same sources produced three different APKs, and the
  # whole difference was here: fourteen entries identical, three carrying the
  # clock. The 117 MB of native code was byte-identical every time, so the
  # only thing standing between this and a reproducible package was the time
  # of day it was packed at.
  set(OSU_ANDROID_ENTRY_TIMESTAMP "2001-01-01T00:00:00Z" CACHE STRING
    "The modification time given to every entry this build adds to the APK")

  find_program(aapt2 NAMES aapt2 REQUIRED)
  find_program(zipalign NAMES zipalign REQUIRED)
  find_program(java NAMES java REQUIRED)
  find_program(jar NAMES jar REQUIRED)
  find_program(keytool NAMES keytool REQUIRED)
  # Said with the path in it: the check is whether the file is there, and a
  # message that only says "set this" is wrong every time it is set to
  # something that is not there -- which is the more common way to get it
  # wrong.
  foreach(needed OSU_ANDROID_FRAMEWORK_RES_APK OSU_ANDROID_APKSIGNER_JAR)
    if(NOT ${needed})
      message(FATAL_ERROR "Set ${needed}")
    elseif(NOT EXISTS "${${needed}}")
      message(FATAL_ERROR
        "${needed} is ${${needed}}, and there is no such file")
    endif()
  endforeach()

  # jar --date is JDK 17 and later. Older ones write the clock and there is
  # nothing to be done about it from here, so it is said rather than left to
  # be discovered by whoever compares two APKs.
  #
  # The operation is spelled long too. `jar --date X uf archive` is not a
  # command: given a long option, jar stops reading `uf` as the operation and
  # says that one of -{ctxuid} must be specified, which is true and is not
  # about the date at all.
  execute_process(
    COMMAND "${jar}" --date "${OSU_ANDROID_ENTRY_TIMESTAMP}" --version
    RESULT_VARIABLE jar_dated OUTPUT_QUIET ERROR_QUIET)
  if(jar_dated EQUAL 0)
    set(jar_date --date "${OSU_ANDROID_ENTRY_TIMESTAMP}")
    message(STATUS
      "APK entries are dated ${OSU_ANDROID_ENTRY_TIMESTAMP}")
  else()
    set(jar_date)
    message(STATUS
      "${jar} does not take --date: APK entries carry the time they were "
      "packed at, and two builds of the same sources will differ")
  endif()

  set(apk_dir "${CMAKE_BINARY_DIR}/apk")
  set(stage_dir "${apk_dir}/stage")
  # Three files, named for what they are. The middle one is the package: it
  # is what gets signed, it is what a signature can be stripped back to, and
  # it is the one worth attesting, because it is the same for everyone who
  # builds these sources and the signed one is not.
  set(unaligned_apk "${apk_dir}/osu-cpp-unaligned.apk")
  set(unsigned_apk "${apk_dir}/osu-cpp-unsigned.apk")
  if(OSU_ANDROID_TEST_KEY)
    set(signed_apk "${apk_dir}/osu-cpp-test-signed.apk")
  else()
    set(signed_apk "${apk_dir}/osu-cpp.apk")
  endif()
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
      COMMAND "${jar}" ${jar_date} --update --file "${unaligned_apk}"
        -C "${dex_dir}" classes.dex)
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

  if(OSU_ANDROID_TEST_KEY)
    add_custom_command(
      OUTPUT "${OSU_ANDROID_KEYSTORE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${apk_dir}"
      COMMAND "${keytool}" -genkeypair -noprompt
        -keystore "${OSU_ANDROID_KEYSTORE}"
        -storepass "${OSU_ANDROID_KEY_PASSWORD}"
        -alias "${OSU_ANDROID_KEY_ALIAS}"
        -keypass "${OSU_ANDROID_KEY_PASSWORD}"
        -dname "CN=osu-cpp TEST KEY - not a release key,O=osu-cpp,C=XX"
        -keyalg RSA -keysize 2048 -validity 10000
      VERBATIM)
  elseif(NOT EXISTS "${OSU_ANDROID_KEYSTORE}")
    # Without OSU_ANDROID_TEST_KEY this build signs with a key it was given
    # and does not invent one, because a key it invented would be a test key
    # wearing a release name.
    message(FATAL_ERROR
      "OSU_ANDROID_TEST_KEY is off and OSU_ANDROID_KEYSTORE is "
      "${OSU_ANDROID_KEYSTORE}, and there is no such file")
  endif()

  add_custom_command(
    OUTPUT "${signed_apk}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${stage_dir}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${native_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:${target}>" "${native_dir}/libosu_client.so"
    ${copy_libraries}
    ${dex_commands}
    COMMAND "${CMAKE_COMMAND}" -E rm -f
      "${compiled_resources}" "${unaligned_apk}" "${unsigned_apk}" "${signed_apk}"
    COMMAND "${aapt2}" compile
      --dir "${android_dir}/res"
      -o "${compiled_resources}"
    COMMAND "${aapt2}" link
      -o "${unaligned_apk}"
      -I "${OSU_ANDROID_FRAMEWORK_RES_APK}"
      --manifest "${android_manifest}"
      --min-sdk-version "${OSU_ANDROID_MIN_API}"
      --target-sdk-version "${OSU_ANDROID_TARGET_API}"
      -A "${CMAKE_CURRENT_SOURCE_DIR}/assets"
      "${compiled_resources}"
    COMMAND "${jar}" ${jar_date} --update --file "${unaligned_apk}"
      -C "${stage_dir}" lib
    ${dex_package_command}
    COMMAND "${zipalign}" -f 4 "${unaligned_apk}" "${unsigned_apk}"
    # Schemes v2 and v3 only. A v1 signature is three ordinary entries under
    # META-INF, and entries cannot be removed the way they were added: with
    # them there is no way back from the signed file to the package. Nothing
    # older than API 24 reads this APK anyway.
    #
    # --alignment-preserved, because apksigner does not preserve it: the
    # documentation says it does, and the source says alignmentPreserved is
    # false unless asked. What it does instead is lay the archive out again
    # -- every entry moved by four bytes, the two directory entries dropped
    # -- so the signed file stops being the package plus a signature, and
    # nothing can compare the two.
    COMMAND "${java}" -jar "${OSU_ANDROID_APKSIGNER_JAR}" sign
      --alignment-preserved
      --ks "${OSU_ANDROID_KEYSTORE}"
      --ks-key-alias "${OSU_ANDROID_KEY_ALIAS}"
      --ks-pass "pass:${OSU_ANDROID_KEY_PASSWORD}"
      --key-pass "pass:${OSU_ANDROID_KEY_PASSWORD}"
      --min-sdk-version "${OSU_ANDROID_MIN_API}"
      --v1-signing-enabled false
      --v2-signing-enabled true
      --v3-signing-enabled true
      --out "${signed_apk}" "${unsigned_apk}"
    DEPENDS ${target} "${OSU_ANDROID_KEYSTORE}"
      "${android_manifest}" ${dex_dependencies}
      ${android_resources} ${packaged_assets} ${prefix_libraries}
    VERBATIM
    COMMENT "Packaging signed Android APK")

  add_custom_target(apk ALL DEPENDS "${signed_apk}")
  message(STATUS "Android APK: ${unsigned_apk}")
  if(OSU_ANDROID_TEST_KEY)
    message(STATUS
      "  signed with a key this build generates, as ${signed_apk}; "
      "the certificate says so")
  else()
    message(STATUS "  signed with ${OSU_ANDROID_KEYSTORE}, as ${signed_apk}")
  endif()
endfunction()
