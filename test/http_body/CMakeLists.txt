# fetchUrlToBuffer: the whole-body reader that replaced the std::string
# accumulator which aborted the device in 1.5.29-BD (B-053).
#
# fetchUrlToBuffer is defined inline in HttpDownloader.h precisely so a suite
# can exercise it over a substituted fetchUrlWithHeaders without linking the
# transport. That substitution is this file's Fake.cpp -- there is no network
# here and no way to reach one.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(HttpBodyTest
  HttpBodyTest.cpp
  Fake.cpp
)

target_compile_options(HttpBodyTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(HttpBodyTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(HttpBodyTest PRIVATE
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(HttpBodyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HttpBodyTest)
