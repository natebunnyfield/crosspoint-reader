// Where Home's selector lands when Home is (re)entered.
//
// Home has two kinds of row: the recent-book covers (indices 0..bookCount-1)
// and the menu (bookCount + menu index). ActivityManager::goHome() used to
// carry only a HomeMenuItem, so leaving Home through a COVER left the last
// menu row in charge: open Settings, Back, open a cover, Back -- and the
// selector sat on Settings, not the book you just read (owner report
// 2026-09-02: "on back button, navigates go to last home menu item, but should
// go back to last home menu item or hovered book, whatever was actually last
// focused").
//
// The book is found by PATH, not by the index it was opened at: the reader
// re-adds it to the recents on open, so it comes back as cover 0 whichever
// cover it left from. That makes the search mostly a no-op -- 0 is also the
// fallback -- and the load-bearing half of the fix is that a cover resets the
// menu row to NONE at all. The search earns its keep where the reader never
// reached addBook (a load failure, the BMP viewer) and for the Back-on-Home
// resume shortcut, which opens cover 0 while the selector may sit on another
// cover. Pure, so the precedence is truth-tabled on a host; HomeActivity
// cannot be linked into the activity harness.
#pragma once

#include <string>

namespace homelanding {

// menuRow   -- Home was left through a menu row (goHome's item is not NONE).
// menuIndex -- that row's index within the menu (HomeActivity::menuItemToIndex);
//              ignored when menuRow is false.
// bookPath  -- the cover Home was left through, empty if it was a menu row.
// books     -- the covers as reloaded on entry; each has a `.path`.
//
// Takes a bool rather than the HomeMenuItem so the header pulls in nothing
// from ActivityManager.h (which needs FreeRTOS) and the host test stays a
// one-header build.
template <class Books>
int selectorIndex(const bool menuRow, const int menuIndex, const std::string& bookPath, const Books& books) {
  const int bookCount = static_cast<int>(books.size());
  // A menu row wins outright: the goTo* wrappers set it and the cover path is
  // never cleared, so a stale path under a live menu row is the normal state.
  if (menuRow) return bookCount + menuIndex;
  if (!bookPath.empty()) {
    for (int i = 0; i < bookCount; ++i) {
      if (books[i].path == bookPath) return i;
    }
  }
  // Boot, or a book that is no longer among the covers: the top of the list.
  return 0;
}

}  // namespace homelanding
