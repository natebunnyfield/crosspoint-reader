#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// One row of the File Transfer menu. Label, description, icon and resulting NetworkMode travel together so the
// display order can be changed without re-deriving the action from the row index.
struct MenuEntry {
  StrId label;
  StrId desc;
  UIIcon icon;
  NetworkMode mode;
};

// Display order (top to bottom). Calibre Wireless is intentionally last.
constexpr MenuEntry MENU_ENTRIES[] = {
    {StrId::STR_JOIN_NETWORK, StrId::STR_JOIN_DESC, UIIcon::Wifi, NetworkMode::JOIN_NETWORK},
    {StrId::STR_CREATE_HOTSPOT, StrId::STR_HOTSPOT_DESC, UIIcon::Hotspot, NetworkMode::CREATE_HOTSPOT},
    {StrId::STR_CALIBRE_WIRELESS, StrId::STR_CALIBRE_DESC, UIIcon::Library, NetworkMode::CONNECT_CALIBRE},
};
constexpr int MENU_ITEM_COUNT = static_cast<int>(sizeof(MENU_ENTRIES) / sizeof(MENU_ENTRIES[0]));
}  // namespace

void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();

  // Reset selection
  selectedIndex = 0;

  // Trigger first update
  requestUpdate();
}

void NetworkModeSelectionActivity::onExit() { Activity::onExit(); }

void NetworkModeSelectionActivity::loop() {
  // Resolve the mode through MENU_ENTRIES so selection always matches the
  // rendered order — the display order lives in that one table.
  auto selectCurrent = [this] {
    if (selectedIndex < 0 || selectedIndex >= MENU_ITEM_COUNT) return;
    onModeSelected(MENU_ENTRIES[selectedIndex].mode);
  };

  // Handle back button - cancel
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
    return;
  }

  // Handle confirm button - select current option
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    selectCurrent();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  switch (handleListTouch(selectedIndex, MENU_ITEM_COUNT, contentTop, contentHeight, true)) {
    case ListTouchResult::Activated:
      selectCurrent();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEM_COUNT);
    requestUpdate();
  });
}

void NetworkModeSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FILE_TRANSFER));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  // Menu items and descriptions come from MENU_ENTRIES so labels stay paired with their NetworkMode.
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEM_COUNT), selectedIndex,
      [](int index) { return std::string(I18N.get(MENU_ENTRIES[index].label)); },
      [](int index) { return std::string(I18N.get(MENU_ENTRIES[index].desc)); },
      [](int index) { return MENU_ENTRIES[index].icon; });

  // Draw help text at bottom
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
