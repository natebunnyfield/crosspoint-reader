# Renderer bounds suite.
#
# Links the REAL firmware GfxRenderer / EpdFont / builtin fonts host-side, the
# same way tools/calendar_preview/build.sh does. The HAL is supplied by the
# stub headers in tools/calendar_preview (HalDisplay is the X3 792x528 panel —
# the device the crash report came from).
#
# GFX_BOUNDS_COUNTER turns drawPixel()'s silent out-of-range clip into an
# assertable counter. Without it these tests can observe nothing.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(RendererBoundsTest
  RendererBoundsTest.cpp
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

target_compile_definitions(RendererBoundsTest PRIVATE GFX_BOUNDS_COUNTER)

# -include Arduino.h mirrors tools/calendar_preview/build.sh: the firmware
# sources assume the Arduino prelude is already in scope. CXX only — Arduino.h
# is C++ (<cstdint>), and minibidi.c / tinflate.c / uzlib_checksums.c are C,
# which is why build.sh compiles those separately with gcc and no prelude.
target_compile_options(RendererBoundsTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(RendererBoundsTest PRIVATE
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

target_link_libraries(RendererBoundsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RendererBoundsTest)
