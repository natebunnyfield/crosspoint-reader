#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>
#include <builtinFonts/all.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SystemFont.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/SleepScreenPolicy.h"
#ifndef CROSSPOINT_NO_DEVICE_FLASH
#include "activities/settings/SdFirmwareUpdateActivity.h"
#endif
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/LoadingIcon.h"
#include "util/ButtonNavigator.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts()
#if CROSSPOINT_RENDER_SCALE > 1
                                                             ,
                                  renderer.getHiResSdCardFonts()
#endif
);
static unsigned long allowSleepAt = 0;
// Inactivity clock for auto-sleep. File scope, not a loop()-local static, because
// the simulator's iOS build wakes from deep sleep with a longjmp back into the
// same process instead of relaunching it. A function-local static keeps its
// pre-sleep value across that jump while millis() keeps counting, so the first
// post-wake loop() sees a stale timestamp already past the timeout and sleeps
// again immediately — the device looks unwakeable. setup() resets this
// explicitly; on real hardware that is a no-op, since a chip reset would have
// re-initialized it anyway.
static unsigned long lastActivityTime = 0;

// Fonts
//
// Libre Franklin is the only built-in reading family (owner ruling 2026-08-07;
// Noto Serif, Noto Sans and Ubuntu were removed outright). The reader cuts are
// distinct symbols from the system-font matrix's 12 pt below — 2-bit
// compressed with italics versus the chrome's plain 1-bit regular/bold — so
// regenerating one can never silently downgrade the other, which was a real
// trap when Noto shared its 12 pt cut between the two roles.
//
// 14 pt sits outside OMIT_FONTS because getReaderFontId()'s default answer
// must resolve in every build, same as Noto Serif 14 always did.
EpdFont lfReader14RegularFont(&librefranklin_reader_14_regular);
EpdFont lfReader14BoldFont(&librefranklin_reader_14_bold);
EpdFont lfReader14ItalicFont(&librefranklin_reader_14_italic);
EpdFont lfReader14BoldItalicFont(&librefranklin_reader_14_bolditalic);
EpdFontFamily librefranklinReader14FontFamily(&lfReader14RegularFont, &lfReader14BoldFont, &lfReader14ItalicFont,
                                              &lfReader14BoldItalicFont);

// Editor-group monospace (owner ruling 2026-08-06). Built in rather than read
// from the card: the editor asks for one size in four styles, ~83 KB for both
// families, and a built-in face works on a blank card AND cannot appear in the
// reading picker — an SD install into /fonts would do both the opposite ways.
// All four styles are real: MarkdownSpans renders bold, italic and bold-italic.
//
// OUTSIDE OMIT_FONTS, for the same reason Libre Franklin 14 pt is: all five
// editor rows must resolve in EVERY build. They were inside it until 2026-08-09
// (the two monos) and 2026-08-11 (the three iA faces), and the only build that
// defines the macro is iOS (crosspoint-simulator/ios/CMakeLists.txt), so on the
// phone the whole Editor Font setting collapsed — builtinFontIdFor() handed
// back ids the renderer had no glyphs for, and resolve() fell through to
// UI_10 chrome. "Built in" has to mean built into the binary that ships.
//
// Flash cost: ~41 KB for IBMPlexMono, ~216 KB for the three iA families (ruling
// 2026-08-11). The _2x companions are dead flash on device (RENDER_SCALE=1,
// stripped by --gc-sections); they are present only for the simulator / iOS
// builds that run at RENDER_SCALE=2.
//
// Space Mono was here until 2026-08-15 and is gone by owner ruling. Its
// generated headers are still in builtinFonts/ and its recipe is still in
// sd-fonts.yaml — only the wiring was removed, so nothing here references it
// and it costs no flash. See the note above FAMILIES in notes/EditorFonts.h.
EpdFont ibmplexmono12RegularFont(&ibmplexmono_12_regular);
EpdFont ibmplexmono12BoldFont(&ibmplexmono_12_bold);
EpdFont ibmplexmono12ItalicFont(&ibmplexmono_12_italic);
EpdFont ibmplexmono12BoldItalicFont(&ibmplexmono_12_bolditalic);
EpdFontFamily ibmplexmono12FontFamily(&ibmplexmono12RegularFont, &ibmplexmono12BoldFont, &ibmplexmono12ItalicFont,
                                      &ibmplexmono12BoldItalicFont);
