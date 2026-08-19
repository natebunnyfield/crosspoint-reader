cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

# crosspoint_add_theme_stack(): links the real UITheme + Lyra theme chain
# host-side. See test/cmake/HostThemeStack.cmake for what a caller must still
# supply.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/HostThemeStack.cmake)

# ---------------------------------------------------------------------------
# Two things this tree needs are absent from a `git worktree` and present only
# in the primary checkout, and BOTH fail silently rather than loudly:
#
#   .pio/libdeps  PlatformIO writes it per checkout. activity_input needs
#                 ArduinoJson from it and FATAL_ERRORs without it, so at least
#                 that one is loud.
#   fs_           the SD-card tree, gitignored (.gitignore:6). font_switch_churn
#                 and system_font GTEST_SKIP without fonts — nine tests quietly
#                 stop running and the suite still reports 100% pass.
#
# So resolve the primary checkout and fall back to it for both. Symlinking would
# also work but mutates the worktree, and a shared .pio/libdeps means a stray
# `pio run` here writes into the primary checkout's package tree.
find_package(Git QUIET)
execute_process(
  COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute --git-common-dir
  WORKING_DIRECTORY ${REPO_ROOT}
  OUTPUT_VARIABLE _cp_git_common
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(_cp_git_common)
  # <primary>/.git -> <primary>. Identical to REPO_ROOT outside a worktree.
  get_filename_component(CROSSPOINT_PRIMARY_ROOT "${_cp_git_common}" DIRECTORY)
endif()

# The SD-card tree the font suites read, exported to them as CROSSPOINT_TEST_SD.
# Empty means "none found" — those suites then skip, which is the honest answer.
set(CROSSPOINT_TEST_SD_DIR "")
if(EXISTS "${REPO_ROOT}/fs_")
  set(CROSSPOINT_TEST_SD_DIR "${REPO_ROOT}/fs_")
elseif(CROSSPOINT_PRIMARY_ROOT AND EXISTS "${CROSSPOINT_PRIMARY_ROOT}/fs_")
  set(CROSSPOINT_TEST_SD_DIR "${CROSSPOINT_PRIMARY_ROOT}/fs_")
  message(STATUS "SD-card tree: borrowing ${CROSSPOINT_TEST_SD_DIR} (this worktree has no fs_)")
endif()
if(NOT CROSSPOINT_TEST_SD_DIR)
  message(STATUS "SD-card tree: none found — the font suites will skip")
endif()

# ArduinoJson comes from PlatformIO's lib_deps, not the repo, so it exists only
# under .pio/libdeps after a pio build has run. Header-only, so an include path
# is all any suite needs; any env's copy will do, they are the same package.
# Empty means "not found" — the suites that need it FATAL_ERROR themselves.
file(GLOB _cp_arduinojson_dirs "${REPO_ROOT}/.pio/libdeps/*/ArduinoJson/src")
if(NOT _cp_arduinojson_dirs AND CROSSPOINT_PRIMARY_ROOT)
  file(GLOB _cp_arduinojson_dirs "${CROSSPOINT_PRIMARY_ROOT}/.pio/libdeps/*/ArduinoJson/src")
  if(_cp_arduinojson_dirs)
    message(STATUS "ArduinoJson: borrowing the primary checkout's (this worktree has no .pio)")
  endif()
endif()
set(CROSSPOINT_ARDUINOJSON_DIR "")
if(_cp_arduinojson_dirs)
  list(GET _cp_arduinojson_dirs 0 CROSSPOINT_ARDUINOJSON_DIR)
endif()

# JPEGDEC, same story as ArduinoJson with one addition: a pio build does not
# merely place it, it PATCHES it in place (scripts/patch_jpegdec.py). Any env's
# copy will do -- they are the same package and all get the same patches.
# progressive_jpeg FATAL_ERRORs on both an absent copy and an unpatched one.
file(GLOB _cp_jpegdec_dirs "${REPO_ROOT}/.pio/libdeps/*/JPEGDEC/src")
if(NOT _cp_jpegdec_dirs AND CROSSPOINT_PRIMARY_ROOT)
  file(GLOB _cp_jpegdec_dirs "${CROSSPOINT_PRIMARY_ROOT}/.pio/libdeps/*/JPEGDEC/src")
  if(_cp_jpegdec_dirs)
    message(STATUS "JPEGDEC: borrowing the primary checkout's (this worktree has no .pio)")
  endif()
endif()
set(CROSSPOINT_JPEGDEC_DIR "")
if(_cp_jpegdec_dirs)
  list(GET _cp_jpegdec_dirs 0 CROSSPOINT_JPEGDEC_DIR)
endif()

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(holidays)
add_subdirectory(renderer_bounds)
add_subdirectory(hires_shapes)
add_subdirectory(return_arrow)
add_subdirectory(image_polarity)
add_subdirectory(dark_mode_polarity)
add_subdirectory(font_switch_churn)
add_subdirectory(system_font)
add_subdirectory(sleep_screen)
add_subdirectory(activity_input)
add_subdirectory(word_anchor)
add_subdirectory(ble_keymap)
add_subdirectory(markdown_spans)
add_subdirectory(text_buffer)
add_subdirectory(note_naming)
add_subdirectory(keyboard_panel)
add_subdirectory(editor_fonts)
add_subdirectory(untrusted_input)
add_subdirectory(settings_display_order)
add_subdirectory(editor_font_preview)
add_subdirectory(omit_fonts)
add_subdirectory(plain_text_files)
add_subdirectory(table_cell_label)
add_subdirectory(table_columns)
add_subdirectory(read_aloud_capture)
add_subdirectory(credential_integrity)
add_subdirectory(text_antialiasing)
add_subdirectory(progressive_jpeg)
