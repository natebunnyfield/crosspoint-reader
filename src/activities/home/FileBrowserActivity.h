#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class FileBrowserActivity final : public UiListActivity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // Does this browser list entries whose name starts with '.'?
  //
  // Browse Files never does, and that is no longer a setting: `showHiddenFiles`
  // existed as a field but had no row in SettingsList.h, so it was neither
  // device- nor web-settable and — since toJson/fromJson iterate that same list
  // — was never persisted either. It could only ever hold 0. Removed 2026-08-04
  // rather than left as an option nothing could reach.
  //
  // A switch rather than `return false`, because Manage Files was expected to
  // land as a Mode here; it shipped instead as its own FileManagerActivity
  // (docs/manage-files.md), which always lists dot-entries. Kept as a switch so
  // that under -Wswitch a new enumerator does not compile silently.
  bool showsHiddenEntries() const {
    switch (mode) {
      case Mode::Books:
      case Mode::PickFirmware:
        return false;
    }
    return false;
  }

  OptionPopup deletePopup;

  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  int listCount() const override { return static_cast<int>(files.size()); }
  void describeRow(int index, ListRow& row) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Short Back goes up a directory (home/cancel at root), and Confirm
  // activates on RELEASE (a hold is "delete").
  bool handleButtons() override;
  // Header shows the current folder name; the path line under the list and
  // the footer labels depend on path depth and picker mode.
  void drawChrome() override;
  void drawFooter() override;
  OptionPopup* activePopup() override { return &deletePopup; }
  // forceDelete routes the touch long-press to the delete branch; button
  // navigation leaves it false and relies on the Confirm hold time instead.
  // fromButton: only the physical Confirm path may consume the
  // lockNextConfirmRelease swallow — a touch tap has no pending release.
  void activateSelected(bool forceDelete = false, bool fromButton = false);
  // Height of the path line + separator band above the button hints.
  int pathReserved() const;

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
};
