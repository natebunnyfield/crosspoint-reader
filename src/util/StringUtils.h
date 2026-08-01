#pragma once

#include <cctype>
#include <string>

namespace StringUtils {

/**
 * Case-insensitive ASCII strcmp. Returns <0, 0, or >0 like strcmp, comparing
 * each byte by its lowercased value.
 *
 * Used wherever data is sorted case-insensitively: StarDict indexes (including
 * wiktionary-derived sources).
 * Plain strcmp would land a binary search on the wrong page for any word whose
 * alphabetic neighbourhood contains mixed-case boundaries.
 *
 * Inline (header) because it is called per comparison in hot loops
 * step; a cross-TU call here would defeat inlining on a hot path.
 */
inline int asciiCaseCmp(const char* a, const char* b) {
  while (*a && *b) {
    int diff = std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
    if (diff != 0) return diff;
    ++a;
    ++b;
  }
  return std::tolower(static_cast<unsigned char>(*a)) - std::tolower(static_cast<unsigned char>(*b));
}

/**
 * Sanitize a string for use as a filename.
 * Replaces invalid characters with underscores, trims spaces/dots,
 * and limits length to maxBytes bytes.
 */
std::string sanitizeFilename(const std::string& name, size_t maxBytes = 100);

}  // namespace StringUtils
