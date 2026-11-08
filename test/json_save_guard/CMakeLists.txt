# JsonSaveGuard: the save-refusal rule writeDocToFile() applies (survey #16).
if(NOT CROSSPOINT_ARDUINOJSON_DIR)
  message(FATAL_ERROR "ArduinoJson not found under .pio/libdeps/*/ArduinoJson/src (nor the primary checkout's).\n"
                      "Run a pio build once so PlatformIO places it.")
endif()
add_executable(JsonSaveGuardTest JsonSaveGuardTest.cpp)
target_include_directories(JsonSaveGuardTest PRIVATE ${REPO_ROOT}/lib/Serialization ${CROSSPOINT_ARDUINOJSON_DIR})
target_link_libraries(JsonSaveGuardTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(JsonSaveGuardTest)
