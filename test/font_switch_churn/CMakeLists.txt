# Font-switch churn suite.
#
# Links the real registry + manager + renderer host-side. `stubs/` comes FIRST
# on the include path so its fuller HalStorage (with directory iteration)
# supersedes tools/calendar_preview's read-only one — SdCardFontRegistry needs
# Storage.open()/openNextFile() and will not link against the harness stub.
#
# WORKING_DIRECTORY is the repo root because the stub rebases device paths onto
# ./fs_, where the real .cpfont tree lives.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(FontSwitchChurnTest
  FontSwitchChurnTest.cpp
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

target_compile_options(FontSwitchChurnTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(FontSwitchChurnTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(FontSwitchChurnTest PRIVATE
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

target_link_libraries(FontSwitchChurnTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

# WORKING_DIRECTORY makes the stub's default "./fs_" the repo root's. That is
# empty in a git worktree, so CROSSPOINT_TEST_SD points at wherever
# test/CMakeLists.txt found an SD tree — normally the primary checkout's.
gtest_discover_tests(FontSwitchChurnTest
  WORKING_DIRECTORY ${REPO_ROOT}
  PROPERTIES ENVIRONMENT "CROSSPOINT_TEST_SD=${CROSSPOINT_TEST_SD_DIR}"
)
