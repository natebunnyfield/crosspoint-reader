# Activity / input-edge suite.
#
# Links the REAL activities, the REAL MappedInputManager, the REAL
# ButtonNavigator and the REAL UITheme + Lyra theme chain host-side, following
# test/renderer_bounds' pattern for the renderer + font stack (and its
# -include Arduino.h trick, CXX only, because the firmware sources assume the
# Arduino prelude is in scope while minibidi.c / tinflate.c /
# uzlib_checksums.c are C).
#
# What is a double rather than the real thing, and why — full rationale in
# HostHarness.cpp:
#   * ActivityManager  — the real goTo* family make_unique's every activity,
#                        dragging in Boot/Home/Crash/Sleep/Reader/Settings
#                        vtables. loop()'s deferred push/pop semantics, which the
#                        suite depends on, are mirrored exactly.
#   * CrossPointSettings — real class and real field defaults, but the .cpp is
#                        not linked (JsonSettingsIO / ArduinoJson / file I/O).
#   * SdCardFontSystem — doubled so ensureLoaded() can be observed, which is how
#                        the RenderLock tests check the lock is actually held.
#   * HalGPIO          — stubs/HalGPIO.h, the scriptable input seam.
#   * HalClock / HalPowerManager — test/host_stubs/, headers only. They exist
#                        solely because BaseTheme.cpp includes them and their
#                        real versions reach ESP-IDF; nothing here reads a clock
#                        and only the battery icon reads the power manager.
#
# UITheme was a double until the theme stack became linkable (see
# crosspoint_add_theme_stack at the bottom of this file). It is the real one
# now, so render() is a legal call here.
#
# stubs/ is FIRST on the include path so its HalGPIO / HalStorage / freertos
# headers supersede tools/calendar_preview's and the real lib/hal ones.
#
# EpubReaderActivity is deliberately NOT linked (124 project-level undefined
# symbols); the launcher side of the contract is a double in
# ActivityInputTest.cpp.

set(CP_PREVIEW "${REPO_ROOT}/tools/calendar_preview")

# I18nStrings.cpp is generated at build time by scripts/gen_i18n.py (gitignored,
# like I18nKeys.h / I18nStrings.h). Every activity here uses tr(), so fail with
# the command to run rather than cmake's "cannot find source file".
# ArduinoJson comes from PlatformIO's lib_deps, not from the repo, so it only
# exists under .pio/libdeps after a pio build has run. CrossPointSettings.h
# includes it and MappedInputManager.cpp includes that, so this target cannot
# compile without it -- it never could, which is why this test has been
# unbuildable. test/CMakeLists.txt resolves it, primary-checkout fallback
# included.
if(NOT CROSSPOINT_ARDUINOJSON_DIR)
  message(FATAL_ERROR
    "ArduinoJson not found under .pio/libdeps/*/ArduinoJson/src (nor the primary checkout's).\n"
    "Run any PlatformIO build first, e.g.:  pio run -e simulator_x3")
endif()
set(_cp_arduinojson "${CROSSPOINT_ARDUINOJSON_DIR}")

if(NOT EXISTS "${REPO_ROOT}/lib/I18n/I18nStrings.cpp")
  message(FATAL_ERROR
    "lib/I18n/I18nStrings.cpp is missing (generated). Run:\n"
    "  python scripts/gen_i18n.py lib/I18n/translations lib/I18n/")
endif()

add_executable(ActivityInputTest
  ActivityInputTest.cpp
  TypedTextEntryTest.cpp
  ClaudeChatSendTest.cpp
  ListPagingTest.cpp
  HostHarness.cpp
  # activity + input layer under test
  ${REPO_ROOT}/src/MappedInputManager.cpp
  # Both font pickers and the Claude answer view push the grayscale text
  # overlay through TextAa::overlay.
  ${REPO_ROOT}/src/TextAntiAliasing.cpp
  ${REPO_ROOT}/src/activities/Activity.cpp
  ${REPO_ROOT}/src/activities/settings/FontSelectionActivity.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/src/activities/util/IntervalSelectionActivity.cpp
  # Host keyboard text entry (TypedTextEntryTest.cpp). The daisywheel is the
  # text-entry activity that links host-side; the grid keyboard would drag the
  # SDK keyboard component in with it, and is covered end-to-end by the
  # simulator instead — that file says which half is tested where.
  ${REPO_ROOT}/src/activities/util/DaisyEntryActivity.cpp
  ${REPO_ROOT}/src/util/ButtonNavigator.cpp
  # Claude chat + its on-screen keyboard panel (ClaudeChatSendTest.cpp). This
  # DOES drag the SDK keyboard component in, which is why the note above says
  # the daisywheel was the cheap one — the OK-key defect is worth the tables.
  # blekbd and claudechat::runExchange are doubled inside the test file; both
  # reach ESP-IDF and neither is under test.
  ${REPO_ROOT}/src/activities/util/ClaudeChatActivity.cpp
  ${REPO_ROOT}/src/notes/KeyboardPanel.cpp
  ${REPO_ROOT}/src/notes/EditorFonts.cpp
  ${REPO_ROOT}/src/ReadingFontList.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
  # i18n (tr() is used by every activity above)
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
  # renderer + font stack, same list as test/renderer_bounds
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/LigatureControl.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  # FontSelectionActivity::resolvedPointSize() reaches
  # SdCardFontRegistry::findFamily and findClosestReaderSize, and the
  # sdFontSystem global embeds a registry + manager.
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontManager.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  ${CP_PREVIEW}/uzlib_checksums.c
)

target_compile_options(ActivityInputTest PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-include$<SEMICOLON>Arduino.h>
)

target_include_directories(ActivityInputTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(ActivityInputTest PRIVATE
  ${_cp_arduinojson}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CP_PREVIEW}
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/uzlib/src
  # Headers only: FontSelectionActivity reaches <Epub.h> transitively, which pulls
  # BookMetadataCache -> BufferedFile / ZipFile / the XML helpers. No .cpp from any
  # of these is compiled — the input paths under test never touch the book.
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/ZipFile
)

# gtest, not gtest_main: main() must _exit() past ~ActivityManager()'s
# assert(false) — see the comment on main() in ActivityInputTest.cpp.
target_link_libraries(ActivityInputTest PRIVATE
  crosspoint_test_common
  GTest::gtest
)

# The REAL UITheme + BaseTheme/Lyra/Lyra3Covers/LyraSix chain. Activities read
# the theme outside render() — FontSelectionActivity::onEnter sizes its list
# with GUI.getListRowStep(), a virtual LyraTheme overrides — so the old null-
# currentTheme double could not run those tests at all.
crosspoint_add_theme_stack(ActivityInputTest ${REPO_ROOT})

gtest_discover_tests(ActivityInputTest)
