# The OMIT_FONTS contract. Parses main.cpp rather than linking it: a host test
# cannot link the device stack, and the invariant is textual anyway — which
# side of a preprocessor guard a registration sits on.
add_executable(OmitFontsTest OmitFontsTest.cpp)
target_compile_definitions(OmitFontsTest PRIVATE
  CROSSPOINT_MAIN_CPP="${REPO_ROOT}/src/main.cpp")
target_link_libraries(OmitFontsTest PRIVATE GTest::gtest_main)
add_test(NAME OmitFontsTest COMMAND OmitFontsTest)
