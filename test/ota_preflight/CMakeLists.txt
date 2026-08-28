# The OTA pre-flight policy. Header-only and clock-free, so this links nothing:
# the caller supplies the observations and the elapsed times.
add_executable(OtaPreflightTest OtaPreflightTest.cpp)
target_include_directories(OtaPreflightTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(OtaPreflightTest PRIVATE GTest::gtest_main)
add_test(NAME OtaPreflightTest COMMAND OtaPreflightTest)
