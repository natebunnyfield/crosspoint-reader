# Sleep-screen policy suite.
#
# Header-only subject: src/activities/boot_sleep/SleepScreenPolicy.h. Nothing
# from the activity layer is linked -- SleepActivity alone drags in Epub, Xtc,
# Txt, HalClock, the calendar renderer and the theme stack.
#
# CrossPointSettings.h is compiled but CrossPointSettings.cpp is NOT linked, the
# same arrangement test/activity_input uses and for the same reason (JsonSettingsIO
# and file I/O). The consequence is the point of the first test: SETTINGS holds
# the real shipped field defaults with no settings.json loaded over them, which
# is exactly a device out of the box.
#
# ArduinoJson comes from PlatformIO's lib_deps rather than the repo, so it only
# exists once a pio build has run; CrossPointSettings.h includes it.
# test/CMakeLists.txt resolves it, including the worktree fallback.

if(NOT CROSSPOINT_ARDUINOJSON_DIR)
  message(FATAL_ERROR
    "ArduinoJson not found under .pio/libdeps/*/ArduinoJson/src (nor the primary checkout's).\n"
    "Run any PlatformIO build first, e.g.:  pio run -e simulator")
endif()
set(_sleep_arduinojson "${CROSSPOINT_ARDUINOJSON_DIR}")

add_executable(SleepScreenPolicyTest
  SleepScreenPolicyTest.cpp
)

# Headers only. CrossPointSettings.h reaches PersistableStore and
# Epub/ReaderRenderSpec.h; no .cpp from either is compiled.
target_include_directories(SleepScreenPolicyTest PRIVATE
  ${_sleep_arduinojson}
  # Arduino.h / Logging.h host stubs, same source the other suites use.
  ${REPO_ROOT}/tools/calendar_preview
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(SleepScreenPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepScreenPolicyTest WORKING_DIRECTORY ${REPO_ROOT})
