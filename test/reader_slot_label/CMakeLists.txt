# Reader font-size slot LABELS. Header-only logic (ReaderFontSizes.h), so this
# links nothing but the test itself.
#
# Two of its cases read repo SOURCE as text rather than linking it: the screens
# that render these labels live in activities that cannot be linked here (the
# Arduino stack, SdFat, the renderer), and the invariant under test is that
# NEITHER screen carries its own copy of the name table. Same call
# EditorFontsTest and OmitFontsTest already make, for the same reason.
add_executable(ReaderSlotLabelTest ReaderSlotLabelTest.cpp)
target_include_directories(ReaderSlotLabelTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
)
target_compile_definitions(ReaderSlotLabelTest PRIVATE
  CROSSPOINT_SETTINGS_LIST_H="${REPO_ROOT}/src/SettingsList.h"
  CROSSPOINT_FONT_SELECTION_CPP="${REPO_ROOT}/src/activities/settings/FontSelectionActivity.cpp")
target_link_libraries(ReaderSlotLabelTest PRIVATE GTest::gtest_main)
add_test(NAME ReaderSlotLabelTest COMMAND ReaderSlotLabelTest)
