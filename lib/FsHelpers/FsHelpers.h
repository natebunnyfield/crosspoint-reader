#pragma once
#include <WString.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace FsHelpers {

std::string decodeUriEscapes(const std::string& path);

std::string normalisePath(const std::string& path);

// Numeric-aware, case-insensitive comparison ("2" < "10"). Returns true when str1 orders
// before str2. Same ordering sortFileList applies within the file/directory groups.
bool naturalLess(const std::string& str1, const std::string& str2);

void sortFileList(std::vector<std::string>& strs);

/**
 * Check if the given filename ends with the specified extension (case-insensitive).
 */
bool checkFileExtension(std::string_view fileName, const char* extension);
inline bool checkFileExtension(const String& fileName, const char* extension) {
  return checkFileExtension(std::string_view{fileName.c_str(), fileName.length()}, extension);
}

// Check for either .jpg or .jpeg extension (case-insensitive)
bool hasJpgExtension(std::string_view fileName);
inline bool hasJpgExtension(const String& fileName) {
  return hasJpgExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .png extension (case-insensitive)
bool hasPngExtension(std::string_view fileName);
inline bool hasPngExtension(const String& fileName) {
  return hasPngExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .bmp extension (case-insensitive)
bool hasBmpExtension(std::string_view fileName);

// Check for .gif extension (case-insensitive)
bool hasGifExtension(std::string_view fileName);
inline bool hasGifExtension(const String& fileName) {
  return hasGifExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .epub extension (case-insensitive)
bool hasEpubExtension(std::string_view fileName);
inline bool hasEpubExtension(const String& fileName) {
  return hasEpubExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for either .xtc or .xtch extension (case-insensitive)
bool hasXtcExtension(std::string_view fileName);

// Check for .txt extension (case-insensitive)
bool hasTxtExtension(std::string_view fileName);

// Plain, UNSTYLED text: .txt plus the config/data formats that are just text
// with no markup of their own -- json, log, csv, xml, yaml/yml, ini, cfg, conf.
//
// A NAMED LIST, not "anything that is not a known binary". Falling back to
// text for unknown extensions would happily open a .zip or a font file as a
// screenful of mojibake; a list can only be wrong by omission, which is a
// missing file rather than a garbage one. Add to it when a format turns up.
//
// These render in the EDITOR font (owner ruling 2026-08-11) -- they have no
// styling to justify a reading face, and json and log columns only line up in
// a mono.
bool hasPlainTextExtension(std::string_view fileName);
inline bool hasPlainTextExtension(const String& fileName) {
  return hasPlainTextExtension(std::string_view{fileName.c_str(), fileName.length()});
}
inline bool hasTxtExtension(const String& fileName) {
  return hasTxtExtension(std::string_view{fileName.c_str(), fileName.length()});
}

// Check for .md extension (case-insensitive)
bool hasMarkdownExtension(std::string_view fileName);

// Check for .css extension (case-insensitive)
bool hasCssExtension(std::string_view fileName);
inline bool hasCssExtension(const String& fileName) {
  return hasCssExtension(std::string_view{fileName.c_str(), fileName.length()});
}
std::string extractFolderPath(const std::string& filePath);

// Rejects an empty component, one containing '/' or '\', or the exact components
// "." and "..", so a single filename/folder-name argument can never be used to
// escape the directory it is placed into. Names like "volume..2.epub" or
// "notes...txt" that merely contain ".." are accepted.
bool isSafePathComponent(std::string_view name);
inline bool isSafePathComponent(const String& name) {
  return isSafePathComponent(std::string_view{name.c_str(), name.length()});
}

/**
 * True when `path` is `ancestor` itself or lies anywhere beneath it, compared
 * the way the card compares names -- UTF-8 decoded and case-folded with
 * SdFat's own long-name table, so "/CAFÉ/x" is inside "/Café" -- ignoring
 * trailing slashes. The guard before renaming a folder: SdFat will happily
 * re-parent a folder under its own descendant, which cuts the subtree off as
 * a loop.
 */
bool isSameOrInside(std::string_view path, std::string_view ancestor);

/** Whether two paths name the same entry on the card (same folding as above). */
bool isSameFatPath(std::string_view a, std::string_view b);

/**
 * Why a FAT or exFAT card cannot store `name` as one path component. SdFat
 * refuses these names when it parses the path (FatFileLFN.cpp parsePathName,
 * ExFatFile.cpp parsePathName) and reports only "open failed", so a refused
 * upload used to be blamed on a full disk or a missing folder (review
 * 2026-11-27, R-11 and R-12). The rules here are SdFat's own: the reserved
 * set is lfnReservedChar (FsStructs.h), the length is counted in UTF-16 units
 * after leading spaces and trailing dots and spaces are dropped, and the limit
 * is FAT_MAX_LFN_LENGTH / EXFAT_MAX_NAME_LENGTH.
 *
 * Ask AFTER the card refuses, to explain the refusal. The simulator and the
 * iOS app store files on a host file system that accepts most of these names,
 * so refusing them up front would take uploads away there.
 */
enum class FatNameProblem : uint8_t {
  None,
  Empty,         // nothing is left once the card drops spaces and dots
  ReservedChar,  // `reserved` is the first one
  TooLong,       // `length` is over FAT_MAX_NAME_LENGTH
  BadEncoding,   // not UTF-8
};
struct FatNameCheck {
  FatNameProblem problem = FatNameProblem::None;
  char reserved = '\0';
  size_t length = 0;  // UTF-16 units, the way the card counts
};
inline constexpr size_t FAT_MAX_NAME_LENGTH = 255;
FatNameCheck checkFatName(std::string_view name);
inline FatNameCheck checkFatName(const String& name) {
  return checkFatName(std::string_view{name.c_str(), name.length()});
}
/** One sentence a person can act on, naming the rule; empty for None. */
std::string describeFatNameProblem(const FatNameCheck& check);

/**
 * Sanitize a filename/path component for FAT32 in a caller-provided buffer.
 * Replaces invalid path characters, spaces, and control characters with '-'.
 */
void sanitizePathComponentForFat32(const char* input, char* output, size_t maxLen);

}  // namespace FsHelpers
