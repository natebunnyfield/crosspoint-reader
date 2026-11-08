#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/FsOps.h"

namespace fui = freeink::ui;

namespace {
// Held Back -> root folder was killed 2026-09-01 (owner ruling,
// docs/hold-gestures.md); this threshold now gates only held Confirm ->
// delete (Books mode).
constexpr unsigned long DELETE_HOLD_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

void FileBrowserActivity::loadFiles() {
  // Build into a local, publish with a swap under the lock -- the same B-022
  // shape as FileManagerActivity::loadFiles, and for the same reason: this runs
  // on the loop task while render() walks `files` per row on the render task.
  // See the long note there.
  std::vector<std::string> built;

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    RenderLock lock(*this);
    files.swap(built);
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    RenderLock lock(*this);
    files.swap(built);
    return;
  }

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    // Empty means getName() failed, not a file called "". Skip rather than list
    // it -- see the note on HalFile::getName and B-054. The hidden-file check
    // below tests [0] == '.', which an empty name passes straight through.
    if (fileNameBuffer[0] == '\0') continue;
    // showsHiddenEntries() is a switch that always returns false today
    // (FileBrowserActivity.h), deliberately kept as a switch rather than
    // `return false` so a future Mode enumerator fails to compile under
    // -Wswitch instead of silently falling through. Not a bug.
    // cppcheck-suppress knownConditionTrueFalse
    if ((!showsHiddenEntries() && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (file.isDirectory()) {
      built.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          built.emplace_back(filename);
        }
      } else if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                 FsHelpers::hasBmpExtension(filename)) {
        built.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(built);

  RenderLock lock(*this);
  files.swap(built);
  // Clamp in the same lock as the publish -- see FileManagerActivity.
  if (nav.selected >= static_cast<int>(files.size())) {
    nav.selected = files.empty() ? 0 : static_cast<int>(files.size()) - 1;
  }
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  // swallowUntilIdle() in ActivityManager covers the Confirm release from the
  // parent's menu press; no explicit latch needed here.

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    // The first screen build pulls the viewport to it (ListNav follow-on-build).
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
}

void FileBrowserActivity::activateIndex(const int index) {
  (void)index;  // base already synced nav.selected to the tapped row
  // Activation navigates or opens; a lingering flash would gray an unrelated
  // row on the next list.
  app.clearTapFlash();
  activateSelected();
}

void FileBrowserActivity::onRowLongPress(const int index) {
  (void)index;  // base already synced nav.selected to the pressed row
  app.clearTapFlash();
  activateSelected(/*forceDelete=*/true);
}

void FileBrowserActivity::activateSelected(const bool forceDelete, const bool fromButton) {
  if (fromButton && lockNextConfirmRelease) {
    lockNextConfirmRelease = false;
    return;
  }
  if (files.empty()) return;

  const std::string& entry = files[nav.selected];
  bool isDirectory = (entry.back() == '/');

  // Firmware picker: select file -> return path; navigate into directories normally.
  if (mode == Mode::PickFirmware && !isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  // Confirm's OWN press length (this runs on its release frame), or a touch
  // long-tap's contact time. The global timer measured the chord's first
  // button, so a Confirm tapped while a page button was still down opened
  // the delete prompt (docs/ux-navigation-audit-2026-09-02.md F10).
  if (mode == Mode::Books &&
      (forceDelete || mappedInput.getHeldTime(MappedInputManager::Button::Confirm) >= DELETE_HOLD_MS)) {
    // --- LONG PRESS ACTION: DELETE FILE OR DIRECTORY ---
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    const std::string fullPath = cleanBasePath + entry;

    std::string heading = tr(STR_DELETE) + std::string("? ");
    const char* options[] = {tr(STR_CANCEL), tr(STR_CONFIRM)};
    deletePopup.show(heading.c_str(), options, 2, 0, [this, fullPath](int idx) {
      // The confirmation popup acts on button press; if Confirm is still
      // held when we resume, swallow its release so it doesn't also open
      // the selection here.
      lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
      if (idx == 1) {
        LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
        if (FsOps::removeRecursiveWithCacheClear(fullPath, fileNameBuffer.get(), NAME_BUFFER_SIZE)) {
          LOG_DBG("FileBrowser", "Deleted successfully");
          loadFiles();
          if (files.empty()) {
            nav.selected = 0;
          } else if (nav.selected >= listCount()) {
            // Move selection to the new "last" item
            nav.selected = listCount() - 1;
          }
          nav.follow(listCount());

          requestUpdate(true);
        } else {
          LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
        }
      } else {
        LOG_DBG("FileBrowser", "Delete cancelled by user");
      }
    });
    deletePopup.setInfoLines({entry});
    requestUpdate();
    return;
  } else {
    // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
    // Do NOT mutate basepath in place: render() reads it on the other task.
    // The trailing slash only ever existed to build a child path, so build it
    // in a local and publish basepath under the lock in the directory case.
    std::string prefix = basepath;
    if (prefix.empty() || prefix.back() != '/') prefix += "/";

    if (isDirectory) {
      std::string next = prefix + entry.substr(0, entry.length() - 1);
      {
        RenderLock lock(*this);
        basepath.swap(next);
      }
      loadFiles();
      {
        RenderLock lock(*this);
        nav.reset();
      }
      requestUpdate();
    } else {
      // Push rather than replace so FileBrowserActivity stays on the stack;
      // Back from the reader pops back here with the selection intact.
      activityManager.pushActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, prefix + entry, false));
    }
  }
  return;
}

