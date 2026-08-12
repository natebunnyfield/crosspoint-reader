#include "FileManagerActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Xtc.h>

#include <cstring>

#include "MappedInputManager.h"
#include "activities/util/TextEntryFactory.h"
#include "activities/util/TextViewerActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/FsOps.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
constexpr size_t MAX_RENAME_LEN = 100;

// Rows show the filename as ONE string (ruling in docs/manage-files.md) —
// no split name/extension columns like the book browser uses.
std::string displayName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
  }
  return filename;
}

// Make a metadata-derived name safe for a FAT filename: strip control chars,
// replace reserved characters with spaces, collapse runs, trim. Multi-byte
// UTF-8 sequences pass through untouched (all bytes >= 0x80).
std::string sanitizeFileName(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (const char c : in) {
    const auto uc = static_cast<unsigned char>(c);
    if (uc < 0x20) continue;
    switch (c) {
      case '/':
      case '\\':
      case ':':
      case '*':
      case '?':
      case '"':
      case '<':
      case '>':
      case '|':
        out += ' ';
        break;
      default:
        out += c;
    }
  }
  std::string collapsed;
  collapsed.reserve(out.size());
  for (const char c : out) {
    if (c == ' ' && (collapsed.empty() || collapsed.back() == ' ')) continue;
    collapsed += c;
  }
  while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
  return collapsed;
}
}  // namespace

void FileManagerActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileManager", "fileNameBuffer not allocated");
    root.close();
    return;
  }

  // Everything is listed — hidden files included, no filtering by type
  // (ruling in docs/manage-files.md).
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
      continue;
    }
    if (file.isDirectory()) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      files.emplace_back(fileNameBuffer.get());
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
}

size_t FileManagerActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}

std::string FileManagerActivity::fullPathOf(const std::string& entry) const {
  std::string cleanBase = basepath;
  if (cleanBase.back() != '/') cleanBase += '/';
  if (entry.back() == '/') {
    return cleanBase + entry.substr(0, entry.length() - 1);
  }
  return cleanBase + entry;
}

int FileManagerActivity::statusLineReserved() const {
  if (moveSourcePath.empty()) return 0;
  return renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
}

void FileManagerActivity::onEnter() {
  Activity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileManager", "malloc failed for name buffer");
    return;
  }

  selectorIndex = 0;
  // Entered from the home menu with Confirm still held: swallow that release
  // so the first entry doesn't self-activate.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  loadFiles();
  requestUpdate();
}

void FileManagerActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
}

void FileManagerActivity::openActionMenu() {
  menuActions.clear();
  const char* options[10];
  int count = 0;
  const bool hasEntry = !files.empty();

  if (!moveSourcePath.empty()) {
    options[count++] = tr(STR_MOVE_HERE);
    menuActions.push_back(MenuAction::MoveHere);
  }
  if (hasEntry) {
    const std::string& entry = files[selectorIndex];
    // View leads for files (any type — the viewer shows anything as text).
    if (entry.back() != '/') {
      options[count++] = tr(STR_VIEW);
      menuActions.push_back(MenuAction::View);
      // Edit opens the note editor over the BLE keyboard. Offered for text-ish
      // files only: the editor loads the whole file into an 8 KB buffer, so
      // pointing it at an epub would refuse on open and read as a broken menu.
      const size_t dot = entry.rfind('.');
      const std::string ext = dot == std::string::npos ? "" : entry.substr(dot);
      if (ext == ".md" || ext == ".txt") {
        options[count++] = tr(STR_EDIT);
        menuActions.push_back(MenuAction::Edit);
      }
    }
    options[count++] = tr(STR_RENAME);
    menuActions.push_back(MenuAction::Rename);
    // Rename using metadata ("Title - Author.ext") — only offered when the
    // book's embedded metadata actually yields a usable title (ruling
    // 2026-08-05: never show it just to error later).
    if (entry.back() != '/') {
      std::string title, author;
      if (probeBookMetadata(entry, title, author)) {
        options[count++] = tr(STR_NORMALIZE_NAME);
        menuActions.push_back(MenuAction::Normalize);
      }
      options[count++] = tr(STR_DUPLICATE);
      menuActions.push_back(MenuAction::Duplicate);
      // Tag for the host-side summarization pipeline (EPUB only — cheap
      // extension gate, no metadata probe needed for a marker toggle).
      if (FsHelpers::hasEpubExtension(entry)) {
        const std::string marker = FsOps::summarizeMarkerFor(fullPathOf(entry));
        options[count++] = Storage.exists(marker.c_str()) ? tr(STR_DONT_SUMMARIZE) : tr(STR_SUMMARIZE);
        menuActions.push_back(MenuAction::Summarize);
      }
    }
    options[count++] = tr(STR_MOVE);
    menuActions.push_back(MenuAction::Move);
    options[count++] = tr(STR_DELETE);
    menuActions.push_back(MenuAction::Delete);
  }
  if (count == 0) {
    return;
  }

  std::string title;
  if (hasEntry) {
    title = files[selectorIndex];
    if (title.back() == '/') title.pop_back();
  } else {
    title = (basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1);
  }

  popup.show(title.c_str(), options, count, 0, [this](const int index) {
    if (index >= 0 && index < static_cast<int>(menuActions.size())) {
      runMenuAction(menuActions[index]);
    }
  });
  if (hasEntry) popup.setInfoLines(buildInfoLines(files[selectorIndex]));
  requestUpdate();
}

