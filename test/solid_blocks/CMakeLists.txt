# Solid block symbols (upstream 7394e40fa): U+2588 and U+25A0 drawn as rectangles when the reading
# font lacks them. Links the REAL GfxRenderer / EpdFont / built-in faces host-side (as rotated_text
# does) and reads the framebuffer back, because the rule is about WHERE the ink lands in each draw
# path: upright, rotated, sup/sub, dark, and the scan pass.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(SolidBlocksTest
  SolidBlocksTest.cpp
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

target_compile_definitions(SolidBlocksTest PRIVATE GFX_BOUNDS_COUNTER)

# -include Arduino.h mirrors tools/calendar_preview/build.sh: the firmware
# sources assume the Arduino prelude is already in scope. CXX only — Arduino.h
# is C++ (<cstdint>), and minibidi.c / tinflate.c / uzlib_checksums.c are C,
# which is why build.sh compiles those separately with gcc and no prelude.
target_compile_options(SolidBlocksTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(SolidBlocksTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/Epub/Epub/parsers
)

target_link_libraries(SolidBlocksTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SolidBlocksTest)
