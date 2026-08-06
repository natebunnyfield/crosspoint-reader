# Editable buffer + cursor. Pure logic: no renderer, no Arduino, no device.
add_executable(TextBufferTest TextBufferTest.cpp)
target_include_directories(TextBufferTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(TextBufferTest PRIVATE GTest::gtest_main)
add_test(NAME TextBufferTest COMMAND TextBufferTest)
