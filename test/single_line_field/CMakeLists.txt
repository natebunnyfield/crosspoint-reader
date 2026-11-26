# The one-line text field's window (src/components/SingleLineField.h): owner
# 2026-11-26, "as before, text field needs to be one line". Render and the
# tap-to-cursor mapping share it, so its boundaries are what keep a tap on the
# right byte.
add_executable(SingleLineFieldTest SingleLineFieldTest.cpp)
target_include_directories(SingleLineFieldTest BEFORE PRIVATE
  ${REPO_ROOT}/src
)
target_compile_options(SingleLineFieldTest PRIVATE -Wall -Wextra -pedantic)
target_link_libraries(SingleLineFieldTest PRIVATE GTest::gtest_main)
add_test(NAME SingleLineFieldTest COMMAND SingleLineFieldTest)