EpdFont iawriterquattro12RegularFont(&iawriterquattro_12_regular);
EpdFont iawriterquattro12BoldFont(&iawriterquattro_12_bold);
EpdFont iawriterquattro12ItalicFont(&iawriterquattro_12_italic);
EpdFont iawriterquattro12BoldItalicFont(&iawriterquattro_12_bolditalic);
EpdFontFamily iawriterquattro12FontFamily(&iawriterquattro12RegularFont, &iawriterquattro12BoldFont,
                                          &iawriterquattro12ItalicFont, &iawriterquattro12BoldItalicFont);
EpdFont iawriterduo12RegularFont(&iawriterduo_12_regular);
EpdFont iawriterduo12BoldFont(&iawriterduo_12_bold);
EpdFont iawriterduo12ItalicFont(&iawriterduo_12_italic);
EpdFont iawriterduo12BoldItalicFont(&iawriterduo_12_bolditalic);
EpdFontFamily iawriterduo12FontFamily(&iawriterduo12RegularFont, &iawriterduo12BoldFont, &iawriterduo12ItalicFont,
                                      &iawriterduo12BoldItalicFont);
EpdFont iawritermono12RegularFont(&iawritermono_12_regular);
EpdFont iawritermono12BoldFont(&iawritermono_12_bold);
EpdFont iawritermono12ItalicFont(&iawritermono_12_italic);
EpdFont iawritermono12BoldItalicFont(&iawritermono_12_bolditalic);
EpdFontFamily iawritermono12FontFamily(&iawritermono12RegularFont, &iawritermono12BoldFont, &iawritermono12ItalicFont,
                                       &iawritermono12BoldItalicFont);

// PragmataPro is COMMERCIAL, so unlike every other family above its glyph
// tables are NOT in this repo: builtinFonts/pragmatapro_*.h are gitignored and
// regenerated locally by convert-builtin-fonts.sh from lib/EpdFont/local_fonts/.
// __has_include is what lets both trees compile from one source — a clone
// without the licensed TTFs simply does not register the row, and
// editorfonts::resolve() already refuses to return an id the renderer has no
// glyphs for, so it degrades to a built-in mono exactly like a card-only row.
// Deliberately NOT added to builtinFonts/all.h: that header is committed and
// unconditional, so a missing include there would break the build for everyone.
#if __has_include(<builtinFonts/pragmatapro_12_regular.h>)
#define CROSSPOINT_HAS_PRAGMATAPRO 1
#include <builtinFonts/pragmatapro_12_bold.h>
#include <builtinFonts/pragmatapro_12_bolditalic.h>
#include <builtinFonts/pragmatapro_12_italic.h>
#include <builtinFonts/pragmatapro_12_regular.h>
EpdFont pragmatapro12RegularFont(&pragmatapro_12_regular);
EpdFont pragmatapro12BoldFont(&pragmatapro_12_bold);
EpdFont pragmatapro12ItalicFont(&pragmatapro_12_italic);
EpdFont pragmatapro12BoldItalicFont(&pragmatapro_12_bolditalic);
EpdFontFamily pragmatapro12FontFamily(&pragmatapro12RegularFont, &pragmatapro12BoldFont, &pragmatapro12ItalicFont,
                                      &pragmatapro12BoldItalicFont);
#endif

// NittiTypewriter: same commercial/gitignored pattern as PragmataPro. Only the
// Regular TTF exists; Bold/Italic/BoldItalic are synthesised at build time by
// convert-builtin-fonts.sh using fontconvert.py's --synth-embolden-em 0.045
// --synth-slant-deg 11. The macro carries "NITTITYPEWRITER" so the 2x block
// below can reuse it without repeating the __has_include test.
#if __has_include(<builtinFonts/nittitypewriter_12_regular.h>)
#define CROSSPOINT_HAS_NITTITYPEWRITER 1
#include <builtinFonts/nittitypewriter_12_bold.h>
#include <builtinFonts/nittitypewriter_12_bolditalic.h>
#include <builtinFonts/nittitypewriter_12_italic.h>
#include <builtinFonts/nittitypewriter_12_regular.h>
EpdFont nittitypewriter12RegularFont(&nittitypewriter_12_regular);
EpdFont nittitypewriter12BoldFont(&nittitypewriter_12_bold);
EpdFont nittitypewriter12ItalicFont(&nittitypewriter_12_italic);
EpdFont nittitypewriter12BoldItalicFont(&nittitypewriter_12_bolditalic);
EpdFontFamily nittitypewriter12FontFamily(&nittitypewriter12RegularFont, &nittitypewriter12BoldFont,
                                          &nittitypewriter12ItalicFont, &nittitypewriter12BoldItalicFont);
