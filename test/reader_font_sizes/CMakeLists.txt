# The reader's six size slots (XXS..XL) and the four places the ramp is written
# down. See the header comment in ReaderFontSizesTest.cpp for why three of the
# four are read as TEXT: linking getReaderFontId() means the settings
# singleton, ArduinoJson and HalStorage for a question about a `switch`, and
# sd-fonts.yaml is not compiled at all.
#
# SdCardFontRegistry.cpp is linked for the real per-family slot selection
# (findClosestReaderSize / availableSizes). It includes HalStorage for the
# card-scanning half this suite never calls, so font_switch_churn's shared stub
# comes first on the include path — the same arrangement sd_font_arrows uses,
# and for the same reason: a second copy would be a second thing to keep true.
set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

add_executable(ReaderFontSizesTest
  ReaderFontSizesTest.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
)
target_compile_options(ReaderFontSizesTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)
target_include_directories(ReaderFontSizesTest BEFORE PRIVATE
  ${REPO_ROOT}/test/font_switch_churn/stubs
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
)
# The suite reads repo sources, so it needs the repo root at compile time
# rather than a path relative to whatever directory ctest happens to run in.
target_compile_definitions(ReaderFontSizesTest PRIVATE CP_REPO_ROOT="${REPO_ROOT}")
target_link_libraries(ReaderFontSizesTest PRIVATE GTest::gtest_main)
add_test(NAME ReaderFontSizesTest COMMAND ReaderFontSizesTest)
