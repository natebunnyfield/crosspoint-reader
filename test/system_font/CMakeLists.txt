# System-font suite.
#
# Built at CROSSPOINT_RENDER_SCALE=2 -- the whole hi-res path is #if'd out at
# scale 1, so the chrome-companion tests would compile down to nothing there.
# This is why it is a separate target from font_switch_churn rather than more
# cases inside it: that suite measures live heap across font switches, and
# loading a 2x companion for every SD font would change the numbers it asserts.
#
# Shares font_switch_churn's HalStorage stub (the fuller one, with directory
# iteration) and its WORKING_DIRECTORY convention: the stub rebases device paths
# onto ./fs_, where the real .cpfont tree lives.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")
set(CP_CHURN_STUBS "${CMAKE_CURRENT_SOURCE_DIR}/../font_switch_churn/stubs")

add_executable(SystemFontTest
  SystemFontTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontManager.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${CP_PREVIEW}/uzlib_checksums.c
)

target_compile_definitions(SystemFontTest PRIVATE CROSSPOINT_RENDER_SCALE=2)

target_compile_options(SystemFontTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(SystemFontTest BEFORE PRIVATE
  ${CP_CHURN_STUBS}
)
target_include_directories(SystemFontTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
)

target_link_libraries(SystemFontTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SystemFontTest WORKING_DIRECTORY ${REPO_ROOT})
