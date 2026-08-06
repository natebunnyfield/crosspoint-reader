# Note filenames. Pure: no RTC, no SD, no Arduino.
add_executable(NoteNamingTest NoteNamingTest.cpp)
target_include_directories(NoteNamingTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(NoteNamingTest PRIVATE GTest::gtest_main)
add_test(NAME NoteNamingTest COMMAND NoteNamingTest)
