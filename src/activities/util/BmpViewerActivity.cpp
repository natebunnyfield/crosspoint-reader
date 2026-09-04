#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        if (fname.length() >= 4 && fname.substr(fname.length() - 4) == ".bmp") {
          siblingImages.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(siblingImages);

  for (size_t i = 0; i < siblingImages.size(); ++i) {
    if (siblingImages[i] == fileName) {
      currentImageIndex = static_cast<int>(i);
      break;
    }
  }
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  HalFile file;

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  // 1. Open the file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                      currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), tr(STR_SET_SLEEP_COVER), (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));

      GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      // Assuming drawBitmap defaults to 0,0 crop if omitted, or pass explicitly: drawBitmap(bitmap, x, y, pageWidth,
      // pageHeight, 0, 0)
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      // Single pass for non-grayscale images

      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      imageValid = true;

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_BMP_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }

    file.close();
  } else {
    // Handle file open error
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

namespace {
// One name for the destination, so the guard above and the open below
// cannot drift apart.
constexpr const char* SLEEP_COVER_PATH = "/sleep.bmp";
}  // namespace

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  // Setting the sleep cover FROM the sleep cover destroys it. openFileForWrite
  // is O_TRUNC, so opening /sleep.bmp for output truncated the very file inFile
  // was reading: the read then returned 0, the copy loop never ran, and success
  // had already been set to true above it -- so it reported Done and left a
  // zero-byte sleep screen. Nothing to copy here anyway; the file already IS
  // the cover, so just record the mode.
  if (filePath == SLEEP_COVER_PATH) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
    delay(1000);
    onEnter();
    return;
  }

  bool success = false;
  HalFile inFile, outFile;
  if (Storage.openFileForRead("BMP", filePath, inFile)) {
    // The copy buffer was a 2048-byte stack array, which made this frame 2432 bytes
    // (measured with -fstack-usage on the riscv32 target) — 9.5x the 256-byte limit in
    // the Resource Protocol. It lived in the prologue, so it stayed on the stack across
    // SETTINGS.saveToFile() and the full onEnter() re-render below, i.e. at the deepest
    // point of this call chain. On the heap it is also released before that re-render.
    constexpr size_t COPY_BUFFER_SIZE = 2048;
    const auto buffer = makeUniqueNoThrow<char[]>(COPY_BUFFER_SIZE);
    if (!buffer) {
      LOG_ERR("BMP", "OOM: %u byte sleep cover copy buffer", static_cast<unsigned>(COPY_BUFFER_SIZE));
    } else if (Storage.openFileForWrite("BMP", SLEEP_COVER_PATH, outFile)) {
      // Count bytes rather than assuming. success used to be set before the
      // loop, so a copy that moved NOTHING still reported Done -- which is how
      // a zero-byte sleep screen looked like it had worked.
      int bytesRead;
      size_t copied = 0;
      bool wrote = true;
      while ((bytesRead = inFile.read(buffer.get(), COPY_BUFFER_SIZE)) > 0) {
        if (outFile.write(buffer.get(), bytesRead) != bytesRead) {
          wrote = false;
          break;
        }
        copied += static_cast<size_t>(bytesRead);
      }
      success = wrote && copied > 0;
      if (!success) LOG_ERR("BMP", "sleep cover copy moved %u bytes", (unsigned)copied);
      outFile.close();
    }
    inFile.close();
  }

  if (success) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  onEnter();
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  auto openSibling = [this](const int delta) {
    if (currentImageIndex < 0) {
      return false;
    }
    const int nextIndex = currentImageIndex + delta;
    if (siblingImages.size() <= 1 || nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) {
      return false;
    }
    currentImageIndex = nextIndex;
    std::string dirPath = FsHelpers::extractFolderPath(filePath);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + siblingImages[currentImageIndex];
    onEnter();
    return true;
  };

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, filePath.c_str(),
                                        {nullptr, [](void*) { activityManager.goHome(); }})) {
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    openSibling(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Only a file that decoded: the error screens draw a Back hint alone,
    // and copying an unreadable file over /sleep.bmp lost a working cover
    // (second-pass audit, 2026-09-04).
    if (imageValid) doSetSleepCover();
    return;
  }

  // The side pair steps siblings as PagePrevious/PageNext, honoring
  // sideButtonLayout like every other paging surface; raw Up/Down stepped
  // the gallery backward under a swapped layout (audit F11's class).
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::PagePrevious)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageNext)) {
    openSibling(1);
    return;
  }
}
