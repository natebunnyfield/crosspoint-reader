# Rotated text suite.
#
# Links the REAL firmware GfxRenderer / EpdFont / builtin fonts host-side, the
# same way tools/calendar_preview/build.sh does, and reads the framebuffer back
# to check where the ink actually landed.
#
# It exists because drawTextRotated90CCW (T-021) is a mirror written by hand:
# four coordinate branches inside a template, any one of which can be wrong in a
# way that still compiles, still draws something, and is only visible as a page
# of sideways text nobody can read.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(RotatedTextTest
  RotatedTextTest.cpp
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

target_compile_definitions(RotatedTextTest PRIVATE GFX_BOUNDS_COUNTER)

# -include Arduino.h mirrors tools/calendar_preview/build.sh: the firmware
# sources assume the Arduino prelude is already in scope. CXX only — Arduino.h
# is C++ (<cstdint>), and minibidi.c / tinflate.c / uzlib_checksums.c are C,
# which is why build.sh compiles those separately with gcc and no prelude.
target_compile_options(RotatedTextTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(RotatedTextTest PRIVATE
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

target_link_libraries(RotatedTextTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RotatedTextTest)