#endif

#if defined(CROSSPOINT_RENDER_SCALE) && CROSSPOINT_RENDER_SCALE > 1
// 2x companions for all five editor fonts. Named for the 1x face each stands
// in for (same convention as the system UI 2x matrix). drawText() checks
// getHiResFamily() for any font id it is handed, so without a registered
// companion the editor text renders at 1x resolution on a 2x build.
EpdFont ibmplexmono12Regular2xFont(&ibmplexmono_12_regular_2x);
EpdFont ibmplexmono12Bold2xFont(&ibmplexmono_12_bold_2x);
EpdFont ibmplexmono12Italic2xFont(&ibmplexmono_12_italic_2x);
EpdFont ibmplexmono12BoldItalic2xFont(&ibmplexmono_12_bolditalic_2x);
EpdFontFamily ibmplexmono12HiResFontFamily(&ibmplexmono12Regular2xFont, &ibmplexmono12Bold2xFont,
                                           &ibmplexmono12Italic2xFont, &ibmplexmono12BoldItalic2xFont);
EpdFont iawriterquattro12Regular2xFont(&iawriterquattro_12_regular_2x);
EpdFont iawriterquattro12Bold2xFont(&iawriterquattro_12_bold_2x);
EpdFont iawriterquattro12Italic2xFont(&iawriterquattro_12_italic_2x);
EpdFont iawriterquattro12BoldItalic2xFont(&iawriterquattro_12_bolditalic_2x);
EpdFontFamily iawriterquattro12HiResFontFamily(&iawriterquattro12Regular2xFont, &iawriterquattro12Bold2xFont,
                                               &iawriterquattro12Italic2xFont, &iawriterquattro12BoldItalic2xFont);
EpdFont iawriterduo12Regular2xFont(&iawriterduo_12_regular_2x);
EpdFont iawriterduo12Bold2xFont(&iawriterduo_12_bold_2x);
EpdFont iawriterduo12Italic2xFont(&iawriterduo_12_italic_2x);
EpdFont iawriterduo12BoldItalic2xFont(&iawriterduo_12_bolditalic_2x);
EpdFontFamily iawriterduo12HiResFontFamily(&iawriterduo12Regular2xFont, &iawriterduo12Bold2xFont,
                                           &iawriterduo12Italic2xFont, &iawriterduo12BoldItalic2xFont);
EpdFont iawritermono12Regular2xFont(&iawritermono_12_regular_2x);
EpdFont iawritermono12Bold2xFont(&iawritermono_12_bold_2x);
EpdFont iawritermono12Italic2xFont(&iawritermono_12_italic_2x);
EpdFont iawritermono12BoldItalic2xFont(&iawritermono_12_bolditalic_2x);
EpdFontFamily iawritermono12HiResFontFamily(&iawritermono12Regular2xFont, &iawritermono12Bold2xFont,
                                            &iawritermono12Italic2xFont, &iawritermono12BoldItalic2xFont);
#ifdef CROSSPOINT_HAS_PRAGMATAPRO
// Same __has_include gate as the 1x cuts above, restated through the macro so
// the two blocks cannot drift. Without this the editor would blit pixel-doubled
// 1x PragmataPro next to crisp 2x chrome on a RENDER_SCALE=2 build — the
// mixed-resolution bug drawTextRotated90CW shipped on 2026-08-04.
#include <builtinFonts/pragmatapro_12_bold_2x.h>
#include <builtinFonts/pragmatapro_12_bolditalic_2x.h>
#include <builtinFonts/pragmatapro_12_italic_2x.h>
#include <builtinFonts/pragmatapro_12_regular_2x.h>
EpdFont pragmatapro12Regular2xFont(&pragmatapro_12_regular_2x);
EpdFont pragmatapro12Bold2xFont(&pragmatapro_12_bold_2x);
EpdFont pragmatapro12Italic2xFont(&pragmatapro_12_italic_2x);
EpdFont pragmatapro12BoldItalic2xFont(&pragmatapro_12_bolditalic_2x);
EpdFontFamily pragmatapro12HiResFontFamily(&pragmatapro12Regular2xFont, &pragmatapro12Bold2xFont,
                                           &pragmatapro12Italic2xFont, &pragmatapro12BoldItalic2xFont);
