# Read-aloud capture grouping. ReadAloudCapture.h is pure and header-only (the
# rect type is a template parameter, so it needs neither HalGPIO.h nor the
# simulator's channel header), so this links one test file and nothing else.
add_executable(ReadAloudCaptureTest
  ReadAloudCaptureTest.cpp
)
target_include_directories(ReadAloudCaptureTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)
target_link_libraries(ReadAloudCaptureTest PRIVATE GTest::gtest_main)
add_test(NAME ReadAloudCaptureTest COMMAND ReadAloudCaptureTest)