// Small-font lines under the action-menu title: size + FAT timestamps for
// files, entry count for folders. Failures just omit lines — info is best
// effort, never an error surface.
std::vector<std::string> FileManagerActivity::buildInfoLines(const std::string& entry) {
  std::vector<std::string> lines;
  lines.reserve(3);
  const std::string full = fullPathOf(entry);
  char buf[64];

  if (entry.back() == '/') {
    auto dir = Storage.open(full.c_str());
    if (dir && dir.isDirectory() && fileNameBuffer) {
      int n = 0;
      dir.rewindDirectory();
      for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
        f.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
        if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) continue;
        n++;
      }
      snprintf(buf, sizeof(buf), "%d%s", n, tr(STR_ITEMS_SUFFIX));
      lines.emplace_back(buf);
    }
    return lines;
  }

  HalFile file;
  if (!Storage.openFileForRead("FileManager", full.c_str(), file)) return lines;

  const uint64_t size = file.fileSize64();
  if (size < 1024) {
    snprintf(buf, sizeof(buf), "%u B", static_cast<unsigned>(size));
  } else if (size < 1024ull * 1024) {
    snprintf(buf, sizeof(buf), "%u.%u KB", static_cast<unsigned>(size / 1024),
             static_cast<unsigned>(size % 1024 * 10 / 1024));
  } else {
    snprintf(buf, sizeof(buf), "%u.%u MB", static_cast<unsigned>(size / (1024 * 1024)),
             static_cast<unsigned>(size % (1024 * 1024) * 10 / (1024 * 1024)));
  }
  lines.emplace_back(buf);

  const auto fatLine = [&buf](const char* prefix, const uint16_t d, const uint16_t t) {
    snprintf(buf, sizeof(buf), "%s%04d-%02d-%02d %02d:%02d", prefix, 1980 + (d >> 9), (d >> 5) & 0xF, d & 0x1F, t >> 11,
             (t >> 5) & 0x3F);
    return std::string(buf);
  };
  uint16_t d = 0, t = 0;
  if (file.getCreateDateTime(&d, &t)) lines.push_back(fatLine(tr(STR_CREATED_PREFIX), d, t));
  if (file.getModifyDateTime(&d, &t)) lines.push_back(fatLine(tr(STR_MODIFIED_PREFIX), d, t));
  return lines;
}

void FileManagerActivity::runMenuAction(const MenuAction action) {
  switch (action) {
    case MenuAction::Edit:
      // Was an early return above this switch, which left 'Edit' unhandled here
      // and -Wswitch firing -- so the compiler stopped checking the rest of the
      // enum for this function. Same behavior, in the place that gets checked.
      if (!files.empty()) activityManager.goToNoteEditor(fullPathOf(files[selectorIndex]), basepath);
      break;
    case MenuAction::MoveHere:
      performMoveHere();
      break;
    case MenuAction::View:
      if (!files.empty()) viewFile(files[selectorIndex]);
      break;
    case MenuAction::Rename:
      if (!files.empty()) startRename(files[selectorIndex]);
      break;
    case MenuAction::Normalize:
      if (!files.empty()) startNormalize(files[selectorIndex]);
      break;
    case MenuAction::Duplicate:
      if (!files.empty()) duplicateFile(files[selectorIndex]);
      break;
    case MenuAction::Summarize:
      if (!files.empty()) toggleSummarize(files[selectorIndex]);
      break;
    case MenuAction::Move:
      if (!files.empty()) armMove(files[selectorIndex]);
      break;
    case MenuAction::Delete:
      if (!files.empty()) confirmDelete(files[selectorIndex]);
      break;
  }
}

