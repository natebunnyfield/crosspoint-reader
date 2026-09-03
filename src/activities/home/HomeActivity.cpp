#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HomeLanding.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/NoteNaming.h"

// Where Create Note writes. Root keeps notes visible next to the other files
// the owner browses in Manage Files.
static constexpr const char* NOTES_DIR = "/";

int HomeActivity::getMenuItemCount() const {
  // This count is what bounds selectorIndex — leave it stale after adding a
  // menu row and the row renders but the selector can never reach it.
  // Recents, Browse, Manage Files, Transfer, Create Note, Claude, Update
  // Library, Settings -- plus Update Firmware on builds that can actually
  // flash one.
#ifndef CROSSPOINT_NO_DEVICE_FLASH
  int count = 9;
#else
  int count = 8;
#endif
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  return count;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // Menu row, else the cover Home was left through, else the top cover.
  selectorIndex = homelanding::selectorIndex(initialMenuItem != HomeMenuItem::NONE, menuItemToIndex(initialMenuItem),
                                             initialBookPath, recentBooks);
  LOG_DBG("HOME", "Landing on row %d of %d (%d covers)", selectorIndex, getMenuItemCount(),
          static_cast<int>(recentBooks.size()));

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();

  auto activateSelection = [this] {
    recordFocus();
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::MANAGE_FILES:
        onManageFilesOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      case HomeMenuItem::CREATE_NOTE:
        onCreateNoteOpen();
        break;
#ifndef CROSSPOINT_NO_DEVICE_FLASH
      case HomeMenuItem::UPDATE_FIRMWARE:
        onFirmwareUpdateOpen();
        break;
#endif
      case HomeMenuItem::UPDATE_LIBRARY:
        onLibraryUpdateOpen();
        break;
      case HomeMenuItem::CLAUDE:
        onClaudeOpen();
        break;
      default:
        break;
    }
  };

  // Wrap-around is wrong when the menu lives on a second page (Lyra Six): Up on
  // the first book would jump to Settings and Down on Settings back to the books,
  // i.e. a single press teleports across a page boundary with no visible relation
  // to the direction pressed. Clamp at both ends instead, so the ends of the list
  // are perceptible and paging is always one step in the direction you pressed.
  // Every other theme keeps its existing wrap — ButtonNavigator::nextIndex /
  // previousIndex are shared by many activities and are deliberately not touched.
  const bool clampEnds = UITheme::getInstance().getMetrics().homeMenuOnSecondPage;

  buttonNavigator.onNext([this, menuCount, clampEnds] {
    if (clampEnds) {
      if (selectorIndex + 1 >= menuCount) return;  // already at the end; nothing moved, no redraw
      selectorIndex++;
    } else {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    }
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount, clampEnds] {
    if (clampEnds) {
      if (selectorIndex <= 0) return;  // already at the start
      selectorIndex--;
    } else {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    }
    requestUpdate();
  });

  // The SIDE pair pages, where the FRONT pair above steps one row. This screen
  // is the one the owner named ("make side buttons be page up and page down,
  // when they are identical functionality to front buttons, like on the home
  // screen"), and it is also the screen where "one screenful" and "the next
  // section" are the same thing: on the two-page home the covers own page 0
  // ([0, bookCount)) and the menu owns page 1 ([bookCount, menuCount)), and the
  // page is DERIVED from selectorIndex by render() below. So paging means
  // landing on the first row of the other page.
  //
  // Clamped, per docs/ui-conventions.md — no wrap from the menu back to the
  // covers. And dead when the home is NOT split (no recent books, or a theme
  // that puts everything on one page): there is no page to turn, so a held side
  // button costs nothing rather than burning a redraw per repeat.
  const int bookCount = static_cast<int>(recentBooks.size());
  const bool splitPages = metrics.homeMenuOnSecondPage && bookCount > 0;

  // ...AND ON THE LAST PAGE IT SELECTS THE LAST ROW, rather than doing nothing.
  // Owner 2026-08-18: "page down on the last page in a list (like Home) should
  // select last item."
  //
  // This is what ButtonNavigator::pageDown has always done for the nine lists
  // that use it -- "CLAMPS at both ends ... the press slides the selection onto
  // the final/first row so the end of the list is perceptible instead of
  // teleporting" (its own contract, ruling 2026-08-14). Home could not use that
  // helper because its pages are SECTIONS of unequal length rather than
  // screenfuls, so it grew its own two-line version and quietly lost the clamp
  // along with it. The end of the list was simply unreachable from the side
  // pair.
  buttonNavigator.onPageNext([this, bookCount, splitPages, menuCount] {
    const int lastRow = menuCount - 1;
    if (menuCount <= 0) return;
    if (splitPages && selectorIndex < bookCount) {
      selectorIndex = bookCount;  // covers -> first menu row
    } else if (selectorIndex < lastRow) {
      selectorIndex = lastRow;  // already on the last page -> its last row
    } else {
      return;  // already there; no redraw
    }
    requestUpdate();
  });

  buttonNavigator.onPagePrevious([this, bookCount, splitPages, menuCount] {
    if (menuCount <= 0) return;
    if (splitPages && selectorIndex >= bookCount) {
      selectorIndex = 0;  // menu -> first cover
    } else if (selectorIndex > 0) {
      selectorIndex = 0;  // already on the first page -> its first row
    } else {
      return;
    }
    requestUpdate();
  });

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card). swallowUntilIdle() in ActivityManager
  // covers the stale release from the previous activity's exit press.
  // The shortcut is not a focus: Back from that book lands on the row the
  // selector was on when Back was pressed (adversarial review 2026-09-02).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !recentBooks.empty()) {
    recordFocus();
    onSelectBook(recentBooks[0].path);
    return;
  }

  int tx = 0;
  int ty = 0;
  if (!recentBooks.empty() && mappedInput.wasScreenTouchDown(tx, ty) && tx >= 0 && tx < renderer.getScreenWidth() &&
      ty >= metrics.homeTopPadding && ty < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    if (selectorIndex != 0) {
      selectorIndex = 0;
      requestUpdate();
    }
    return;
  }

  if (!recentBooks.empty() &&
      mappedInput.wasTapInRect(0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight)) {
    selectorIndex = 0;
    activateSelection();
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  const int renderedMenuCount =
      menuCount - (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
  int menuRow = -1;
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, metrics.menuRowHeight + metrics.menuSpacing,
                                              renderedMenuCount, 0, INT32_MAX, metrics.menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int bookCount = static_cast<int>(recentBooks.size());

  // Cover grid shape. Every theme except Lyra Six is 1x1, for which coverRows is
  // 1 and coverAreaHeight is exactly metrics.homeCoverTileHeight — the value
  // this function used before the grid existed.
  const int coverColumns = std::max(1, metrics.homeCoverColumns);
  const int coverRows =
      bookCount > 0 ? std::min(std::max(1, metrics.homeCoverRows), (bookCount + coverColumns - 1) / coverColumns) : 1;
  const int coverAreaHeight = metrics.homeCoverTileHeight * coverRows;

  // Two-page home (Lyra Six): the covers own page 0 and the menu rows own
  // page 1. There is no separate page state to keep in sync — the page is
  // DERIVED from selectorIndex, the same way BaseTheme::drawList derives its
  // page from the selected row, so Up/Down alone moves between pages and
  // wrapping at either end crosses back over. Indices [0, bookCount) are
  // covers, [bookCount, getMenuItemCount()) are menu rows.
  //
  // With no recent books at all there is nothing to put on page 0, so the
  // split is off and the layout falls back to the one-page form: the theme's
  // "no open book" panel (one row tall) above the menu, exactly as Lyra
  // Extended renders that case.
  const bool splitPages = metrics.homeMenuOnSecondPage && bookCount > 0;
  const bool showCovers = !splitPages || selectorIndex < bookCount;
  const bool showMenu = !splitPages || selectorIndex >= bookCount;

  renderer.clearScreen();
  // Only the cover page owns that region of the framebuffer; restoring the
  // snapshot while the menu page is up would blit stale covers under the rows.
  bool bufferRestored = showCovers && coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  if (showCovers) {
    // Record the tile rect so storeCoverBuffer (called from the theme) knows
    // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
    // instead of the 48 KB full framebuffer the previous bind captured.
    coverRectX = 0;
    coverRectY = metrics.homeTopPadding;
    coverRectW = pageWidth;
    coverRectH = coverAreaHeight;

    GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, coverAreaHeight}, recentBooks,
                            selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                            std::bind(&HomeActivity::storeCoverBuffer, this));
  }

  if (showMenu) {
    // Build menu items dynamically
    // Order is the owner's ruling (2026-08-06). These two vectors and BOTH
    // index maps in HomeActivity.h must stay in the same order — see the note
    // there; they are four separate sources of truth for one list.
    std::vector<const char*> menuItems = {tr(STR_MENU_RECENT_BOOKS), tr(STR_BROWSE_FILES), tr(STR_MANAGE_FILES),
                                          tr(STR_FILE_TRANSFER),     tr(STR_CREATE_NOTE),  "Claude",
                                          tr(STR_SETTINGS_TITLE)};
    std::vector<UIIcon> menuIcons = {Recent, Folder, ManageFiles, Transfer, CreateNote, ClaudeMark, Settings};
#ifndef CROSSPOINT_NO_DEVICE_FLASH
    // One-button firmware update, immediately above Settings. Inserted rather
    // than written into the literals above so the iOS build -- which cannot
    // write a flash partition -- drops the row without disturbing the order of
    // the six that stay. The two index maps in HomeActivity.h and the count in
    // getMenuItemCount() carry the same guard; all five must agree.
    menuItems.insert(menuItems.end() - 1, tr(STR_UPDATE_FIRMWARE));
    menuIcons.insert(menuIcons.end() - 1, Update);
#endif
    // Update Library, immediately above Settings (owner's ask 2026-08-21) and
    // below Update Firmware where that row exists. Unconditional: every build
    // can write /books/, including the ones that cannot flash firmware.
    menuItems.insert(menuItems.end() - 1, tr(STR_UPDATE_LIBRARY));
    menuIcons.insert(menuIcons.end() - 1, Library);

    if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
      // Insert Continue Reading at the top if enabled in theme
      menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
      menuIcons.insert(menuIcons.begin(), Book);
    }

    // On its own page the menu starts just below the header instead of below
    // the covers. The one-page geometry is left exactly as it was.
    const Rect menuRect =
        splitPages ? Rect{0, metrics.homeTopPadding + metrics.homeMenuTopOffset, pageWidth,
                          pageHeight - (metrics.homeTopPadding + metrics.homeMenuTopOffset + metrics.buttonHintsHeight)}
                   : Rect{0, metrics.homeTopPadding + coverAreaHeight + metrics.homeMenuTopOffset, pageWidth,
                          // Subtract the SAME terms the y offset added, so the rect ends at
                          // pageHeight - buttonHintsHeight exactly, like the split branch above.
                          // This used to subtract headerHeight + verticalSpacing instead of
                          // coverAreaHeight -- a leftover from when the menu sat under a header
                          // rather than under the covers. On Lyra Six (cover tile 312, header 84,
                          // spacing 16, hints 40) that put the rect's bottom at 972 on an 800px
                          // screen, so drawButtonMenu laid out a row that fell off the panel and
                          // drawPixel dropped it one ERR line at a time -- 1756 per Home paint.
                          pageHeight - (metrics.homeTopPadding + coverAreaHeight + metrics.homeMenuTopOffset +
                                        metrics.buttonHintsHeight)};

    // Menu-relative selection. A theme that folds Continue Reading INTO the
    // menu gives that row index 0, so selectorIndex is already menu-relative;
    // otherwise the covers sit ahead of the menu and have to be subtracted.
    // Lyra Six, the only theme left, is the second kind, but the branch stays:
    // homeContinueReadingInMenu is a live metric, not a per-theme constant. On
    // page 1 of a split home this is selectorIndex - bookCount >= 0, i.e. the
    // row the selector is really on.
    GUI.drawButtonMenu(
        renderer, menuRect, static_cast<int>(menuItems.size()),
        metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - bookCount,
        [&menuItems](int index) { return std::string(menuItems[index]); },
        [&menuIcons](int index) { return menuIcons[index]; });
  }

  const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::recordFocus() {
  // What Home is leaving from, for goHome() to land on (HomeLanding.h). A cover
  // is remembered by path; a menu row is set here AND by the goTo* wrapper it
  // opens, so the row is right even for a leave that opens no wrapper.
  if (selectorIndex < static_cast<int>(recentBooks.size())) {
    activityManager.lastHomeMenuItem = HomeMenuItem::NONE;
    activityManager.lastHomeBookPath = recentBooks[selectorIndex].path;
  } else {
    activityManager.lastHomeMenuItem = indexToMenuItem(selectorIndex - static_cast<int>(recentBooks.size()));
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onManageFilesOpen() { activityManager.goToFileManager(); }

// Create Note makes a NEW note every run — it must never reopen a fixed file.
// Seconds come from the C library clock (set by syncFromNTP); the RTC only
// resolves to minutes. With no synced clock at all the stamp repeats, so
// uniqueNotePath's -N suffix is what actually guarantees a fresh file.
void HomeActivity::onCreateNoteOpen() {
  uint16_t y = 1970;
  uint8_t mo = 1, d = 1, h = 0, mi = 0, sec = 0;

  const time_t now = time(nullptr);
  if (now > 1700000000) {  // past 2023, so a real clock rather than an unset one
    struct tm utc;
    gmtime_r(&now, &utc);
    y = static_cast<uint16_t>(utc.tm_year + 1900);
    mo = static_cast<uint8_t>(utc.tm_mon + 1);
    d = static_cast<uint8_t>(utc.tm_mday);
    h = static_cast<uint8_t>(utc.tm_hour);
    mi = static_cast<uint8_t>(utc.tm_min);
    sec = static_cast<uint8_t>(utc.tm_sec);
  } else {
    halClock.getDateTime(y, mo, d, h, mi);  // minutes only; seconds stay 0
  }

  const std::string stamp = notenaming::formatStamp(y, mo, d, h, mi, sec);
  const std::string path =
      notenaming::uniqueNotePath(NOTES_DIR, stamp, [](const char* p) { return Storage.exists(p); });
  LOG_INF("HOME", "new note: %s", path.c_str());
  activityManager.goToNoteEditor(path);
}

void HomeActivity::onClaudeOpen() { activityManager.goToClaudeChat(); }

#ifndef CROSSPOINT_NO_DEVICE_FLASH
void HomeActivity::onFirmwareUpdateOpen() { activityManager.goToFirmwareUpdate(); }
#endif

void HomeActivity::onLibraryUpdateOpen() { activityManager.goToLibraryUpdate(); }