#endif
#ifdef CROSSPOINT_HAS_NITTITYPEWRITER
// Same __has_include gate as the 1x cuts above, restated through the macro.
#include <builtinFonts/nittitypewriter_12_bold_2x.h>
#include <builtinFonts/nittitypewriter_12_bolditalic_2x.h>
#include <builtinFonts/nittitypewriter_12_italic_2x.h>
#include <builtinFonts/nittitypewriter_12_regular_2x.h>
EpdFont nittitypewriter12Regular2xFont(&nittitypewriter_12_regular_2x);
EpdFont nittitypewriter12Bold2xFont(&nittitypewriter_12_bold_2x);
EpdFont nittitypewriter12Italic2xFont(&nittitypewriter_12_italic_2x);
EpdFont nittitypewriter12BoldItalic2xFont(&nittitypewriter_12_bolditalic_2x);
EpdFontFamily nittitypewriter12HiResFontFamily(&nittitypewriter12Regular2xFont, &nittitypewriter12Bold2xFont,
                                               &nittitypewriter12Italic2xFont, &nittitypewriter12BoldItalic2xFont);
#endif
#endif

#ifndef OMIT_FONTS
EpdFont lfReader12RegularFont(&librefranklin_reader_12_regular);
EpdFont lfReader12BoldFont(&librefranklin_reader_12_bold);
EpdFont lfReader12ItalicFont(&librefranklin_reader_12_italic);
EpdFont lfReader12BoldItalicFont(&librefranklin_reader_12_bolditalic);
EpdFontFamily librefranklinReader12FontFamily(&lfReader12RegularFont, &lfReader12BoldFont, &lfReader12ItalicFont,
                                              &lfReader12BoldItalicFont);
EpdFont lfReader16RegularFont(&librefranklin_reader_16_regular);
EpdFont lfReader16BoldFont(&librefranklin_reader_16_bold);
EpdFont lfReader16ItalicFont(&librefranklin_reader_16_italic);
EpdFont lfReader16BoldItalicFont(&librefranklin_reader_16_bolditalic);
EpdFontFamily librefranklinReader16FontFamily(&lfReader16RegularFont, &lfReader16BoldFont, &lfReader16ItalicFont,
                                              &lfReader16BoldItalicFont);
EpdFont lfReader18RegularFont(&librefranklin_reader_18_regular);
EpdFont lfReader18BoldFont(&librefranklin_reader_18_bold);
EpdFont lfReader18ItalicFont(&librefranklin_reader_18_italic);
EpdFont lfReader18BoldItalicFont(&librefranklin_reader_18_bolditalic);
EpdFontFamily librefranklinReader18FontFamily(&lfReader18RegularFont, &lfReader18BoldFont, &lfReader18ItalicFont,
                                              &lfReader18BoldItalicFont);

#endif  // OMIT_FONTS

// --- System font family -----------------------------------------------------
//
// The UI draws at three sizes -- a 3x2 matrix of 8/10/12 pt, regular and bold.
// Libre Franklin is the only chrome family (owner ruling 2026-08-07); the
// Ubuntu, Noto Sans and Noto Serif columns this matrix used to carry were
// removed with the rest of those families' font data.
//
// SMALL (8 pt) has a bold face now where it previously had only regular. That
// is additive -- nothing asked for bold at 8 pt before, and EpdFontFamily falls
// back to regular for a style it lacks, so the old single-face behavior is
// still what an 8pt bold request gets on any family that has no bold cut.
#define CP_UI_FAMILY(sym, r8, b8, r10, b10, r12, b12) \
  EpdFont sym##8R(&r8), sym##8B(&b8);                 \
  EpdFontFamily sym##8(&sym##8R, &sym##8B);           \
  EpdFont sym##10R(&r10), sym##10B(&b10);             \
  EpdFontFamily sym##10(&sym##10R, &sym##10B);        \
  EpdFont sym##12R(&r12), sym##12B(&b12);             \
  EpdFontFamily sym##12(&sym##12R, &sym##12B)

