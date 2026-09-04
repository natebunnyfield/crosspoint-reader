# Dark-mode image counter-inversion suite.
#
# Links the REAL firmware GfxRenderer host-side against the stub HAL in
# tools/calendar_preview, the same arrangement renderer_bounds uses. No fonts
# are registered: preserveImagePolarity is pure framebuffer bit-masking, so the
# tests draw nothing and read the panel buffer back directly.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(ImagePolarityTest
  ImagePolarityTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  # The image-dimension guard's overflow pin (ValidatesImageDimensions* below).
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
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
target_compile_options(ImagePolarityTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(ImagePolarityTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Epub/Epub/converters
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
)

target_link_libraries(ImagePolarityTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ImagePolarityTest)