void FileManagerActivity::viewFile(const std::string& entry) {
  if (entry.empty() || entry.back() == '/') return;
  startActivityForResult(std::make_unique<TextViewerActivity>(renderer, mappedInput, fullPathOf(entry), entry),
                         [this](const ActivityResult&) {
                           // swallowUntilIdle() covers press/release edges. Keep lockLongPressBack:
                           // it guards isPressed(Back) long-press, which swallow does not suppress.
                           lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);
                         });
}

void FileManagerActivity::startRename(const std::string& entry, const std::string& seedName) {
  const bool isDirectory = entry.back() == '/';
  const std::string oldName = isDirectory ? entry.substr(0, entry.length() - 1) : entry;
  const std::string oldFull = fullPathOf(entry);

  startActivityForResult(makeTextEntryActivity(renderer, mappedInput, tr(STR_NEW_NAME),
                                               seedName.empty() ? oldName : seedName, MAX_RENAME_LEN),
                         [this, oldFull, oldName, isDirectory](const ActivityResult& res) {
                           // swallowUntilIdle() covers press/release edges. Keep lockLongPressBack:
                           // isPressed(Back) long-press is not suppressed by swallow.
                           lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);
                           if (res.isCancelled) return;
                           const auto* kb = std::get_if<KeyboardResult>(&res.data);
                           if (!kb) return;
                           const std::string& newName = kb->text;
                           if (newName.empty() || newName == oldName) return;
                           if (newName.find('/') != std::string::npos) {
                             showError(tr(STR_RENAME_FAILED));
                             return;
                           }
                           std::string cleanBase = basepath;
                           if (cleanBase.back() != '/') cleanBase += '/';
                           const std::string newFull = cleanBase + newName;
                           if (Storage.exists(newFull.c_str())) {
                             showError(tr(STR_NAME_IN_USE));
                             return;
                           }
                           if (!Storage.rename(oldFull.c_str(), newFull.c_str())) {
                             showError(tr(STR_RENAME_FAILED));
                             return;
                           }
                           if (isDirectory) {
                             FsOps::migrateBookRefsRecursive(oldFull, newFull, fileNameBuffer.get(), NAME_BUFFER_SIZE);
                           } else {
                             FsOps::migrateBookRefs(oldFull, newFull);
                           }
                           if (moveSourcePath == oldFull) {
                             moveSourcePath = newFull;
                             moveSourceName = newName;
                           }
                           loadFiles();
                           selectorIndex = findEntry(isDirectory ? newName + "/" : newName);
                           requestUpdate(true);
                         });
}

// True when `entry` is a book file whose embedded metadata yields a usable
// (sanitized, non-empty) title. Outputs are sanitized for FAT names.
bool FileManagerActivity::probeBookMetadata(const std::string& entry, std::string& title, std::string& author) {
  title.clear();
  author.clear();
  const std::string fullPath = fullPathOf(entry);
  if (FsHelpers::hasEpubExtension(entry)) {
    Epub epub(fullPath, "/.crosspoint");
    if (epub.load(false, true)) {
      title = epub.getTitle();
      author = epub.getAuthor();
    }
  } else if (FsHelpers::hasXtcExtension(entry)) {
    Xtc xtc(fullPath, "/.crosspoint");
    if (xtc.load()) {
      title = xtc.getTitle();
      author = xtc.getAuthor();
    }
  } else {
    return false;
  }
  title = sanitizeFileName(title);
  author = sanitizeFileName(author);
  return !title.empty();
}

void FileManagerActivity::startNormalize(const std::string& entry) {
  std::string title;
  std::string author;
  if (!probeBookMetadata(entry, title, author)) {
    showError(tr(STR_NO_METADATA));
    return;
  }

  // Card naming format: "Title - Author.ext" (docs/manage-files.md).
  const std::string ext = entry.substr(entry.rfind('.'));
  std::string proposed = author.empty() ? title + ext : title + " - " + author + ext;
  LOG_DBG("FileManager", "normalize '%s' -> '%s'", entry.c_str(), proposed.c_str());
  startRename(entry, proposed);
}