CP_UI_FAMILY(sysLibreFranklin, librefranklin_8_regular, librefranklin_8_bold, librefranklin_10_regular,
             librefranklin_10_bold, librefranklin_12_regular, librefranklin_12_bold);

#if defined(CROSSPOINT_RENDER_SCALE) && CROSSPOINT_RENDER_SCALE > 1
// The same matrix at double the ppem, for glyph blitting only. Named for the 1x
// face each stands in for; their own point sizes are 16/20/24.
CP_UI_FAMILY(sysLibreFranklin2x, librefranklin_8_regular_2x, librefranklin_8_bold_2x, librefranklin_10_regular_2x,
             librefranklin_10_bold_2x, librefranklin_12_regular_2x, librefranklin_12_bold_2x);
#endif
#undef CP_UI_FAMILY

// Bind every UI slot to Libre Franklin. Called once at boot. SETTINGS.systemFont
// is deliberately not consulted: the SYSTEM_FONT values naming other families
// are retired -- their font data no longer exists -- and
// normalizeRetiredSettings() pins the stored byte back to SYSTEM_FONT_LIBREFRANKLIN
// on every load anyway.
void applySystemFont(GfxRenderer& renderer) {
  // replaceFont, not insertFont: setupDisplayAndFonts can run more than once
  // (simulator wake path), and insertFont refuses to overwrite an id it holds.
  renderer.replaceFont(SMALL_FONT_ID, sysLibreFranklin8);
  renderer.replaceFont(UI_10_FONT_ID, sysLibreFranklin10);
  renderer.replaceFont(UI_12_FONT_ID, sysLibreFranklin12);

#if defined(CROSSPOINT_RENDER_SCALE) && CROSSPOINT_RENDER_SCALE > 1
  renderer.registerHiResBuiltinFont(SMALL_FONT_ID, sysLibreFranklin2x8);
  renderer.registerHiResBuiltinFont(UI_10_FONT_ID, sysLibreFranklin2x10);
  renderer.registerHiResBuiltinFont(UI_12_FONT_ID, sysLibreFranklin2x12);
#endif
}

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,       // cold boot, flash, panic, or plain reboot
  Silent,       // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  QuickResume,  // wake from a quick-resume deep sleep (SD flag; survives power loss)
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. On hardware startDeepSleep()
// does not return, so the latch ends at the wakeup reset; setup() clears it
// explicitly for the simulator's in-process wake, where it would otherwise stay
// latched and silently disable silentRestart() for the rest of the session.
static bool deepSleepInProgress = false;

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

// Enter deep sleep mode
void enterDeepSleep(bool fromTimeout = false) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  // Must be the SAME decision SleepActivity::onEnter makes, hence the shared
  // policy: this one picks whether to save the frame buffer and skip the boot
  // screen, and if the two ever disagreed the device would either restore a
  // frame nothing drew or drop the one it did.
  const bool isQuickResumeSleep =
      sleepscreen::shouldQuickResume(SETTINGS.sleepScreen, SETTINGS.quickResumeSleepScreen, fromTimeout);
  APP_STATE.showBootScreen = !isQuickResumeSleep;

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts(bool seamless = false) {
  display.begin(seamless);
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, librefranklinReader14FontFamily);
  // Registered unguarded, matching where the families are now defined. The
  // Editor Font picker's availability mark is literally
  // renderer.getFontMap().count(id), so a face defined but never inserted here
  // still reports "Not on card" — the definition and the insert have to leave
  // OMIT_FONTS together or the move buys nothing.
  renderer.insertFont(IBMPLEXMONO_12_FONT_ID, ibmplexmono12FontFamily);
  renderer.insertFont(IAWRITERQUATTRO_12_FONT_ID, iawriterquattro12FontFamily);
  renderer.insertFont(IAWRITERDUO_12_FONT_ID, iawriterduo12FontFamily);
  renderer.insertFont(IAWRITERMONO_12_FONT_ID, iawritermono12FontFamily);
#ifdef CROSSPOINT_HAS_PRAGMATAPRO
  // Only when the local commercial cuts were built. When they were not, this
  // insert is absent, getFontMap().count() is 0, and the picker marks the row
  // unreachable rather than offering a face that would draw nothing.
  renderer.insertFont(PRAGMATAPRO_12_FONT_ID, pragmatapro12FontFamily);
