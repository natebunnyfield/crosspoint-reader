#include "HostHarness.h"

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <builtinFonts/all.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

// ============================================================================
// Globals the firmware declares extern and expects main.cpp to define.
// ============================================================================

HalDisplay display;
HalGPIO gpio;

namespace {

// One renderer for the whole process, built exactly like test/renderer_bounds'
// Gfx and tools/calendar_preview: begin() allocates the framebuffer chunks and
// the builtin fonts are compressed, so the decompressor must be wired before
// any drawText. Function-local static, so construction order relative to the
// other globals in this TU is defined by first use rather than luck.
class Gfx {
 public:
  static Gfx& instance() {
    static Gfx g;
    return g;
  }

  GfxRenderer& renderer() { return renderer_; }

 private:
  Gfx() : renderer_(display), cache_(renderer_.getFontMap(), renderer_.getSdCardFonts()) {
    renderer_.begin();
    if (!decompressor_.init()) {
      std::fprintf(stderr, "[harness] font decompressor init failed\n");
      std::abort();
    }
    cache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&cache_);
    renderer_.insertFont(LIBREFRANKLIN_READER_12_FONT_ID, lfReader12_);
    renderer_.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lfReader14_);
    renderer_.insertFont(UI_10_FONT_ID, ui10_);
    renderer_.insertFont(UI_12_FONT_ID, ui12_);
    renderer_.insertFont(SMALL_FONT_ID, small_);
    // Orientation is a compile-time constant now (3b30b531); nothing to set.
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;

  EpdFont lfr12R_{&librefranklin_reader_12_regular}, lfr12B_{&librefranklin_reader_12_bold},
      lfr12I_{&librefranklin_reader_12_italic}, lfr12BI_{&librefranklin_reader_12_bolditalic};
  EpdFontFamily lfReader12_{&lfr12R_, &lfr12B_, &lfr12I_, &lfr12BI_};
  EpdFont lfr14R_{&librefranklin_reader_14_regular}, lfr14B_{&librefranklin_reader_14_bold},
      lfr14I_{&librefranklin_reader_14_italic}, lfr14BI_{&librefranklin_reader_14_bolditalic};
  EpdFontFamily lfReader14_{&lfr14R_, &lfr14B_, &lfr14I_, &lfr14BI_};
  EpdFont ui10R_{&librefranklin_10_regular}, ui10B_{&librefranklin_10_bold};
  EpdFontFamily ui10_{&ui10R_, &ui10B_};
  EpdFont ui12R_{&librefranklin_12_regular}, ui12B_{&librefranklin_12_bold};
  EpdFontFamily ui12_{&ui12R_, &ui12B_};
  EpdFont small8_{&librefranklin_8_regular};
  EpdFontFamily small_{&small8_};
};

MappedInputManager& mappedInputSingleton() {
  static MappedInputManager m(gpio, Gfx::instance().renderer());
  return m;
}

// ---------------------------------------------------------------------------
// ActivityManager double state.
//
// The real ActivityManager's stack lives in protected members, and nothing
// outside the class can reach them — so the double keeps its own copy of that
// state here instead. Every ActivityManager method defined below is a real
// member of the real class, so it is a friend of Activity and can read the
// protected `name`, `result` and `resultHandler` the navigation contract needs.
// ---------------------------------------------------------------------------

enum class HostPending { None, Push, Pop, Replace, ReplaceCurrentOnly };

struct AmState {
  std::unique_ptr<Activity> current;
  std::vector<std::unique_ptr<Activity>> stack;
  std::unique_ptr<Activity> pending;
  HostPending pendingAction = HostPending::None;
  std::string currentName;
  host::Counters counters;
  host::FontSystemCalls fontCalls;
};

AmState& am() {
  static AmState s;
  return s;
}

// Stand-in for whatever real activity a goTo*/goHome lands on. Neither is
// linkable here (nm -u on the simulator objects: 40 project-level undefined
// symbols for HomeActivity, 124 for EpubReaderActivity), and the tests only
// need to observe THAT the user was taken there, which the counters record.
class HostPlaceholderActivity final : public Activity {
 public:
  HostPlaceholderActivity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(std::move(name), renderer, mappedInput) {}
};

}  // namespace

