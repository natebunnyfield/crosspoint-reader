# One-time migration of the justification threshold 40 -> 34 (owner ruling
# 2026-09-26). Header-only subject: src/JustifyThresholdMigration.h, which
# CrossPointSettings::fromJson/toJson call. Needs ArduinoJson from a pio build,
# like test/sleep_screen.
if(NOT CROSSPOINT_ARDUINOJSON_DIR)
  message(FATAL_ERROR "ArduinoJson not found; run any PlatformIO build first, e.g.: pio run -e simulator")
endif()
add_executable(JustifyMigrationTest JustifyMigrationTest.cpp)
target_include_directories(JustifyMigrationTest PRIVATE
  ${CROSSPOINT_ARDUINOJSON_DIR}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
)
target_link_libraries(JustifyMigrationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(JustifyMigrationTest)
