#include "BookNotes.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

namespace booknotes {
namespace {
// version(1) + bookMask(4) + layoutMask(4) + fingerprint(4) + three uint16 = 19.
constexpr uint8_t NOTES_FILE_VERSION = 1;
constexpr const char* NOTES_FILE = "/notes.bin";
}  // namespace

// Only the two methods that touch storage live here; everything else is inline
// in the header, for the reason its comment gives.

void Notes::openBook(const std::string& cacheDir) {
  // Clear FIRST and unconditionally: a second book that has no notes.bin must
  // show its own emptiness, not the previous book's notes.
  clear();
  cachePath = cacheDir;
  if (cacheDir.empty()) return;

  HalFile file;
  if (!Storage.openFileForRead("BKN", cacheDir + NOTES_FILE, file)) return;
  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version != NOTES_FILE_VERSION) {
    file.close();
    return;
  }
  serialization::readPod(file, bookMask);
  serialization::readPod(file, layoutMask);
  serialization::readPod(file, fingerprint);
  serialization::readPod(file, detail.narrowestCharsPerLine);
  serialization::readPod(file, detail.imagesDropped);
  serialization::readPod(file, detail.cssRulesDropped);
  file.close();

  // Masks written by a newer firmware may carry bits this build has no note
  // for; drop them rather than count them into a total the screen cannot show.
  const uint32_t known =
      (static_cast<uint8_t>(Note::_COUNT) >= 32) ? 0xFFFFFFFFu : ((1u << static_cast<uint8_t>(Note::_COUNT)) - 1u);
  bookMask &= known;
  layoutMask &= known;
  LOG_DBG("BKN", "Loaded book notes: book 0x%08x, layout 0x%08x", bookMask, layoutMask);
}

void Notes::flush() {
  if (!dirty || cachePath.empty()) return;
  HalFile file;
  if (!Storage.openFileForWrite("BKN", cachePath + NOTES_FILE, file)) {
    dirty = false;  // do not retry on every page turn
    return;
  }
  serialization::writePod(file, NOTES_FILE_VERSION);
  serialization::writePod(file, bookMask);
  serialization::writePod(file, layoutMask);
  serialization::writePod(file, fingerprint);
  serialization::writePod(file, detail.narrowestCharsPerLine);
  serialization::writePod(file, detail.imagesDropped);
  serialization::writePod(file, detail.cssRulesDropped);
  file.close();
  dirty = false;
  LOG_DBG("BKN", "Wrote book notes: book 0x%08x, layout 0x%08x (chars/line %u, images %u, css rules %u)", bookMask,
          layoutMask, detail.narrowestCharsPerLine, detail.imagesDropped, detail.cssRulesDropped);
}

}  // namespace booknotes
