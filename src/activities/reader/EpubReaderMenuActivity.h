#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class EpubReaderMenuActivity final : public Activity {
 public:
  // Menu actions available from the reader menu.
  //
  // AUTO_PAGE_TURN, ROTATE_SCREEN, BOOKMARKS, TOGGLE_BOOKMARK, DISPLAY_QR,
  // GO_TO_PERCENT, GO_HOME and SYNC are deliberately retained here and in
  // EpubReaderActivity's dispatch switch even though buildMenuItems() no longer
  // offers them: only the menu surface was withdrawn, the behaviour behind each
  // one is still intact, so restoring any of them is a one-line push_back
  // rather than a rebuild. GO_HOME is also reached by a short Back press and
  // SYNC by a long Confirm press (SETTINGS.longPressMenuFunction ==
  // LP_MENU_KOSYNC), so those two remain live paths, not just dead cases.
  enum class MenuAction {
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    SELECT_FONT,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    DICTIONARY
  };

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static std::vector<MenuItem> buildMenuItems(bool hasFootnotes);
  void closeCancelled();

  // Fixed menu layout
  const std::vector<MenuItem> menuItems;

  int selectedIndex = 0;

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  // True while the button press that closed the popup is still held; its release
  // must not fall through to the menu's own Back/Confirm handlers.
  bool popupClosing = false;
  std::string title = "Reader Menu";
  // Still carried out in MenuResult so EpubReaderActivity's callback keeps its
  // shape, but no longer editable from here: both pickers were removed with
  // their menu items, so these simply echo back the values passed in.
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
