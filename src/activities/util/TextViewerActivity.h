#pragma once

#include <HalStorage.h>

#include <memory>
#include <string>
#include <vector>

#include "PrettyView.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// File viewer for the Manage Files screen (docs/manage-files.md). Views ANY
// file as text: only a small window of the file is ever in RAM, lines are
// word-wrapped to the content width, and the visited page-start offsets are
// kept so paging backwards replays them exactly.
//
// Confirm toggles the pretty view where one exists (PrettyView::detect):
// json/html re-render readable, epub and .crosspoint binaries show a metadata
// card (all paginated from an in-memory document via the same layout path),
// bmp/png/jpg render as an image. A persistent right-edge scrollbar shows the
// page's position and share of the file in both text modes.
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

  PrettyView::Kind prettyKind = PrettyView::Kind::None;
  bool prettyMode = false;
  bool prettyUnavailable = false;  // generation failed once: stop offering it
  std::string prettyText;          // generated document for non-image kinds

  // Byte offsets where each visited page starts; back() = current page.
  std::vector<uint32_t> pageStarts;
  uint32_t nextPageOffset = 0;

  std::vector<std::string> lines;  // laid-out lines of the current page
  int maxLines = 1;
  int maxWidth = 0;
  int lineHeight = 1;
  int contentTop = 0;

  bool imageMode() const {
    return prettyMode && (prettyKind == PrettyView::Kind::ImageBmp || prettyKind == PrettyView::Kind::ImageDecoded);
  }
  uint32_t sourceSize() const;
  int byteAt(uint32_t offset);
  uint32_t layoutPage(uint32_t start);
  void layoutCurrentPage();
  void pageForward();
  void pageBack();
  void togglePretty();
  void resetPagination();
  void renderImage();
  void drawScrollBar() const;
  // The page's drawing, without the clear or the refresh, so render() and the
  // grayscale anti-aliasing pass share one body.
  void drawFrame();

 public:
  explicit TextViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                              std::string displayTitle)
      : Activity("TextViewer", renderer, mappedInput), path(std::move(path)), displayTitle(std::move(displayTitle)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
