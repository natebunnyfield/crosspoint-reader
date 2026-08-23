add_executable(RubySerializationTest
  RubySerializationTest.cpp
)

# stubs/ ahead of lib/hal so <HalStorage.h> resolves to the in-memory HalFile.
target_include_directories(RubySerializationTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(RubySerializationTest PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(RubySerializationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RubySerializationTest)
