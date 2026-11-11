# Per-glyph fast path (GlyphBitmap.h, upstream 04ca1d53c).
#
# GlyphBitmapTest: the header-only rasterizer against a per-pixel reference
# (upstream's suite, adapted to this fork's explicit level masks).
#
# GlyphFastPathTest / GlyphFastPathTest2x: the REAL GfxRenderer rendering the
# same text with the fast path on and off, byte-compared. Linked against the
# stub HAL in tools/calendar_preview like test/rotated_text. Two scales: 1 is
# the device's case; 2 adds hi-res companions, which take the deviceSpace fast
# path, while the 1x faces must be declined. GFX_GLYPH_FASTPATH_TOGGLE is the
# test-only switch; GFX_BOUNDS_COUNTER makes the out-of-range count comparable.

add_executable(GlyphBitmapTest GlyphBitmapTest.cpp)
target_link_libraries(GlyphBitmapTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(GlyphBitmapTest)

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

foreach(scale 1 2)
  if(scale EQUAL 1)
    set(target GlyphFastPathTest)
  else()
    set(target GlyphFastPathTest${scale}x)
  endif()
  add_executable(${target}
    GlyphFastPathTest.cpp
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
  target_compile_definitions(${target} PRIVATE
    GFX_GLYPH_FASTPATH_TOGGLE
    GFX_BOUNDS_COUNTER
    CROSSPOINT_RENDER_SCALE=${scale}
  )
  # -include Arduino.h mirrors tools/calendar_preview/build.sh. CXX only.
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
  )
  target_include_directories(${target} PRIVATE
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
  target_link_libraries(${target} PRIVATE
    crosspoint_test_common
    GTest::gtest_main
  )
  gtest_discover_tests(${target})
endforeach()
