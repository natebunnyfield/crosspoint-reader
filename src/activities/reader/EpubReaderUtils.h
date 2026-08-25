#pragma once

#include <Epub.h>
#include <Logging.h>

#include "ProgressFile.h"

namespace EpubReaderUtils {

// Persists reader progress for an EPUB to its cache directory. Returns true on success.
//
// progress.bin has no version byte; its LENGTH is the discriminator. 4 bytes is
// the oldest form (spine + page), 6 adds the chapter page count, 8 adds the
// paragraph index the reader was on, and 12 adds the word anchor (the Section
// v35 chapter-global source byte offset of the page's first word, uint32 LE).
// The anchors are what let a font or size change land back on the same TEXT: a
// page index means nothing once the chapter re-paginates, and the Reader Font
// paths tear the reader down, so an in-memory anchor cannot survive them. The
// word anchor is exact where the paragraph is not (chapters without <p>). Older
// files simply read short and degrade, ultimately to the proportional remap.
// A word anchor is only written when the paragraph is also present, so every
// length up the ladder implies the fields below it.
inline bool saveProgress(const Epub& epub, int spineIndex, int pageNumber, int pageCount, int paragraphIndex = -1,
                         int64_t wordAnchor = -1) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  const bool hasParagraph = paragraphIndex >= 0 && paragraphIndex <= 0xFFFF;
  const bool hasWordAnchor = hasParagraph && wordAnchor >= 0 && wordAnchor <= 0xFFFFFFFFll;
  uint8_t data[12];
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
  if (hasWordAnchor) {
    const uint32_t a = static_cast<uint32_t>(wordAnchor);
    data[8] = a & 0xFF;
    data[9] = (a >> 8) & 0xFF;
    data[10] = (a >> 16) & 0xFF;
    data[11] = (a >> 24) & 0xFF;
  }
  if (!ProgressFile::writeAtomic(epub.getCachePath(), data, hasWordAnchor ? 12 : (hasParagraph ? 8 : 6))) {
    return false;
  }
  LOG_DBG("ERS", "Progress saved: spine=%d page=%d para=%d word=%lld", spineIndex, pageNumber, paragraphIndex,
          static_cast<long long>(wordAnchor));
  return true;
}

}  // namespace EpubReaderUtils
