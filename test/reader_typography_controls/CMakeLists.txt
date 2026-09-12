# Two reader-facing typography rules: two-choice rows toggle in place, and the
# line spacing ramp is append-only.
#
# SettingRowUi.h is header-only but reaches SettingsActivity.h -> Activity.h ->
# ActivityManager.h, which wants FreeRTOS. test/activity_input/stubs supplies it
# (and HalGPIO / HalStorage / Print), exactly as the activity suites do, so this
# links NO .cpp from the activity layer at all -- the questions here are about a
# constant and a `switch`.
#
# ArduinoJson comes from PlatformIO's lib_deps rather than the repo, so it only
# exists once a pio build has run; CrossPointSettings.h includes it.
# test/CMakeLists.txt resolves it, including the worktree fallback.
if(NOT CROSSPOINT_ARDUINOJSON_DIR)
  message(FATAL_ERROR
    "ArduinoJson not found under .pio/libdeps/*/ArduinoJson/src (nor the primary checkout's).\n"
    "Run any PlatformIO build first, e.g.:  pio run -e simulator")
endif()

add_executable(ReaderTypographyControlsTest
  ReaderTypographyControlsTest.cpp
)

target_compile_options(ReaderTypographyControlsTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

# The ramp and the gate are read out of the tree as text, so the suite needs to
# know where the tree is.
target_compile_definitions(ReaderTypographyControlsTest PRIVATE
  CROSSPOINT_REPO_ROOT="${REPO_ROOT}")

target_include_directories(ReaderTypographyControlsTest BEFORE PRIVATE
  ${REPO_ROOT}/test/activity_input/stubs
)
target_include_directories(ReaderTypographyControlsTest PRIVATE
  ${CROSSPOINT_ARDUINOJSON_DIR}
  ${REPO_ROOT}/test/host_stubs
  ${REPO_ROOT}/tools/calendar_preview
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(ReaderTypographyControlsTest PRIVATE crosspoint_test_common GTest::gtest GTest::gtest_main)
gtest_discover_tests(ReaderTypographyControlsTest)
