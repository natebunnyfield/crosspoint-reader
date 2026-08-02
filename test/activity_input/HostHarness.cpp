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
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"
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
    renderer_.insertFont(NOTOSERIF_12_FONT_ID, notoserif12_);
    renderer_.insertFont(NOTOSERIF_14_FONT_ID, notoserif14_);
    renderer_.insertFont(UI_10_FONT_ID, ui10_);
    renderer_.insertFont(UI_12_FONT_ID, ui12_);
    renderer_.insertFont(SMALL_FONT_ID, small_);
    // Orientation is a compile-time constant now (3b30b531); nothing to set.
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;

  EpdFont ns12R_{&notoserif_12_regular}, ns12B_{&notoserif_12_bold}, ns12I_{&notoserif_12_italic},
      ns12BI_{&notoserif_12_bolditalic};
  EpdFontFamily notoserif12_{&ns12R_, &ns12B_, &ns12I_, &ns12BI_};
  EpdFont ns14R_{&notoserif_14_regular}, ns14B_{&notoserif_14_bold}, ns14I_{&notoserif_14_italic},
      ns14BI_{&notoserif_14_bolditalic};
  EpdFontFamily notoserif14_{&ns14R_, &ns14B_, &ns14I_, &ns14BI_};
  EpdFont ui10R_{&ubuntu_10_regular}, ui10B_{&ubuntu_10_bold};
  EpdFontFamily ui10_{&ui10R_, &ui10B_};
  EpdFont ui12R_{&ubuntu_12_regular}, ui12B_{&ubuntu_12_bold};
  EpdFontFamily ui12_{&ui12R_, &ui12B_};
  EpdFont small8_{&notosans_8_regular};
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

enum class HostPending { None, Push, Pop, Replace };

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
// PREV_NEXT, fontSize = MEDIUM, ...) which is exactly what MappedInputManager's
// logical-to-physical mapping is driven by. Only the two methods the linked TUs
// reference are defined; the real CrossPointSettings.cpp is not linked because
// it pulls in JsonSettingsIO/ArduinoJson and the settings file I/O.
// ============================================================================

CrossPointSettings CrossPointSettings::instance;

int CrossPointSettings::getReaderFontId() const {
  // Only reachable from FontSelectionActivity::render(), which this suite never
  // drives (see the UITheme double below). 0 means "no font", which
  // renderPreviewPane already treats as "draw nothing".
  return 0;
}

// ============================================================================
// UITheme double.
//
// The real UITheme.cpp cannot be linked: its constructor make_unique's one of
// the four concrete themes, which drags in BaseTheme + Lyra + Lyra3Covers +
// RoundedRaff and, behind them, HalClock, HalPowerManager, RecentBooksStore and
// the icon tables. So the metrics — the only part activity *logic* reads — are
// wired to the genuine BaseMetrics::values from BaseTheme.h, and `currentTheme`
// is left null.
//
// CONSEQUENCE, and the one real limit of this suite: GUI (== getTheme()) is a
// null dereference, so Activity::render() MUST NOT be called. Nothing calls it
// — the harness never starts the render task and never notifies one. A test
// that wants render() coverage belongs in test/renderer_bounds, which links the
// real theme stack's collaborators.
// ============================================================================

UITheme UITheme::instance;

UITheme::UITheme() : currentMetrics(&BaseMetrics::values) {}

// Copied from src/components/UITheme.cpp:52-70. Self-contained — metrics plus
// the renderer — so this is the real computation, not an approximation.
// FontSelectionActivity::loop() calls it every frame to size a page jump.
int UITheme::getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  int reservedHeight = metrics.topPadding;
  if (hasHeader) {
    reservedHeight += metrics.headerHeight + metrics.verticalSpacing;
  }
  if (hasTabBar) {
    reservedHeight += metrics.tabBarHeight;
  }
  if (hasButtonHints && orientation != GfxRenderer::Orientation::LandscapeClockwise &&
      orientation != GfxRenderer::Orientation::LandscapeCounterClockwise) {
    reservedHeight += metrics.verticalSpacing + metrics.buttonHintsHeight;
  }
  const int availableHeight = renderer.getScreenHeight() - reservedHeight - extraReservedHeight;
  const int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  return availableHeight / rowHeight;
}

// Render-path only (EpubReaderMenuActivity::render, EpubReaderPercentSelection
// Activity::render). Defined for the linker; the full-screen rect is not used
// for any assertion.
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool, bool) {
  return Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()};
}

void UITheme::drawCenteredText(const GfxRenderer&, Rect, int, int, const char*, bool, EpdFontFamily::Style) {
  // Render-path only; see getScreenSafeArea.
}

// ============================================================================
// SdCardFontSystem double.
//
// ensureLoaded() is the dangerous call in FontSelectionActivity: on the device
// it runs unloadAll() -> delete lf.font, freeing glyph tables the render task
// may be mid-read of, which is why both call sites hold a RenderLock
// (FontSelectionActivity.cpp:200-220, 229-234). The double records whether the
// lock was actually held so a future edit that drops it fails a test instead of
// panicking on a device. begin()/resolveFontId()/loadForDisplay() are not
// referenced by anything linked here, so they are left undefined.
// ============================================================================

SdCardFontSystem sdFontSystem;

void SdCardFontSystem::ensureLoaded(GfxRenderer&) {
  auto& s = am();
  s.fontCalls.ensureLoaded++;
  if (!RenderLock::peek()) s.fontCalls.ensureLoadedWithoutRenderLock++;
}

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
  // started a render task, which the UITheme double cannot survive.
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
      } else if (s.pendingAction == HostPending::Push) {
        s.stack.push_back(std::move(s.current));
        s.counters.pushes++;
      }
      s.counters.transitions++;
      s.pendingAction = HostPending::None;
      s.current = std::move(s.pending);
      s.currentName = s.current->name;

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

void ActivityManager::goToReader(std::string) {
  am().counters.goToReader++;
  replaceActivity(std::make_unique<HostPlaceholderActivity>("TestReader", renderer, mappedInput));
}

void ActivityManager::requestUpdate(bool) { am().counters.updates++; }

void ActivityManager::requestUpdateAndWait() { am().counters.updateAndWaits++; }

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

void resetCounters() { am().counters = Counters{}; }

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

  // Restore the settings fields these tests write, so ordering between tests
  // cannot matter.
  SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
  SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
  SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
  SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
  SETTINGS.frontButtonFollowOrientation = 0;
  SETTINGS.sideButtonLayout = CrossPointSettings::PREV_NEXT;
  SETTINGS.fontSize = CrossPointSettings::MEDIUM;
  SETTINGS.fontFamily = CrossPointSettings::NOTOSERIF;
  SETTINGS.sdFontFamilyName[0] = '\0';

  renderer().setOrientation(GfxRenderer::Portrait);

  // ButtonNavigator holds the input manager in a static, set once in
  // src/main.cpp; without it every onRelease/onContinuous silently no-ops.
  ButtonNavigator::setMappedInputManager(input());
}

}  // namespace host