// ============================================================================
// CrossPointSettings — the real class, a host-side definition of its singleton.
//
// Every field default lives in CrossPointSettings.h, so this gives the genuine
// shipped defaults (frontButtonBack = FRONT_HW_BACK, sideButtonLayout =
// PREV_NEXT, fontSizeSlot = DEFAULT_FONT_SIZE_SLOT, ...) which is exactly what MappedInputManager's
// logical-to-physical mapping is driven by. Only the two methods the linked TUs
// reference are defined; the real CrossPointSettings.cpp is not linked because
// it pulls in JsonSettingsIO/ArduinoJson and the settings file I/O.
// ============================================================================

// (The singleton itself lives in PersistableStore<T>::getInstance()'s
// function-local static; no out-of-line member definition is needed.)

int CrossPointSettings::getReaderFontId() const {
  // Reachable only from FontSelectionActivity::render(). 0 means "no font",
  // which renderPreviewPane already treats as "draw nothing" — the right answer
  // here, since the real one resolves against an SD registry this suite does not
  // have.
  return 0;
}

// ============================================================================
// UITheme is the REAL one now.
//
// It used to be a double with a null `currentTheme`, which made GUI (==
// getTheme()) a null dereference and put render() out of bounds for this
// suite. That was tolerable only until an activity read the theme OUTSIDE
// render: cb98d4fa added GUI.getListRowStep() to FontSelectionActivity::onEnter
// (FontSelectionActivity.cpp:105), and every FontSelection case here started
// segfaulting in onEnter.
//
// src/components/UITheme.cpp and the BaseTheme -> LyraTheme -> Lyra3CoversTheme
// -> LyraSixTheme chain are linked by crosspoint_add_theme_stack() (see
// test/cmake/HostThemeStack.cmake). The only thing that had ever blocked it was
// BaseTheme.cpp's <HalClock.h> / <HalPowerManager.h> include chain reaching
// ESP-IDF; test/host_stubs/ shadows those two headers.
//
// So GUI is a live LyraSixTheme here, and render() is no longer forbidden — the
// harness still never starts a render task, but a test may now call
// activity.render() directly if it wants pixel coverage.
// ============================================================================

// ============================================================================
// SdCardFontSystem double.
//
// ensureLoaded() is the dangerous call in FontSelectionActivity: on the device
// it runs unloadAll() -> delete lf.font, freeing glyph tables the render task
// may be mid-read of, which is why both call sites hold a RenderLock
// (FontSelectionActivity.cpp:200-220, 229-234). The double records whether the
// lock was actually held so a future edit that drops it fails a test instead of
// panicking on a device. begin()/resolveFontId() are not referenced by anything
// linked here, so they are left undefined.
// ============================================================================

SdCardFontSystem sdFontSystem;

void SdCardFontSystem::ensureLoaded(GfxRenderer&) {
  auto& s = am();
  s.fontCalls.ensureLoaded++;
  if (!RenderLock::peek()) s.fontCalls.ensureLoadedWithoutRenderLock++;
}

// Referenced by LyraSixTheme::drawRecentBookCover (LyraSixTheme.cpp:135) for
// the Home title face. Nothing host-side installs SD fonts, and 0 is what the
// real one returns for a family that is not present — the theme then falls back
// to the built-in title font.
int SdCardFontSystem::loadForDisplay(const char*, uint8_t, GfxRenderer&) { return 0; }

// ============================================================================
// ActivityManager double.
//
// loop(), replaceActivity(), pushActivity() and popActivity() mirror
// src/activities/ActivityManager.cpp exactly, including the property the whole
// suite depends on: a finish() during loop() is DEFERRED and applied only after
// the current activity's loop() returns. The goTo* family is replaced by
// counters (see HostPlaceholderActivity) and the render task does not exist.
// ============================================================================

ActivityManager activityManager(Gfx::instance().renderer(), mappedInputSingleton());

