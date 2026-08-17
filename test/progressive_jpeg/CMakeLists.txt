# Decodes through the REAL vendored JPEGDEC, patches and all -- a stub would
# prove nothing here, since the bug under test lives in the decoder's MCU loop.
#
# JPEGDEC comes from PlatformIO's lib_deps, so it exists only under
# .pio/libdeps after a `pio run`, and it is patched in place by
# scripts/patch_jpegdec.py during that build. Both facts are checked, loudly:
# a missing or unpatched copy makes this suite meaningless rather than red.
if(NOT CROSSPOINT_JPEGDEC_DIR)
  message(FATAL_ERROR
    "progressive_jpeg needs JPEGDEC from .pio/libdeps, which appears only after a "
    "PlatformIO build. Run `pio run -e default` first.")
endif()

file(READ "${CROSSPOINT_JPEGDEC_DIR}/jpeg.inl" _cp_jpeg_inl)
string(FIND "${_cp_jpeg_inl}" "bNonInterleavedDC" _cp_patch_pos)
unset(_cp_jpeg_inl)
if(_cp_patch_pos EQUAL -1)
  message(FATAL_ERROR
    "The JPEGDEC copy at ${CROSSPOINT_JPEGDEC_DIR} is missing patch 0003 "
    "(scripts/jpegdec_patches/). A `pio run` applies it. Testing an unpatched "
    "decoder would report the bug this suite exists to prevent.")
endif()

add_executable(ProgressiveJpegTest
  ProgressiveJpegTest.cpp
  ${CROSSPOINT_JPEGDEC_DIR}/JPEGDEC.cpp
)

target_include_directories(ProgressiveJpegTest PRIVATE
  ${CROSSPOINT_JPEGDEC_DIR}
)

target_compile_definitions(ProgressiveJpegTest PRIVATE
  CROSSPOINT_JPEG_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

# Vendored third-party code: its warnings are not ours to fix, and -Werror-clean
# is not a promise this suite can keep for it.
target_compile_options(ProgressiveJpegTest PRIVATE -Wno-unused-parameter)

target_link_libraries(ProgressiveJpegTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ProgressiveJpegTest)
