#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// Home-screen file manager (design: docs/manage-files.md). Shows every file on
// the SD card — hidden files included — with rename, move (one-slot clipboard)
// and delete via a per-item action menu.
class FileManagerActivity final : public Activity {
  enum class MenuAction : uint8_t { MoveHere, View, Edit, Rename, Normalize, Duplicate, Summarize, Move, Delete };

  ButtonNavigator buttonNavigator;
  OptionPopup popup;

  size_t selectorIndex = 0;
  bool lockLongPressBack = false;
  // True when this activity (or its action popup) consumed a press whose
  // release must not also act on the list below.
  bool lockNextConfirmRelease = false;
  bool lockNextBackRelease = false;
  // True once a Confirm press edge was seen inside this activity; gates the
  // long-press action menu so a press carried in from another screen can't
  // trigger it.
  bool confirmPressSeen = false;

  std::string basepath = "/";
  std::vector<std::string> files;  // directories carry a trailing '/'
  std::unique_ptr<char[]> fileNameBuffer;

  // One-slot move clipboard: empty sourcePath = nothing armed.
  std::string moveSourcePath;
  std::string moveSourceName;
  bool moveSourceIsDir = false;

  // Popup option index -> action for the currently open menu.
  std::vector<MenuAction> menuActions;

  void loadFiles();
  size_t findEntry(const std::string& name) const;
  std::string fullPathOf(const std::string& entry) const;
  int statusLineReserved() const;

  void openActionMenu();
  void runMenuAction(MenuAction action);
  bool probeBookMetadata(const std::string& entry, std::string& title, std::string& author);
  std::vector<std::string> buildInfoLines(const std::string& entry);
  void duplicateFile(const std::string& entry);
  void viewFile(const std::string& entry);
  // Opens the rename keyboard for `entry`. `seedName` pre-fills a proposed
  // name (used by Normalize); empty = seed with the current name.
  void startRename(const std::string& entry, const std::string& seedName = "");
  void startNormalize(const std::string& entry);
  void toggleSummarize(const std::string& entry);
  void armMove(const std::string& entry);
  void performMoveHere();
  void confirmDelete(const std::string& entry);
  void showError(const char* message);

 public:
  // startPath: directory to open initially (empty = SD root "/").
  explicit FileManagerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string startPath = {})
      : Activity("FileManager", renderer, mappedInput), basepath(startPath.empty() ? "/" : std::move(startPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