void ActivityManager::begin() {
  // No render task host-side: nothing notifies it and nothing renders.
}

void ActivityManager::renderTaskLoop() {
  // Defined because it is the vtable's key function (first non-inline virtual),
  // so constructing an ActivityManager needs it. Reaching it would mean a test
  // started a FreeRTOS render task, which the host shim has no scheduler for —
  // a test that wants pixels calls Activity::render() directly instead (see
  // RenderDrawsAFullFrameThroughTheRealTheme).
  std::fprintf(stderr, "[harness] renderTaskLoop() must never run host-side\n");
  std::abort();
}

void ActivityManager::exitActivity(const RenderLock&) {
  auto& s = am();
  if (s.current) {
    s.current->onExit();
    s.current.reset();
    s.currentName.clear();
  }
}

void ActivityManager::loop() {
  auto& s = am();
  // No lock here, matching ActivityManager.cpp:64 — loop() is responsible for
  // acquiring one if it needs it, which is why changeFontSize() must.
  if (s.current) {
    s.current->loop();
  }

  while (s.pendingAction != HostPending::None) {
    if (s.pendingAction == HostPending::Pop) {
      RenderLock lock;

      if (!s.current) {
        s.pendingAction = HostPending::None;
        continue;
      }

      ActivityResult pendingResult = std::move(s.current->result);
      exitActivity(lock);
      s.pendingAction = HostPending::None;
      s.counters.pops++;
      s.counters.transitions++;

      if (s.stack.empty()) {
        lock.unlock();  // goHome acquires its own
        goHome();
        continue;
      }

      s.current = std::move(s.stack.back());
      s.stack.pop_back();
      s.currentName = s.current->name;
      // Mirror ActivityManager.cpp: swallow stale edges for the restored parent.
      mappedInput.swallowUntilIdle();

      if (s.current->resultHandler) {
        auto handler = std::move(s.current->resultHandler);
        s.current->resultHandler = nullptr;
        lock.unlock();  // handler may acquire its own
        handler(pendingResult);
      }

      if (s.pendingAction == HostPending::None) {
        requestUpdate();
      }
      continue;
    }

    if (s.pending) {
      RenderLock lock;

      if (s.pendingAction == HostPending::Replace) {
        exitActivity(lock);
        while (!s.stack.empty()) {
          s.stack.back()->onExit();
          s.stack.pop_back();
        }
        s.counters.replaces++;
      } else if (s.pendingAction == HostPending::ReplaceCurrentOnly) {
        // Destroy current without clearing the stack (mirrors ActivityManager.cpp).
        exitActivity(lock);
        s.counters.replaces++;
      } else if (s.pendingAction == HostPending::Push) {
        s.stack.push_back(std::move(s.current));
        s.counters.pushes++;
      }
      s.counters.transitions++;
      s.pendingAction = HostPending::None;
      s.current = std::move(s.pending);
      s.currentName = s.current->name;
      // Mirror ActivityManager.cpp: swallow stale edges for the new activity.
      mappedInput.swallowUntilIdle();

      lock.unlock();  // onEnter may acquire its own
      s.current->onEnter();
      continue;
    }

    // A pending action with nothing to launch. The real loop() would spin here
    // forever; bail so a harness bug surfaces as a failed assertion instead of
    // a hung test binary.
    s.pendingAction = HostPending::None;
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  auto& s = am();
  if (s.current) {
    s.pending = std::move(newActivity);
    s.pendingAction = HostPending::Replace;
  } else {
    s.current = std::move(newActivity);
    s.currentName = s.current->name;
    s.counters.transitions++;
    s.current->onEnter();
  }
}

void ActivityManager::replaceCurrentActivity(std::unique_ptr<Activity>&& newActivity) {
  auto& s = am();
  s.pending = std::move(newActivity);
  s.pendingAction = HostPending::ReplaceCurrentOnly;
}

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  auto& s = am();
  s.pending = std::move(activity);
  s.pendingAction = HostPending::Push;
}

