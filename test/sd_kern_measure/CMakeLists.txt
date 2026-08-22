# SD-font measure/draw kern parity suite (punctuation-kerning audit P0).
#
# Same wiring as font_switch_churn: the real registry + manager + renderer
# host-side, over the real .cpfont tree. `stubs/` (a copy of font_switch_churn's
# directory-capable HalStorage) comes FIRST on the include path so it supersedes
# tools/calendar_preview's read-only one.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(SdKernMeasureTest
  SdKernMeasureTest.cpp
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

target_compile_options(SdKernMeasureTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(SdKernMeasureTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(SdKernMeasureTest PRIVATE
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

target_link_libraries(SdKernMeasureTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SdKernMeasureTest
  WORKING_DIRECTORY ${REPO_ROOT}
  PROPERTIES ENVIRONMENT "CROSSPOINT_TEST_SD=${CROSSPOINT_TEST_SD_DIR}"
)
