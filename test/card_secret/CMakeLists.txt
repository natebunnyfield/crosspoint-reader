# The one-line card secret reader (src/util/CardSecret.h) behind
# /claude-key.txt and /github-token.txt. Header-only, so nothing from the
# firmware is compiled here beyond the header itself; stubs/HalStorage.h is a
# file-backed stand-in for the SD card.
add_executable(CardSecretTest
  CardSecretTest.cpp
)
target_include_directories(CardSecretTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(CardSecretTest PRIVATE
  ${REPO_ROOT}/src
)
target_link_libraries(CardSecretTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)
gtest_discover_tests(CardSecretTest)