bool FileBrowserActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected(/*forceDelete=*/false, /*fromButton=*/true);
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Go up one directory, or go home if at root.
    if (basepath != "/") {
      const std::string oldPath = basepath;

      // As above: build the parent path in a local, publish under the lock.
      std::string parent = oldPath;
      parent.replace(parent.find_last_of('/'), std::string::npos, "");
      if (parent.empty()) parent = "/";
      {
        RenderLock lock(*this);
        basepath.swap(parent);
      }
      loadFiles();

      const auto pos = oldPath.find_last_of('/');
      const std::string dirName = oldPath.substr(pos + 1) + "/";
      {
        RenderLock lock(*this);
        nav.reset(static_cast<int>(findEntry(dirName)));
      }

      requestUpdate();
    } else if (mode == Mode::PickFirmware) {
      // Firmware picker at root: cancel back to caller instead of going home.
      ActivityResult res;
      res.isCancelled = true;
      setResult(std::move(res));
      finish();
    } else {
      onGoHome();
    }
    return true;
  }

  return false;
}

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

void FileBrowserActivity::describeRow(const int index, ListRow& row) const {
  row.title = getFileName(files[index]);
  row.icon = UITheme::getFileIcon(files[index]);
  row.value = getFileExtension(files[index]);
}

int FileBrowserActivity::pathReserved() const {
  return renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
}

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the header band, above the path line and the button hints.
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing + pathReserved()), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (files.empty()) {
    const char* emptyMsg = (mode == Mode::PickFirmware) ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND);
    drawEmptyMessage(screen, emptyMsg);
    return;
  }

  ListLook look;
  look.icons = true;
  drawRows(screen, look);
}

void FileBrowserActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  // Full path display
  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
  const int separatorY = pathY - metrics.verticalSpacing / 2;
  renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
  const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
  // Left-truncate so the deepest directory is always visible
  const char* pathStr = basepath.c_str();
  const char* pathDisplay = pathStr;
  char leftTruncBuf[256];
  if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
    const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
    const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
    const int available = pathMaxWidth - ellipsisWidth;
    // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
    const char* p = pathStr;
    while (*p) {
      if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
      ++p;
      while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
    }
    snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
    pathDisplay = leftTruncBuf;
  }
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);
}

void FileBrowserActivity::drawFooter() {
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && !files.empty() && files[nav.selected].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
