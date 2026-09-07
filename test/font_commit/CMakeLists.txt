# Font-commit suite: the REAL FontUpdater over a REAL filesystem.
#
# Everything else that tests Update Fonts works on pure logic (test/font_sync)
# or on the screen over a doubled updater (test/activity_input), so before this
# suite existed not one line of the code that WRITES TO THE CARD was executed by
# any test -- staging, verification, the two-rename commit, the rollback, the
# recursive delete of the outgoing family, and recovery from an interrupted
# commit. An adversarial review named that as the largest gap on 2026-09-07.
#
# What is real here: src/network/FontUpdater.cpp, lib/EpdFont/SdCardFontRegistry.cpp
# (root selection), lib/JsonParser/StreamingJsonParser.cpp (the release JSON),
# and ArduinoJson.
#
# What is stubbed, and why: HttpDownloader (a scriptable in-memory release --
# there is no network), mbedtls sha256 (a real SHA-256 implementation, asserted
# against the published vectors before anything leans on it), CrossPointSettings
# (the real header drags in PersistableStore and the whole settings list for two
# char arrays) and SdCardFontSystem (the real facade owns a GfxRenderer).
#
# stubs/ is FIRST on the include path so its HalStorage -- a real filesystem
# rooted in a per-test temp directory, with directory rename and recursive
# removeDir, which no other stub in test/ implements -- supersedes both
# tools/calendar_preview's and lib/hal's.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

if(NOT CROSSPOINT_ARDUINOJSON_DIR)
  message(FATAL_ERROR
    "ArduinoJson not found under .pio/libdeps/*/ArduinoJson/src (nor the primary checkout's).\n"
    "Run any PlatformIO build first, e.g.:  pio run -e simulator_x3")
endif()

add_executable(FontCommitTest
  FontCommitTest.cpp
  Fixtures.cpp
  ${REPO_ROOT}/src/network/FontUpdater.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

# The firmware sources assume the Arduino prelude is in scope (Logging.h pulls
# it), same trick as test/renderer_bounds and test/font_switch_churn.
target_compile_options(FontCommitTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(FontCommitTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(FontCommitTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CROSSPOINT_ARDUINOJSON_DIR}
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(FontCommitTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontCommitTest)
