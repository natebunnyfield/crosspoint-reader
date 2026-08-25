# Hi-res shapes suite.
#
# Built at CROSSPOINT_RENDER_SCALE=3 -- the value the shipped iOS app pins
# (crosspoint-simulator/ios/CMakeLists.txt:89) -- because the device-pixel path
# under test is #if'd out at scale 1 and every case would assert nothing there.
# Same reason test/system_font is its own target at scale 2.
#
# Source list mirrors renderer_bounds: GfxRenderer.cpp does not link without the
# font and inflate objects even when no test in the suite draws text.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(HiResShapesTest
  HiResShapesTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/LigatureControl.cpp
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

target_compile_definitions(HiResShapesTest PRIVATE CROSSPOINT_RENDER_SCALE=3)

target_compile_options(HiResShapesTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(HiResShapesTest PRIVATE
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

target_link_libraries(HiResShapesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HiResShapesTest WORKING_DIRECTORY ${REPO_ROOT})
