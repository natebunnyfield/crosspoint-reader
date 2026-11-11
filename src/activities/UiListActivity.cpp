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

  // Swipes page the list. The view is derived from the selection (drawRows),
  // so a swipe moves the selection a page, as the pre-port lists did; the
  // Picco never reports a swipe (HalGPIO::wasSwipe).
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    auto& n = activeNav();
    const int count = listCount();
    moveSelectionTo(swipe == MappedInputManager::SwipeDir::Up
                        ? ButtonNavigator::nextPageIndex(n.selected, count, n.visibleRows)
                        : ButtonNavigator::previousPageIndex(n.selected, count, n.visibleRows));
    return;
  }

  navigateButtons();
}

void UiListActivity::navigateButtons() {
  const int count = listCount();
  auto& n = activeNav();
  // The front pair steps one row on the PRESS edge; a hold past 500 ms then
  // continues as page jumps. Owner ruling 2026-11-07 (docs/ui-conventions.md,
  // "List cursor moves fire on PRESS; a hold then pages"). Upstream's base
  // steps on release so a hold can page instead, which puts the whole click
  // in front of every move.
  buttonNavigator.onNextPress([this, count, &n] { moveSelectionTo(ButtonNavigator::nextIndex(n.selected, count)); });
  buttonNavigator.onPreviousPress(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::previousIndex(n.selected, count)); });
  buttonNavigator.onNextContinuous(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::nextPageIndex(n.selected, count, n.visibleRows)); });
  buttonNavigator.onPreviousContinuous(
      [this, count, &n] { moveSelectionTo(ButtonNavigator::previousPageIndex(n.selected, count, n.visibleRows)); });
  // The SIDE pair pages by a whole screenful (docs/ui-conventions.md, "Side
  // buttons should page, not repeat the front buttons"). pageDown/pageUp clamp
  // at the ends and return false when nothing moved, so a short list costs no
  // redraw.
  buttonNavigator.onPageNext([this, count, &n] {
    int index = n.selected;
    if (!ButtonNavigator::pageDown(index, count, n.visibleRows)) return;
    moveSelectionTo(index);
  });
  buttonNavigator.onPagePrevious([this, count, &n] {
    int index = n.selected;
    if (!ButtonNavigator::pageUp(index, count, n.visibleRows)) return;
    moveSelectionTo(index);
  });
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
  auto& n = activeNav();
  if (n.selected >= count) n.selected = count - 1;
  if (n.selected < 0) n.selected = 0;

  // Lyra Six paints the rows (GUI.drawList), so a converted screen looks as
  // it did before the FUI port: the theme's row heights, UI_10 titles, the
  // rounded light band, the value badges, its scroll bar and its PAGED
  // layout. Paging matters beyond looks: a cursor move inside a page changes
  // only the two rows involved, which GfxRenderer's tile diff presents as a
  // small window, where a list that scrolls by one row at its edge repaints
  // every row on every step there. The viewport is therefore derived from the
  // selection exactly as drawList derives it.
  const fui::Rect band = screen.body();
  const Rect rect{band.x, band.y, band.width, band.height};
  const int rowStep = GUI.getListRowStep(look.subtitles);
  const int pageItems = GUI.getListPageItems(rect.height, look.subtitles);
  n.visibleRows = pageItems;
  n.followOnBuild = false;
  const int pageStart = n.selected / pageItems * pageItems;
  n.top = pageStart;

  // Only the rows on this page are described (drawList asks for no others).
  const int pageCount = pageStart + pageItems < count ? pageItems : count - pageStart;
  pageRows.resize(static_cast<size_t>(pageCount));
  for (int i = 0; i < pageCount; i++) {
    pageRows[static_cast<size_t>(i)] = ListRow{};
    describeRow(pageStart + i, pageRows[static_cast<size_t>(i)]);
  }
  const auto rowAt = [this, pageStart](const int index) -> const ListRow& {
    return pageRows[static_cast<size_t>(index - pageStart)];
  };
  GUI.drawList(
      renderer, rect, count, n.selected, [&rowAt](int index) { return rowAt(index).title; },
      look.subtitles ? std::function<std::string(int)>([&rowAt](int index) { return rowAt(index).subtitle; }) : nullptr,
      look.icons ? std::function<UIIcon(int)>([&rowAt](int index) { return rowAt(index).icon; }) : nullptr,
      [&rowAt](int index) { return rowAt(index).value; }, look.highlightValue,
      [&rowAt](int index) { return rowAt(index).dimmed; });

  // FreeInkUI still owns touch: one hit rect per drawn row, routed by the
  // app (routeListTouch) to ACTION_ROW with the absolute index.
  const uint16_t mask = wantsTouchLongPress ? static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress)
                                            : static_cast<uint16_t>(fui::InputTouch);
  for (int i = 0; i < pageCount; i++) {
    const fui::Rect hit{band.x, static_cast<int16_t>(band.y + i * rowStep), band.width, static_cast<int16_t>(rowStep)};
    screen.frame().hit(hit, ACTION_ROW, static_cast<int16_t>(pageStart + i), mask);
  }
}

void UiListActivity::drawEmptyMessage(UiScreen& screen, const char* message) {
  // The theme's empty-list line, as the pre-port screens drew it.
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.drawText(UI_10_FONT_ID, screen.body().x + metrics.contentSidePadding, screen.body().y + 20, message);
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
