# Picker display order vs stored value. Pure table logic — SettingDisplayOrder.h
# is header-only with no Arduino, renderer or settings-singleton dependency, and
# EditorFonts.cpp is equally standalone, so this links one file and nothing else.
add_executable(SettingDisplayOrderTest
  SettingDisplayOrderTest.cpp
  ${REPO_ROOT}/src/notes/EditorFonts.cpp
)
target_include_directories(SettingDisplayOrderTest PRIVATE
  ${REPO_ROOT}/src
)
target_link_libraries(SettingDisplayOrderTest PRIVATE GTest::gtest_main)
add_test(NAME SettingDisplayOrderTest COMMAND SettingDisplayOrderTest)