#endif
#ifdef CROSSPOINT_HAS_NITTITYPEWRITER
  renderer.insertFont(NITTITYPEWRITER_12_FONT_ID, nittitypewriter12FontFamily);
#endif
#if defined(CROSSPOINT_RENDER_SCALE) && CROSSPOINT_RENDER_SCALE > 1
  // Hi-res companions for all five editor fonts. drawText() checks
  // getHiResFamily() for any font id, so without these the editor activities
  // (NoteEditorActivity, ClaudeChatActivity) render at 1x on a 2x build.
  renderer.registerHiResBuiltinFont(IBMPLEXMONO_12_FONT_ID, ibmplexmono12HiResFontFamily);
  renderer.registerHiResBuiltinFont(IAWRITERQUATTRO_12_FONT_ID, iawriterquattro12HiResFontFamily);
  renderer.registerHiResBuiltinFont(IAWRITERDUO_12_FONT_ID, iawriterduo12HiResFontFamily);
  renderer.registerHiResBuiltinFont(IAWRITERMONO_12_FONT_ID, iawritermono12HiResFontFamily);
#ifdef CROSSPOINT_HAS_PRAGMATAPRO
  renderer.registerHiResBuiltinFont(PRAGMATAPRO_12_FONT_ID, pragmatapro12HiResFontFamily);
#endif
#ifdef CROSSPOINT_HAS_NITTITYPEWRITER
  renderer.registerHiResBuiltinFont(NITTITYPEWRITER_12_FONT_ID, nittitypewriter12HiResFontFamily);
#endif
#endif
#ifndef OMIT_FONTS
  renderer.insertFont(LIBREFRANKLIN_READER_12_FONT_ID, librefranklinReader12FontFamily);
  renderer.insertFont(LIBREFRANKLIN_READER_16_FONT_ID, librefranklinReader16FontFamily);
  renderer.insertFont(LIBREFRANKLIN_READER_18_FONT_ID, librefranklinReader18FontFamily);
#endif  // OMIT_FONTS
  // Fills SMALL / UI_10 / UI_12 from the chosen system font, and on a 2x build
  // registers the matching hi-res companions alongside them.
  applySystemFont(renderer);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

#ifndef SIMULATOR
// Pin the Arduino loopTask stack explicitly.
//
// Without this the size comes from whichever WEAK getArduinoLoopTaskStackSize()
// the linker happens to pick: the Arduino core's (CONFIG_ARDUINO_LOOP_STACK_SIZE,
// 8192) or FreeInkUI's (16384). Which one lands depends on whether that archive
// member gets extracted at all — i.e. on link order, not on intent. This macro
// emits a STRONG definition that beats both, so the budget is deterministic.
//
// 16K because the deepest measured loopTask frames are large and can nest:
// getSettingsList's builder, CrossPointWebServer::handleDownload and
// WebDAVHandler::handleCopy are all multi-KB (each is being reduced separately,
// but headroom here is what turns a stack-canary panic into a non-event).
// Costs 8KB of DRAM against ~200KB free at link time.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

