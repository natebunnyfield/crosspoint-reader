# B-036: measurement and rendering must agree about kerning across a missing
# glyph. Links the real GfxRenderer + EpdFont host-side and pins
# EpdFont::getTextBounds and GfxRenderer::getTextAdvanceX (the same loop shape
# drawText uses) to one width over a synthetic font whose kern classes list a
# codepoint its intervals do not carry -- the shape SD-font pruning produces.
set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(MissingGlyphKernTest
  MissingGlyphKernTest.cpp
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

target_compile_definitions(MissingGlyphKernTest PRIVATE GFX_BOUNDS_COUNTER)

# -include Arduino.h mirrors tools/calendar_preview/build.sh: the firmware
# sources assume the Arduino prelude is already in scope. CXX only — Arduino.h
# is C++ (<cstdint>), and minibidi.c / tinflate.c / uzlib_checksums.c are C,
# which is why build.sh compiles those separately with gcc and no prelude.
target_compile_options(MissingGlyphKernTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(MissingGlyphKernTest PRIVATE
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

target_link_libraries(MissingGlyphKernTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MissingGlyphKernTest)