void ActivityManager::popActivity() { am().pendingAction = HostPending::Pop; }

void ActivityManager::goHome(HomeMenuItem) {
  am().counters.goHome++;
  replaceActivity(std::make_unique<HostPlaceholderActivity>("TestHome", renderer, mappedInput));
}

void ActivityManager::goToReader(std::string, bool) {
  am().counters.goToReader++;
  replaceActivity(std::make_unique<HostPlaceholderActivity>("TestReader", renderer, mappedInput));
}

void ActivityManager::requestUpdate(bool) { am().counters.updates++; }

// The real one asserts on three conditions (ActivityManager.cpp:319-326). Only
// the third — "cannot call while holding RenderLock" — is reachable from an
// activity's own task, and it fired on device: ClaudeChatActivity's OK key ran
// send() under the lock it had taken to type. Recorded rather than aborted, so
// a test can name the defect instead of dying inside the harness.
void ActivityManager::requestUpdateAndWait() {
  auto& s = am();
  s.counters.updateAndWaits++;
  if (RenderLock::peek()) s.counters.updateAndWaitsHoldingRenderLock++;
}

// ============================================================================
// RenderLock — mirrors ActivityManager.cpp:311-342 over the host semaphore
// shim, so peek() reports the real held/not-held state.
// ============================================================================

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock(Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, nullptr, 0) != pdTRUE; }

// ============================================================================
// host:: API
// ============================================================================

namespace host {

GfxRenderer& renderer() { return Gfx::instance().renderer(); }

MappedInputManager& input() { return mappedInputSingleton(); }

HalGPIO& buttons() { return gpio; }

void frame() {
  gpio.update();           // src/main.cpp:505
  activityManager.loop();  // src/main.cpp:599
}

void frames(int count) {
  for (int i = 0; i < count; i++) frame();
}

void pressFrame(uint8_t hardwareButton) {
  gpio.simSetDown(hardwareButton, true);
  frame();
}

void releaseFrame(uint8_t hardwareButton) {
  gpio.simSetDown(hardwareButton, false);
  frame();
}

void tap(uint8_t hardwareButton) {
  pressFrame(hardwareButton);
  releaseFrame(hardwareButton);
}

void setRootActivity(std::unique_ptr<Activity>&& activity) { activityManager.replaceActivity(std::move(activity)); }

Activity* currentActivity() { return am().current.get(); }

const std::string& currentActivityName() { return am().currentName; }

const Counters& counters() { return am().counters; }

// Clears fontCalls too: FontSelectionActivity::onEnter() preloads the SD font
// (ensureLoaded) since the uniform-slots preview work, so tests that assert on
// per-action call counts must be able to zero the ledger after activity entry.
void resetCounters() {
  am().counters = Counters{};
  am().fontCalls = FontSystemCalls{};
}

const FontSystemCalls& fontSystemCalls() { return am().fontCalls; }

void reset() {
  auto& s = am();

  // Destroy without onExit(): this is a between-tests teardown, not navigation.
  s.pending.reset();
  s.pendingAction = HostPending::None;
  s.current.reset();
  s.stack.clear();
  s.currentName.clear();
  s.counters = Counters{};
  s.fontCalls = FontSystemCalls{};

  gpio.simReset();
  mappedInputSingleton().resetSwallow();

  // Restore the settings fields these tests write, so ordering between tests
  // cannot matter.
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.sideButtonLayout = CrossPointSettings::PREV_NEXT;
  SETTINGS.fontSizeSlot = CrossPointSettings::DEFAULT_FONT_SIZE_SLOT;
  SETTINGS.fontFamily = CrossPointSettings::BUILTIN_LIBRE_FRANKLIN;
  SETTINGS.sdFontFamilyName[0] = '\0';

  // Orientation is a compile-time constant now (GfxRenderer::orientation =
  // Portrait); there is no setter.

  // ButtonNavigator holds the input manager in a static, set once in
  // src/main.cpp; without it every onRelease/onContinuous silently no-ops.
  ButtonNavigator::setMappedInputManager(input());
}

}  // namespace host
