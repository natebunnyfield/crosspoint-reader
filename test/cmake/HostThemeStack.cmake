# The REAL UITheme + theme stack, host-side.
#
# WHY THIS EXISTS
#
# Activity code reads the theme outside render(): FontSelectionActivity::onEnter
# calls GUI.getListRowStep() to size its list (FontSelectionActivity.cpp:105),
# and LyraTheme overrides that virtual — so a null `currentTheme` is a segfault,
# not a missing pixel. test/activity_input used to double UITheme with a null
# theme and could therefore not run any test whose activity had a theme-reading
# onEnter; seven cases were skipped for exactly that.
#
# The one thing that made the real stack unlinkable was an include chain, not a
# behaviour: BaseTheme.cpp -> <HalClock.h> / <HalPowerManager.h> -> BoardConfig.h
# -> ESP-IDF <driver/gpio.h>. test/host_stubs/ shadows those two headers (and
# nothing else), which is why the whole chain below now compiles on the host.
#
# WHAT A CALLER MUST STILL PROVIDE
#
#   * `HalDisplay display;` and `HalGPIO gpio;` — the panel and the input seam.
#     Each suite picks its own (activity_input's HalGPIO is scriptable).
#   * `SdCardFontSystem sdFontSystem;` plus a definition of the members the
#     theme reaches: LyraSixTheme::drawRecentBookCover calls loadForDisplay()
#     (LyraSixTheme.cpp:135).
#   * A HalStorage on the include path — the cover-drawing paths call
#     Storage.openFileForRead().
#   * The i18n strings (tr()) and the renderer/font stack.
#
# The theme sources are the real, unmodified shipped ones. BaseTheme, LyraTheme
# and Lyra3CoversTheme are all four live code: they are the layers LyraSixTheme
# is composed from, not alternative themes (CLAUDE.md, "One theme").

set(CROSSPOINT_HOST_STUBS_DIR "${CMAKE_CURRENT_LIST_DIR}/../host_stubs")

# Adds the real theme stack to `target`. Call BEFORE the target's own
# target_include_directories(... BEFORE ...) block if that block has to win.
function(crosspoint_add_theme_stack target repo_root)
  target_sources(${target} PRIVATE
    ${repo_root}/src/components/UITheme.cpp
    ${repo_root}/src/components/themes/BaseTheme.cpp
    ${repo_root}/src/components/themes/lyra/LyraTheme.cpp
    ${repo_root}/src/components/themes/lyra/Lyra3CoversTheme.cpp
    ${repo_root}/src/components/themes/lyra/LyraSixTheme.cpp
    # UITheme::getFileIcon dispatches on the extension helpers.
    ${repo_root}/lib/FsHelpers/FsHelpers.cpp
    # halClock / powerManager singletons for the stub HALs above.
    ${CROSSPOINT_HOST_STUBS_DIR}/HostThemeStack.cpp
  )
  target_include_directories(${target} BEFORE PRIVATE
    ${CROSSPOINT_HOST_STUBS_DIR}
  )
  target_include_directories(${target} PRIVATE
    ${repo_root}/lib/FsHelpers
  )
endfunction()
