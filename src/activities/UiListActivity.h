#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/UiAppHost.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

// Base for activities hosting a single FreeInkUI list screen. UiAppHost owns
// the app-hosting protocol (render target, FreeInkApp, uiReady handshake);
// this base layers the list protocol on top: the touch-routing / swipe-scroll
// / button-navigation loop (swipes scroll the viewport without moving the
// selection; buttons move the selection and pull the viewport along via
// fui::ListNav), and the render skeleton (chrome, app, footer). Subclasses
// supply the data: item count, screen content, and what activating a row does.
//
// Screens that are not a single list (sliders, tab layouts, state machines)
// should NOT derive from this — they use UiAppHost directly.
//
// Fork adaptation of upstream's contract (9e3cea2e5): rows are DESCRIBED one
// at a time (describeRow) and the base lays out only the rows on screen
// (drawRows), instead of every subclass pre-building a ListItem and its
// strings for every entry. A 500-file folder or a long TOC costs one screenful
// of strings per render, and the TOC is not read from the card in full before
// the first frame.
//
// The rows are PAINTED by the theme (GUI.drawList, Lyra Six) rather than by
// fui::list, so converted screens keep the fork's look and paging; FreeInkUI
// hosts the screen and routes touch through one hit rect per drawn row. See
// drawRows().
class UiListActivity : public Activity, protected UiAppHost {
 public:
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 protected:
  // Base-owned row action; subclass-registered actions start at ACTION_USER.
  static constexpr freeink::ui::ActionId ACTION_ROW = 1;
  static constexpr freeink::ui::ActionId ACTION_USER = 2;

  UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                 bool wantsTouchLongPress = false);

  // What one row shows. Empty strings mean "none"; icon None means no icon.
  struct ListRow {
    std::string title;
    std::string subtitle;
    std::string value;
    UIIcon icon = UIIcon::None;
    bool dimmed = false;
  };
  // Which optional parts a list carries. Uniform per list (row height and
  // icon column are list-wide), so it is passed once to drawRows().
  struct ListLook {
    bool subtitles = false;
    bool icons = false;
    // The value column inverts on the selected row (Settings) rather than
    // sitting on the selection band.
    bool highlightValue = false;
  };

  // --- subclass contract -----------------------------------------------------
  // Current number of list rows (re-read every loop pass; may change).
  virtual int listCount() const = 0;
  // Fill `row` for list index `index` (0 <= index < listCount()). Runs on the
  // render task, for the rows on screen only.
  virtual void describeRow(int index, ListRow& row) const = 0;
  // Build the screen: content margin, then drawRows(). Runs on the render task
  // via the base trampoline.
  virtual void buildScreen(UiScreen& screen) = 0;
  // Activate a row (touch tap or Confirm on the selection). Handlers that
  // leave this screen should call app.clearTapFlash() so a lingering flash
  // can't gray an unrelated element on the next render.
  virtual void activateIndex(int index) = 0;
  // Touch long-press on a row; only fires when the subclass opted in via the
  // wantsTouchLongPress constructor flag.
  virtual void onRowLongPress(int /*index*/) {}
  // The selection/viewport state the loop, sync, and row dispatch operate on.
  // Default is the single `nav` member; UiTabListActivity redirects it to the
  // active tab's per-tab state.
  virtual freeink::ui::ListNav& activeNav() { return nav; }
  // Bounds-checked ACTION_ROW dispatch. Default: selection follows the tapped
  // row, then long-press/activate. UiTabListActivity remaps row -> ring.
  virtual void onRowAction(const freeink::ui::ActionEvent& event);
  // The button-navigation tail of loop(): the front pair steps the selection
  // on PRESS and a hold jumps by page; the side pair pages. UiTabListActivity
  // replaces it with the ring walk.
  virtual void navigateButtons();
  // First hook in loop(); return true when the pass is consumed (extra
  // buttons, gestures, modes). Runs after the popup gate and before the base
  // button handling.
  virtual bool handleCustomInput() { return false; }
  // Back/Confirm handling; override wholesale for press/release or hold
  // variants. Return true when a button consumed the pass.
  virtual bool handleButtons();
  virtual void onBackButton() { finish(); }
  // Header band, drawn before the app renders. Default paints GUI.drawHeader
  // with headerTitle()/headerSubtitle(); override either for custom chrome.
  virtual const char* headerTitle() const { return nullptr; }
  // Right-aligned secondary header text (Settings shows the version there).
  virtual const char* headerSubtitle() const { return nullptr; }
  virtual void drawChrome();
  // Button hints, drawn after the app renders. Default: Back/Select/Up/Down.
  virtual void drawFooter();
  // The screen's OptionPopup, if it has one (the fork's one interactive modal,
  // docs/ui-conventions.md "Popup idioms"). While it is showing it owns the
  // input (loop() gates on handleInput before anything else) and render()
  // paints it over the last frame instead of rebuilding the list under it.
  virtual OptionPopup* activePopup() { return nullptr; }

  // --- helpers ---------------------------------------------------------------
  // Measure visibleRows for the screen band, apply follow-on-build, clamp the
  // viewport, and write selection/viewport into props.
  // hasSubtitle: rows carry a second (subtitle) text line, so on non-touch
  // hardware the denser override below uses the theme's *-with-subtitle row
  // height instead of its single-line one (see syncListViewport()).
  void syncListViewport(UiScreen& screen, freeink::ui::ListProps& props, bool hasSubtitle = false);
  // Lay out the rows on screen in the remaining body of `screen` and register
  // their hit rects. Call from buildScreen() once the margins are set.
  void drawRows(UiScreen& screen, const ListLook& look);
  // The empty-list line (left-aligned UI_10 at the top of the body), as the
  // pre-port screens drew it.
  void drawEmptyMessage(UiScreen& screen, const char* message);
  // Move the selection to index and pull the viewport to it.
  void moveSelectionTo(int index);

  // --- shared state ----------------------------------------------------------
  // Selection + viewport (selected/top/visibleRows/followOnBuild). Access via
  // activeNav() in shared code; `nav` is the single-list default storage.
  freeink::ui::ListNav nav;
  ButtonNavigator buttonNavigator;

 private:
  static void screenTrampoline(UiScreen& screen, void* user);
  static void rowActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  // Named apart from UiAppHost::routeTouch so the host overload stays visible
  // (not name-hidden) to subclasses with extra touch surfaces.
  bool routeListTouch();

  const bool wantsTouchLongPress;
  // The page drawRows() last described. Render task only.
  std::vector<ListRow> pageRows;
};
