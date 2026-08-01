#pragma once

#include <Epub.h>
#include <Logging.h>

#include "ProgressFile.h"

namespace EpubReaderUtils {

// Persists reader progress for an EPUB to its cache directory. Returns true on success.
//
// progress.bin has no version byte; its LENGTH is the discriminator. 4 bytes is
// the oldest form (spine + page), 6 adds the chapter page count, and 8 adds the
// paragraph index the reader was on. The paragraph is what lets a font or size
// change land back on the same TEXT: a page index means nothing once the chapter
// re-paginates, and the Text Settings paths tear the reader down, so an in-memory
// anchor cannot survive them. Older files simply read short and degrade to the
// proportional remap.
inline bool saveProgress(const Epub& epub, int spineIndex, int pageNumber, int pageCount,
                         int paragraphIndex = -1) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  const bool hasParagraph = paragraphIndex >= 0 && paragraphIndex <= 0xFFFF;
  uint8_t data[8];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = pageNumber & 0xFF;
  data[3] = (pageNumber >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  if (hasParagraph) {
    data[6] = paragraphIndex & 0xFF;
    data[7] = (paragraphIndex >> 8) & 0xFF;
  }
  if (!ProgressFile::writeAtomic(epub.getCachePath(), data, hasParagraph ? 8 : 6)) {
    return false;
  }
  LOG_DBG("ERS", "Progress saved: spine=%d page=%d para=%d", spineIndex, pageNumber, paragraphIndex);
  return true;
}

}  // namespace EpubReaderUtils
