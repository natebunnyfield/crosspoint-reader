# Grayscale text anti-aliasing pass.
#
# Links the REAL firmware GfxRenderer and TextAntiAliasing.cpp host-side against
# the stub HAL in tools/calendar_preview — the same arrangement image_polarity
# uses. TextAntiAliasingPass.h is deliberately free of CrossPointSettings, which
# is what makes this linkable without ArduinoJson and the persisted store; the
# settings half lives in TextAntiAliasing.h and is not exercised here.
#
# No fonts are registered: the pass is tested through its contract with the
# caller (framebuffer preserved, panel covered once per plane, render mode
# restored), not through glyph pixels.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(TextAntiAliasingTest
  TextAntiAliasingTest.cpp
  ${REPO_ROOT}/src/TextAntiAliasing.cpp
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

# -include Arduino.h mirrors tools/calendar_preview/build.sh: the firmware
# sources assume the Arduino prelude is already in scope. CXX only — the .c
# sources are C and must not see it.
target_compile_options(TextAntiAliasingTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(TextAntiAliasingTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  # builtinFonts/*.h — one 1-bit and one 2-bit face are linked as fixtures for
  # the "chrome cannot antialias" cases.
  ${REPO_ROOT}/lib/EpdFont/builtinFonts
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
)

target_link_libraries(TextAntiAliasingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextAntiAliasingTest)