// Toggle the summarization marker (`<cache dir>/summarize.tag`, content = the
// book's full card path). The host pipeline scans these over WebDAV, generates
// the summary EPUB, and deletes the marker (docs/manage-files.md).
void FileManagerActivity::toggleSummarize(const std::string& entry) {
  const std::string full = fullPathOf(entry);
  const std::string marker = FsOps::summarizeMarkerFor(full);
  if (marker.empty()) return;

  if (Storage.exists(marker.c_str())) {
    if (!Storage.remove(marker.c_str())) {
      showError(tr(STR_SUMMARIZE_FAILED));
      return;
    }
  } else {
    // Cache dir may not exist yet (book never opened); mkdir fails on an
    // existing dir, so guard with exists (same idiom as Epub::setupCacheDir).
    const std::string cacheDir = FsOps::bookCacheDirFor(full);
    if (!Storage.exists(cacheDir.c_str()) && !Storage.mkdir(cacheDir.c_str())) {
      showError(tr(STR_SUMMARIZE_FAILED));
      return;
    }
    HalFile file;
    if (!Storage.openFileForWrite("FileManager", marker.c_str(), file)) {
      showError(tr(STR_SUMMARIZE_FAILED));
      return;
    }
    file.write(full.c_str(), full.size());
    file.write('\n');
  }
  requestUpdate();
}

// Streamed copy to "<stem> copy<n><ext>" in the same directory. Files only
// (folder duplicate is a tracked TODO in docs/manage-files.md).
void FileManagerActivity::duplicateFile(const std::string& entry) {
  const std::string src = fullPathOf(entry);
  const size_t dot = entry.rfind('.');
  const std::string stem = dot == std::string::npos ? entry : entry.substr(0, dot);
  const std::string ext = dot == std::string::npos ? "" : entry.substr(dot);

  std::string newName;
  for (int i = 1; i <= 99; i++) {
    std::string candidate = i == 1 ? stem + " copy" + ext : stem + " copy " + std::to_string(i) + ext;
    if (!Storage.exists(fullPathOf(candidate).c_str())) {
      newName = std::move(candidate);
      break;
    }
  }
  if (newName.empty()) {
    showError(tr(STR_DUPLICATE_FAILED));
    return;
  }
  const std::string dst = fullPathOf(newName);

  HalFile in;
  HalFile out;
  if (!Storage.openFileForRead("FileManager", src.c_str(), in) ||
      !Storage.openFileForWrite("FileManager", dst.c_str(), out)) {
    LOG_ERR("FileManager", "duplicate open failed '%s'", src.c_str());
    showError(tr(STR_DUPLICATE_FAILED));
    return;
  }

  // 8KB transient copy buffer; freed on return. Stack is too small (<256B
  // local limit) and the framebuffer can't be borrowed mid-activity.
  constexpr size_t COPY_BUF = 8192;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(COPY_BUF);
  if (!buffer) {
    LOG_ERR("FileManager", "OOM: %u byte copy buffer", static_cast<unsigned>(COPY_BUF));
    showError(tr(STR_DUPLICATE_FAILED));
    return;
  }

  bool ok = true;
  for (;;) {
    const int n = in.read(buffer.get(), COPY_BUF);
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) break;
    if (out.write(buffer.get(), n) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
  }
  out.close();  // close before a possible Storage.remove on the same path
  if (!ok) {
    LOG_ERR("FileManager", "duplicate copy failed '%s'", dst.c_str());
    Storage.remove(dst.c_str());  // never leave a partial copy behind
    showError(tr(STR_DUPLICATE_FAILED));
    return;
  }

  loadFiles();
  selectorIndex = findEntry(newName);
  requestUpdate();
}

void FileManagerActivity::armMove(const std::string& entry) {
  LOG_DBG("FileManager", "armMove entry='%s' base='%s'", entry.c_str(), basepath.c_str());
  moveSourceIsDir = entry.back() == '/';
  moveSourceName = moveSourceIsDir ? entry.substr(0, entry.length() - 1) : entry;
  moveSourcePath = fullPathOf(entry);
  requestUpdate();
}

