# The streaming manifest parser, on its own.
#
# It replaced a buffered ArduinoJson parse whose 18 KB contiguous block was the
# largest single demand Update Fonts made (B-053). A streaming parser trades
# that memory for a state machine, and the failure modes move with it: a
# buffered parse gets "the document was truncated" for free, and this one has to
# be told to care. That is what most of these tests are about.
add_executable(FontManifestTest
  FontManifestTest.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(FontManifestTest PRIVATE
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(FontManifestTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontManifestTest)
