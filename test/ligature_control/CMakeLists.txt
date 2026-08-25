add_executable(LigatureControlTest
  LigatureControlTest.cpp
  ${REPO_ROOT}/lib/EpdFont/LigatureControl.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(LigatureControlTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(LigatureControlTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LigatureControlTest)
