#include "UiListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

UiListActivity::UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const bool wantsTouchLongPress)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer), wantsTouchLongPress(wantsTouchLongPress) {}

void UiListActivity::onEnter() {
  Activity::onEnter();
  activeNav().reset();
  resetUi();
  app.on(ACTION_ROW, &UiListActivity::rowActionTrampoline, this);
  app.setScreen(&UiListActivity::screenTrampoline, this);
  requestUpdate();
}

void UiListActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<UiListActivity*>(user)->buildScreen(screen);
}

void UiListActivity::rowActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  if (event.value < 0 || event.value >= self->listCount()) return;
  self->onRowAction(event);
}

void UiListActivity::onRowAction(const fui::ActionEvent& event) {
  activeNav().selected = event.value;
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  activateIndex(event.value);
}

bool UiListActivity::handleButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const int selected = activeNav().selected;
    if (selected >= 0 && selected < listCount()) activateIndex(selected);
    return true;
  }
  return false;
}

bool UiListActivity::routeListTouch() {
  // Touch goes through the FreeInkApp: render() registered the row hit rects;
  // route the snapshot and let the action trampoline dispatch.
  const auto route = UiAppHost::routeTouch(mappedInput, wantsTouchLongPress);
  // No pressed-state repaint: the render it triggers would drop a slow tap's
  // release inside the uiReady window (tap-to-activate needed two taps), and
  // it costs a second e-ink refresh per tap.
  if (route.routed && app.invalidated()) requestUpdate();
  return static_cast<bool>(route);  // dispatched to the action handler
}

void UiListActivity::moveSelectionTo(const int index) {
  auto& n = activeNav();
  n.selected = index;
  n.follow(listCount());
  requestUpdate();
}

void UiListActivity::loop() {
  if (OptionPopup* popup = activePopup()) {
    // handleInput() as the gate, not isActive(): it keeps returning true while
    // draining the release of the press that closed the popup.
    if (popup->handleInput(mappedInput, [this] { requestUpdate(); })) return;
  }
  if (handleCustomInput()) return;
  if (handleButtons()) return;
  if (routeListTouch()) return;

  // Swipes scroll the viewport; the selection stays put (it may scroll
  // off-screen) and button navigation pulls the view back to it.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    auto& n = activeNav();
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? n.visibleRows : -n.visibleRows;
    if (n.scrollBy(delta, listCount())) requestUpdate();
    return;
  }

  navigateButtons();
}

void UiListActivity::navigateButtons() {
  const int count = listCount();
  auto& n = activeNav();
  buttonNavigator.onNextRelease([this, count, &n] { moveSelectionTo(ButtonNavigator::nextIndex(n.selected, count)); });
  buttonNavigator.onPreviousRelease(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::previousIndex(n.selected, count)); });
  buttonNavigator.onNextContinuous(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::nextPageIndex(n.selected, count, n.visibleRows)); });
  buttonNavigator.onPreviousContinuous(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::previousPageIndex(n.selected, count, n.visibleRows)); });
}

void UiListActivity::syncListViewport(UiScreen& screen, fui::ListProps& props, const bool hasSubtitle) {
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch()) {
    // Non-touch hardware (X3/X4) keeps the original, denser per-theme row
    // height instead of FreeInkUI's touch-target-sized default, so lists fit
    // as many rows per screen as they did before the FreeInkUI migration.
    // props.rowHeight must be set explicitly: screen.list() otherwise falls
    // back to the (touch-friendly) theme token, not this local value.
    const auto& metrics = UITheme::getInstance().getMetrics();
    rowHeight = static_cast<int16_t>(hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight);
    props.rowHeight = rowHeight;
  }
  activeNav().syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, listCount(), props);
}

void UiListActivity::drawRows(UiScreen& screen, const ListLook& look) {
  const int count = listCount();
  if (count <= 0) return;
  fui::ListProps props;
  props.action = ACTION_ROW;
  props.inputMask = wantsTouchLongPress ? static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress)
                                        : static_cast<uint16_t>(fui::InputTouch);  // physical buttons stay in loop()
  syncListViewport(screen, props, look.subtitles);
  auto& n = activeNav();

  // Only the rows on screen are described; the list draws that window with a
  // zero top index and the hit values carry the absolute index.
  const int top = n.top;
  const int end = top + n.visibleRows < count ? top + n.visibleRows : count;
  std::vector<ListRow> rows(static_cast<size_t>(end - top));
  std::vector<fui::ListItem> items(rows.size());
  for (int i = top; i < end; i++) {
    ListRow& row = rows[static_cast<size_t>(i - top)];
    describeRow(i, row);
    fui::ListItem& item = items[static_cast<size_t>(i - top)];
    item.label = row.title.c_str();
    if (look.subtitles && !row.subtitle.empty()) item.subtitle = row.subtitle.c_str();
    if (!row.value.empty()) item.value = row.value.c_str();
    if (look.icons) item.icon = listIconFor(row.icon, look.subtitles ? 32 : 24);
    item.actionValue = static_cast<int16_t>(i);
  }
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.topIndex = 0;
  props.selectedIndex = static_cast<int16_t>(n.selected - top);
  props.scrollIndicator = false;  // the window always fits; the indicator is drawn for the whole list below
  const fui::Rect band = screen.body();
  screen.list(props);
  if (count > n.visibleRows) {
    const auto& theme = screen.theme();
    fui::drawListScrollIndicator(screen.target(), band, static_cast<uint32_t>(count),
                                 static_cast<uint32_t>(n.visibleRows), static_cast<uint32_t>(top),
                                 theme.listScrollWidth, theme.listScrollSide, theme.listScrollInset);
  }
}

void UiListActivity::drawChrome() {
  const char* title = headerTitle();
  if (!title) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title,
                 headerSubtitle());
}

void UiListActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void UiListActivity::render(RenderLock&&) {
  // A showing popup draws over the frame already on the panel (the list it
  // was opened from), with its own hint bar.
  if (OptionPopup* popup = activePopup()) {
    if (popup->processRender(renderer, mappedInput)) return;
  }
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  renderer.displayBuffer();
}