void FileManagerActivity::performMoveHere() {
  LOG_DBG("FileManager", "performMoveHere src='%s' base='%s'", moveSourcePath.c_str(), basepath.c_str());
  if (moveSourcePath.empty()) return;

  std::string cleanBase = basepath;
  if (cleanBase.back() != '/') cleanBase += '/';
  const std::string dest = cleanBase + moveSourceName;

  if (dest == moveSourcePath) {
    // Same directory: nothing to move, just disarm.
    moveSourcePath.clear();
    moveSourceName.clear();
    requestUpdate();
    return;
  }
  if (moveSourceIsDir && cleanBase.compare(0, moveSourcePath.length() + 1, moveSourcePath + "/") == 0) {
    showError(tr(STR_CANT_MOVE_INTO_ITSELF));
    return;
  }
  if (Storage.exists(dest.c_str())) {
    showError(tr(STR_NAME_IN_USE));
    return;
  }
  if (!Storage.rename(moveSourcePath.c_str(), dest.c_str())) {
    showError(tr(STR_MOVE_FAILED));
    return;
  }

  if (moveSourceIsDir) {
    FsOps::migrateBookRefsRecursive(moveSourcePath, dest, fileNameBuffer.get(), NAME_BUFFER_SIZE);
  } else {
    FsOps::migrateBookRefs(moveSourcePath, dest);
  }

  const std::string selectName = moveSourceIsDir ? moveSourceName + "/" : moveSourceName;
  moveSourcePath.clear();
  moveSourceName.clear();
  loadFiles();
  selectorIndex = findEntry(selectName);
  requestUpdate(true);
}

void FileManagerActivity::confirmDelete(const std::string& entry) {
  const std::string fullPath = fullPathOf(entry);

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  const char* options[] = {tr(STR_CANCEL), tr(STR_CONFIRM)};
  popup.show(heading.c_str(), options, 2, 0, [this, fullPath](int idx) {
    // The confirmation popup acts on button press; if that button is still
    // held when we resume, swallow its release so it doesn't also act here.
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);
    lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
    if (idx != 1) return;

    if (!FsOps::removeRecursiveWithCacheClear(fullPath, fileNameBuffer.get(), NAME_BUFFER_SIZE)) {
      LOG_ERR("FileManager", "Failed to delete: %s", fullPath.c_str());
    }
    // A move source that was deleted (or lived inside a deleted folder) can no
    // longer be pasted.
    if (!moveSourcePath.empty() &&
        (moveSourcePath == fullPath || moveSourcePath.compare(0, fullPath.length() + 1, fullPath + "/") == 0)) {
      moveSourcePath.clear();
      moveSourceName.clear();
    }
    loadFiles();
    if (files.empty()) {
      selectorIndex = 0;
    } else if (selectorIndex >= files.size()) {
      selectorIndex = files.size() - 1;
    }
    requestUpdate(true);
  });
  popup.setInfoLines({entry});
  requestUpdate();
}

void FileManagerActivity::showError(const char* message) {
  const char* options[1] = {tr(STR_OK_BUTTON)};
  popup.show(message, options, 1, 0, [](int) {});
  requestUpdate();
}

