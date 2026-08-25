# Return/OK arrowhead geometry.
#
# Built TWICE from one source. The apex is exactly where the two resolutions
# disagree -- fillPolygon rasterizes at device resolution above scale 1, so the
# hypotenuse sharpens with the panel instead of staircasing at a third of it --
# and a suite built at only one scale would miss whichever half regressed.
#   scale 1: every device build (X3, X4)
#   scale 3: the value the shipped iOS app pins
#            (crosspoint-simulator/ios/CMakeLists.txt:89,208)
#
# Source list mirrors test/hires_shapes: GfxRenderer.cpp does not link without
# the font and inflate objects even though nothing here draws text.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

function(cp_add_return_arrow_target target scale)
  add_executable(${target}
    ReturnArrowTest.cpp
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
  target_compile_definitions(${target} PRIVATE CROSSPOINT_RENDER_SCALE=${scale})
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
  target_link_libraries(${target} PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(${target} WORKING_DIRECTORY ${REPO_ROOT})
endfunction()

cp_add_return_arrow_target(ReturnArrowTest 1)
cp_add_return_arrow_target(ReturnArrowHiResTest 3)