void setup() {
  BoardConfig::holdPowerRails();

  t1 = millis();

#ifdef ENABLE_SERIAL_LOG
  // Earliest possible Serial setup. The 250 ms stall before begin() lets the
  // USB Serial/JTAG peripheral finish power-on and lets the host complete USB
  // enumeration before we touch the CDC state — otherwise cold boot races
  // and the host has to be physically replugged for logs to flow. Warm reboot
  // worked without the delay because USB was already enumerated.
  delay(250);
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  // checkPanic() clears the watchdog capture marker after a successful SD
  // dump, so retain the boot classification for the later activity route.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  powerManager.begin();
  halClock.begin();

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();

  SETTINGS.loadFromFile();
  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button press duration");
      if (!gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                        SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP)) {
        powerManager.startDeepSleep(gpio);
      }
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // Recovery firmware mode: hold left side button (BTN_UP) together with the power button at
  // boot to skip directly to the SD-card firmware update screen. Useful on devices where USB
  // flashing has been locked down (e.g. recent X3 firmware).
  bool recoveryFirmwareMode = false;
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  const BootResume resume = isSilentReboot              ? BootResume::Silent
                            : !APP_STATE.showBootScreen ? BootResume::QuickResume
                                                        : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;

  setupDisplayAndFonts(resume != BootResume::Splash);

  // Restore the stored polarity BEFORE the first paint of any kind. The
  // framebuffer is always logical (1 = white) whichever mode is active, so the
  // retained quick-resume frame carries no polarity of its own — it renders in
  // whatever mode is live when it is re-displayed. Setting the flag here is what
  // makes a dark-mode reader wake straight back into a dark page instead of
  // flashing a full-brightness white one and correcting on the next refresh.
  //
  // Cost: setInverted() marks the inversion dirty, which promotes the first
  // refresh from FAST to HALF (~1.7s, FreeInkDisplay.cpp:573-575). Only on the
  // boot that turns it on — a light-mode device sets false over false, which the
  // SDK drops as a no-op.
  display.setInverted(SETTINGS.darkMode != 0);

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::QuickResume:
      // One-shot flag: re-arm the splash for the next non-quick-resume boot. Save
      // before any painting so a hang in the blocking paint path can't strand
      // us in a quick-resume-with-no-frame loop on the next boot.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      if (loadSleepFrameBuffer()) {
        const bool useDifferentialRefresh = gpio.deviceIsX3();
        if (useDifferentialRefresh) {
          // begin() clears the X3 controller RAM, so restore the saved frame as
          // the baseline before replacing the moon with the loading icon.
          renderer.cleanupGrayscaleWithFrameBuffer();
        }

        const auto pageHeight = renderer.getScreenHeight();
        renderer.drawImage(LoadingIcon, 0, pageHeight - LOADINGICON_HEIGHT, LOADINGICON_WIDTH, LOADINGICON_HEIGHT);
        if (useDifferentialRefresh) {
          renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
          allowFastInitialReaderRefresh = true;
        } else {
          renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        }
      } else {
        activityManager.goToBoot();  // frame file missing, fall back to the splash
      }
      break;
    case BootResume::Splash:
      activityManager.goToBoot();
      break;
  }

  if (recoveryFirmwareMode) {
#ifndef CROSSPOINT_NO_DEVICE_FLASH
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
#else
    // Recovery firmware mode is not available on this platform; fall through to normal boot.
    activityManager.goHome();
#endif
  } else if (rebootedFromPanic) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // The device IS the current book: boot into the last-open one whenever it is
    // known, else the first EPUB on the card. Home exists only as the escape
    // hatch for the two cases that need it — crash recovery
    // (readerActivityLoadCount) and holding BACK during boot — so a bad book
    // cannot brick the device.
    if (!mappedInputManager.isPressed(MappedInputManager::Button::Back) && APP_STATE.readerActivityLoadCount == 0) {
      std::string zenPath = APP_STATE.openEpubPath;
      if (zenPath.empty()) {
        HalFile books = Storage.open("/books");
        while (books) {
          HalFile entry = books.openNextFile();
          if (!entry) break;
          char nameBuf[192];
          entry.getName(nameBuf, sizeof(nameBuf));
          const bool isDir = entry.isDirectory();
          entry.close();
          if (isDir) continue;
          const size_t len = strlen(nameBuf);
          if (len > 5 && strcasecmp(nameBuf + len - 5, ".epub") == 0) {
            zenPath = std::string("/books/") + nameBuf;
            break;
          }
        }
      }
      if (!zenPath.empty()) {
        APP_STATE.readerActivityLoadCount++;
        APP_STATE.saveToFile();
        activityManager.goToReader(zenPath, allowFastInitialReaderRefresh);
      } else {
        activityManager.goHome();
      }
    } else {
      activityManager.goHome();
    }
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path, allowFastInitialReaderRefresh);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
  allowSleepAt = millis() + 2000;
  // Both of these are what a chip reset would give us for free on hardware, and
  // what the simulator's in-process (longjmp) wake does not.
  lastActivityTime = millis();
  deepSleepInProgress = false;

  // A panic's serial output is lost when USB CDC drops with the crash, so
  // replay the persisted record on the next boot instead. This is how the BLE
  // watchdog reboots were diagnosed.
  if (HalSystem::isRebootFromPanic()) {
    LOG_INF("MAIN", "Previous boot panicked: %s", HalSystem::getPanicInfo(true).c_str());
  }

  LOG_INF("MEM", "Boot complete: free=%u total=%u maxalloc=%u", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getHeapSize(), (unsigned)ESP.getMaxAllocHeap());
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  gpio.update();

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (millis() >= allowSleepAt && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
