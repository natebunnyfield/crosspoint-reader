# Untrusted-input suite (B-023, B-024).
#
# Values read out of a file on the card, used to size an allocation or index a
# buffer. Serialization.h is header-only and PngBitDepth.h was extracted from
# PngToBmpConverter.cpp precisely so the rule could be tested without a HalFile,
# a decode context, or a real PNG on disk.
#
# HalStorage.h comes from the calendar_preview stub headers, the same way the
# renderer_bounds suite gets its HAL.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(UntrustedInputTest
  UntrustedInputTest.cpp
)

target_compile_options(UntrustedInputTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(UntrustedInputTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/PngToBmpConverter
  ${REPO_ROOT}/lib/hal
)

target_link_libraries(UntrustedInputTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UntrustedInputTest)
