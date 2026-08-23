add_executable(MissingGlyphLedgerTest
  MissingGlyphLedgerTest.cpp
)

target_include_directories(MissingGlyphLedgerTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
)

target_link_libraries(MissingGlyphLedgerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MissingGlyphLedgerTest)
