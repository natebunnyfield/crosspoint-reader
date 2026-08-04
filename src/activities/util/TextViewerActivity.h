#pragma once

#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Plain-text viewer for the Manage Files screen (docs/manage-files.md). Views
// ANY file as text: only a small window of the file is ever in RAM, lines are
// word-wrapped to the content width, and the visited page-start offsets are
// kept so paging backwards replays them exactly.
class TextViewerActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  std::string path;
  std::string displayTitle;

  HalFile file;  // member handle: closed in onExit()
  std::unique_ptr<char[]> chunk;
  uint32_t chunkStart = 0;
  uint32_t chunkLen = 0;
  uint32_t fileSize = 0;
  bool openFailed = false;

  // Byte offsets where each visited page starts; back() = current page.
  std::vector<uint32_t> pageStarts;
  uint32_t nextPageOffset = 0;

  std::vector<std::string> lines;  // laid-out lines of the current page
  int maxLines = 1;
  int maxWidth = 0;
  int lineHeight = 1;
  int contentTop = 0;

  int byteAt(uint32_t offset);
  uint32_t layoutPage(uint32_t start);
  void layoutCurrentPage();
  void pageForward();
  void pageBack();

 public:
  explicit TextViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                              std::string displayTitle)
      : Activity("TextViewer", renderer, mappedInput), path(std::move(path)), displayTitle(std::move(displayTitle)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
