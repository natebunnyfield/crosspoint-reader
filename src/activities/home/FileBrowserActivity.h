#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
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

  ButtonNavigator buttonNavigator;
  OptionPopup deletePopup;

  size_t selectorIndex = 0;

  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