void FileManagerActivity::loop() {
  if (popup.isActive()) {
    popup.handleInput(mappedInput, [this] { requestUpdate(); });
    if (!popup.isActive()) {
      // The popup closed on a button press this frame; list actions below are
      // release-driven, so swallow the matching release.
      if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) lockNextConfirmRelease = true;
      if (mappedInput.isPressed(MappedInputManager::Button::Back)) lockNextBackRelease = true;
    }
    return;
  }

  // Long press BACK (1s+) jumps to the SD root.
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/" && !lockLongPressBack) {
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  // Use a level read (!isPressed) instead of wasReleased: the edge read has a
  // side effect of clearing the swallowUntilIdle() latch, which then lets the
  // short-press wasReleased check below fire in the same frame and cause a
  // second navigation (the B-018 double-consume regression that re-surfaced
  // when the central swallow replaced backPressSeen — see BUGS.md).
  if (lockLongPressBack && !mappedInput.isPressed(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  // Long press CONFIRM (1s+): action menu, fired while the button is still
  // held. Release-driven detection is not enough here: the simulator's
  // getHeldTime() reads 0 on the release frame, and acting mid-hold gives
  // feedback the moment the threshold is crossed. confirmPressSeen gates out
  // presses carried in from the home menu or a child activity. The eventual
  // release lands in the popup, which only acts on press edges.
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) confirmPressSeen = true;
  if (confirmPressSeen && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= GO_HOME_MS) {
    confirmPressSeen = false;
    if (!moveSourcePath.empty()) {
      performMoveHere();
    } else {
      openActionMenu();
    }
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing + statusLineReserved();
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;

  auto activateSelected = [this] {
    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
      return;
    }
    if (files.empty()) {
      // Empty folder: while armed, paste directly (consistent with non-empty case).
      if (!moveSourcePath.empty()) performMoveHere();
      return;
    }

    const std::string& entry = files[selectorIndex];
    const bool isDirectory = entry.back() == '/';

    // Long press: while armed, paste directly; otherwise open action menu.
    // Normally fired mid-hold in loop(); this release path still catches a hold
    // that completed inside a render stall.
    if (mappedInput.getHeldTime() >= GO_HOME_MS) {
      if (!moveSourcePath.empty()) {
        performMoveHere();
      } else {
        openActionMenu();
      }
      return;
    }

    if (!isDirectory) {
      viewFile(entry);
      return;
    }

    if (basepath.back() != '/') basepath += '/';
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
  };

  int touchSel = static_cast<int>(selectorIndex);
  const auto listTouch = handleListTouch(touchSel, static_cast<int>(files.size()), contentTop, contentHeight, false);
  if (listTouch != ListTouchResult::None) {
    selectorIndex = static_cast<size_t>(touchSel);
    if (listTouch == ListTouchResult::Activated) activateSelected();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmPressSeen = false;
    activateSelected();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // TextViewerActivity now exits on press; swallowUntilIdle() covers the
    // trailing release. lockNextBackRelease still guards popup-close leaks
    // (popup closes on press within this activity, release stays in this frame).
    if (lockNextBackRelease) {
      lockNextBackRelease = false;
      return;
    }
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else {
        onGoHome();
      }
    }
    return;
  }

  int listSize = static_cast<int>(files.size());
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void FileManagerActivity::render(RenderLock&&) {
  // The popup overlays whatever the previous frame drew; no base repaint needed
  // (same idiom as SettingsActivity / ClearCacheActivity).
  if (popup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const std::string folderName =
      (basepath == "/") ? std::string(tr(STR_MANAGE_FILES)) : basepath.substr(basepath.rfind('/') + 1);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing + statusLineReserved();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;

  if (files.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_FILES_FOUND));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
        [this](int index) { return displayName(files[index]); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(files[index]); });
  }

  const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
  const int separatorY = pathY - metrics.verticalSpacing / 2;

  // Armed move: status line above the path separator.
  if (!moveSourcePath.empty()) {
    const int statusY = separatorY - metrics.verticalSpacing / 2 - pathLineHeight;
    const std::string status =
        renderer.truncatedText(SMALL_FONT_ID, (std::string(tr(STR_MOVING_PREFIX)) + moveSourceName).c_str(),
                               pageWidth - metrics.contentSidePadding * 2);
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, statusY, status.c_str());
  }

  // Full path display
  {
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

  // Button hints
  const char* backLabel = (basepath == "/") ? tr(STR_HOME) : tr(STR_BACK);
  // Short press acts, long press = action menu — say both, or the menu is
  // undiscoverable (user request, 2026-08-04; View-on-press ruling 2026-08-05).
  char confirmBuf[48];
  const char* confirmLabel;
  if (files.empty()) {
    confirmLabel = moveSourcePath.empty() ? "" : tr(STR_MOVE_HERE);
  } else if (files[selectorIndex].back() == '/') {
    snprintf(confirmBuf, sizeof(confirmBuf), "%s/%s", tr(STR_OPEN),
             moveSourcePath.empty() ? tr(STR_MENU) : tr(STR_MOVE_HERE));
    confirmLabel = confirmBuf;
  } else {
    snprintf(confirmBuf, sizeof(confirmBuf), "%s/%s", tr(STR_VIEW),
             moveSourcePath.empty() ? tr(STR_MENU) : tr(STR_MOVE_HERE));
    confirmLabel = confirmBuf;
  }
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
