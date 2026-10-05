#include "FsHelpers.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

namespace FsHelpers {

namespace {
bool isHexDigit(const char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

uint8_t hexValue(const char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
  return static_cast<uint8_t>(10 + (c - 'A'));
}
}  // namespace

std::string decodeUriEscapes(const std::string& path) {
  std::string decoded;
  decoded.reserve(path.size());

  for (size_t i = 0; i < path.size(); i++) {
    if (path[i] == '%' && i + 2 < path.size() && isHexDigit(path[i + 1]) && isHexDigit(path[i + 2])) {
      const uint8_t value = static_cast<uint8_t>((hexValue(path[i + 1]) << 4) | hexValue(path[i + 2]));
      decoded += static_cast<char>(value);
      i += 2;
      continue;
    }

    decoded += path[i];
  }

  return decoded;
}

std::string normalisePath(const std::string& path) {
  std::vector<std::string_view> components;
  components.reserve(8);  // Eight nested folders is more than we might expect

  size_t start = 0;
  for (size_t i = 0; i <= path.length(); ++i) {
    if (i == path.length() || path[i] == '/') {
      if (i > start) {
        std::string_view component(path.data() + start, i - start);
        if (component == "..") {
          if (!components.empty()) {
            components.pop_back();
          }
        } else if (component != ".") {
          // "." names the folder it already is in, so it must be DROPPED, not
          // kept: the result of this function is compared with == to decide
          // whether a TOC entry reaches a spine item, and "./Text/ch1.xhtml"
          // has to come out equal to "Text/ch1.xhtml" or the whole contents of
          // such a book resolves to nothing. Dropped before ".." is applied, so
          // "a/./b/../c" still collapses to "a/c".
          components.push_back(component);
        }
      }
      start = i + 1;
    }
  }

  if (components.empty()) {
    return "";
  }

  size_t total_len = 0;
  for (const auto& c : components) {
    total_len += c.length() + 1;
  }

  std::string result;
  result.reserve(total_len - 1);

  for (size_t i = 0; i < components.size(); ++i) {
    if (i > 0) {
      result += '/';
    }
    result.append(components[i].data(), components[i].length());
  }

  return result;
}

bool naturalLess(const std::string& str1, const std::string& str2) {
  // Naive natural sort: numeric-aware, case-insensitive
  const char* s1 = str1.c_str();
  const char* s2 = str2.c_str();

  // ctype functions require unsigned char values: passing a negative char (UTF-8
  // bytes above 0x7f with signed char) is undefined behavior
  const auto isDigit = [](const char c) { return isdigit(static_cast<unsigned char>(c)) != 0; };

  // Iterate while both strings have characters
  while (*s1 && *s2) {
    // Check if both are at the start of a number
    if (isDigit(*s1) && isDigit(*s2)) {
      // Skip leading zeros and track them
      while (*s1 == '0') s1++;
      while (*s2 == '0') s2++;

      // Count digits to compare lengths first
      int len1 = 0, len2 = 0;
      while (isDigit(s1[len1])) len1++;
      while (isDigit(s2[len2])) len2++;

      // Different length so return smaller integer value
      if (len1 != len2) return len1 < len2;

      // Same length so compare digit by digit
      for (int i = 0; i < len1; i++) {
        if (s1[i] != s2[i]) return s1[i] < s2[i];
      }

      // Numbers equal so advance pointers
      s1 += len1;
      s2 += len2;
    } else {
      // Regular case-insensitive character comparison
      const int c1 = tolower(static_cast<unsigned char>(*s1));
      const int c2 = tolower(static_cast<unsigned char>(*s2));
      if (c1 != c2) return c1 < c2;
      s1++;
      s2++;
    }
  }

  // One string is prefix of other
  return *s1 == '\0' && *s2 != '\0';
}

void sortFileList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    // Directories first
    bool isDir1 = str1.back() == '/';
    bool isDir2 = str2.back() == '/';
    if (isDir1 != isDir2) return isDir1;

    return naturalLess(str1, str2);
  });
}

bool checkFileExtension(std::string_view fileName, const char* extension) {
  const size_t extLen = strlen(extension);
  if (fileName.length() < extLen) {
    return false;
  }

  const size_t offset = fileName.length() - extLen;
  for (size_t i = 0; i < extLen; i++) {
    if (tolower(static_cast<unsigned char>(fileName[offset + i])) !=
        tolower(static_cast<unsigned char>(extension[i]))) {
      return false;
    }
  }
  return true;
}

bool hasJpgExtension(std::string_view fileName) {
  return checkFileExtension(fileName, ".jpg") || checkFileExtension(fileName, ".jpeg");
}

bool hasPngExtension(std::string_view fileName) { return checkFileExtension(fileName, ".png"); }

bool hasBmpExtension(std::string_view fileName) { return checkFileExtension(fileName, ".bmp"); }

bool hasGifExtension(std::string_view fileName) { return checkFileExtension(fileName, ".gif"); }

bool hasEpubExtension(std::string_view fileName) { return checkFileExtension(fileName, ".epub"); }

bool hasXtcExtension(std::string_view fileName) {
  return checkFileExtension(fileName, ".xtc") || checkFileExtension(fileName, ".xtch");
}

bool hasTxtExtension(std::string_view fileName) { return checkFileExtension(fileName, ".txt"); }

bool hasPlainTextExtension(std::string_view fileName) {
  static constexpr const char* kPlainText[] = {".txt",  ".json", ".log", ".csv", ".xml",
                                               ".yaml", ".yml",  ".ini", ".cfg", ".conf"};
  for (const char* ext : kPlainText) {
    if (checkFileExtension(fileName, ext)) return true;
  }
  return false;
}

bool hasMarkdownExtension(std::string_view fileName) { return checkFileExtension(fileName, ".md"); }

bool hasCssExtension(std::string_view fileName) { return checkFileExtension(fileName, ".css"); }

std::string extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

namespace {

// FAT long-name case folding, ported from SdFat's common/upcase.cpp
// (Copyright (c) 2011-2025 Bill Greiman, MIT License) so path comparisons here
// agree with the card's: SdFat built with USE_UTF8_LONG_NAMES matches names by
// toUpcase() per UTF-16 unit (FatFileLFN.cpp), so "/Café" and "/CAFÉ" are one
// directory. Tables verbatim; flash-only.
struct UpcaseRange {
  uint16_t base;
  int8_t off;
  uint8_t count;
};
struct UpcasePair {
  uint16_t key;
  uint16_t val;
};
constexpr UpcaseRange UPCASE_RANGES[] = {
    {0X0061, -32, 26}, {0X00E0, -32, 23}, {0X00F8, -32, 7},  {0X0100, 1, 48},   {0X0132, 1, 6},    {0X0139, 1, 16},
    {0X014A, 1, 46},   {0X0179, 1, 6},    {0X0182, 1, 4},    {0X01A0, 1, 6},    {0X01B3, 1, 4},    {0X01CD, 1, 16},
    {0X01DE, 1, 18},   {0X01F8, 1, 40},   {0X0222, 1, 18},   {0X0246, 1, 10},   {0X03AD, -37, 3},  {0X03B1, -32, 17},
    {0X03C3, -32, 9},  {0X03D8, 1, 24},   {0X0430, -32, 32}, {0X0450, -80, 16}, {0X0460, 1, 34},   {0X048A, 1, 54},
    {0X04C1, 1, 14},   {0X04D0, 1, 68},   {0X0561, -48, 38}, {0X1E00, 1, 150},  {0X1EA0, 1, 90},   {0X1F00, 8, 8},
    {0X1F10, 8, 6},    {0X1F20, 8, 8},    {0X1F30, 8, 8},    {0X1F40, 8, 6},    {0X1F60, 8, 8},    {0X1F70, 74, 2},
    {0X1F72, 86, 4},   {0X1F76, 100, 2},  {0X1F7A, 112, 2},  {0X1F7C, 126, 2},  {0X1F80, 8, 8},    {0X1F90, 8, 8},
    {0X1FA0, 8, 8},    {0X1FB0, 8, 2},    {0X1FD0, 8, 2},    {0X1FE0, 8, 2},    {0X2170, -16, 16}, {0X24D0, -26, 26},
    {0X2C30, -48, 47}, {0X2C67, 1, 6},    {0X2C80, 1, 100},  {0X2D00, 0, 38},   {0XFF41, -32, 26},
};
constexpr UpcasePair UPCASE_PAIRS[] = {
    {0X00FF, 0X0178}, {0X0180, 0X0243}, {0X0188, 0X0187}, {0X018C, 0X018B}, {0X0192, 0X0191}, {0X0195, 0X01F6},
    {0X0199, 0X0198}, {0X019A, 0X023D}, {0X019E, 0X0220}, {0X01A8, 0X01A7}, {0X01AD, 0X01AC}, {0X01B0, 0X01AF},
    {0X01B9, 0X01B8}, {0X01BD, 0X01BC}, {0X01BF, 0X01F7}, {0X01C6, 0X01C4}, {0X01C9, 0X01C7}, {0X01CC, 0X01CA},
    {0X01DD, 0X018E}, {0X01F3, 0X01F1}, {0X01F5, 0X01F4}, {0X023A, 0X2C65}, {0X023C, 0X023B}, {0X023E, 0X2C66},
    {0X0242, 0X0241}, {0X0253, 0X0181}, {0X0254, 0X0186}, {0X0256, 0X0189}, {0X0257, 0X018A}, {0X0259, 0X018F},
    {0X025B, 0X0190}, {0X0260, 0X0193}, {0X0263, 0X0194}, {0X0268, 0X0197}, {0X0269, 0X0196}, {0X026B, 0X2C62},
    {0X026F, 0X019C}, {0X0272, 0X019D}, {0X0275, 0X019F}, {0X027D, 0X2C64}, {0X0280, 0X01A6}, {0X0283, 0X01A9},
    {0X0288, 0X01AE}, {0X0289, 0X0244}, {0X028A, 0X01B1}, {0X028B, 0X01B2}, {0X028C, 0X0245}, {0X0292, 0X01B7},
    {0X037B, 0X03FD}, {0X037C, 0X03FE}, {0X037D, 0X03FF}, {0X03AC, 0X0386}, {0X03C2, 0X03A3}, {0X03CC, 0X038C},
    {0X03CD, 0X038E}, {0X03CE, 0X038F}, {0X03F2, 0X03F9}, {0X03F8, 0X03F7}, {0X03FB, 0X03FA}, {0X04CF, 0X04C0},
    {0X1D7D, 0X2C63}, {0X1F51, 0X1F59}, {0X1F53, 0X1F5B}, {0X1F55, 0X1F5D}, {0X1F57, 0X1F5F}, {0X1F78, 0X1FF8},
    {0X1F79, 0X1FF9}, {0X1FB3, 0X1FBC}, {0X1FCC, 0X1FC3}, {0X1FE5, 0X1FEC}, {0X1FFC, 0X1FF3}, {0X214E, 0X2132},
    {0X2184, 0X2183}, {0X2C61, 0X2C60}, {0X2C76, 0X2C75},
};

// Index of the last entry whose `field` <= key (0 when none is).
template <typename T>
size_t searchByField(const T* table, size_t size, uint16_t T::* field, uint16_t key) {
  size_t left = 0;
  size_t right = size;
  while (right - left > 1) {
    const size_t mid = left + (right - left) / 2;
    if (table[mid].*field <= key) {
      left = mid;
    } else {
      right = mid;
    }
  }
  return left;
}

uint32_t fatUpcase(uint32_t cp) {
  if (cp > 0xFFFF) return cp;  // SdFat compares UTF-16 units; surrogates do not fold
  const uint16_t chr = static_cast<uint16_t>(cp);
  if (chr < 127) return chr - ('a' <= chr && chr <= 'z' ? 'a' - 'A' : 0);
  size_t i = searchByField(UPCASE_RANGES, std::size(UPCASE_RANGES), &UpcaseRange::base, chr);
  const uint16_t first = UPCASE_RANGES[i].base;
  if (first <= chr && (chr - first) < UPCASE_RANGES[i].count) {
    const int8_t off = UPCASE_RANGES[i].off;
    if (off == 1) return chr - ((chr - first) & 1);
    return static_cast<uint16_t>(chr + (off ? off : -0x1C60));
  }
  i = searchByField(UPCASE_PAIRS, std::size(UPCASE_PAIRS), &UpcasePair::key, chr);
  return UPCASE_PAIRS[i].key == chr ? UPCASE_PAIRS[i].val : chr;
}

// Decode one code point at s[i], advancing i. Malformed bytes decode as
// themselves, one byte each -- compared exactly, never folded into a match.
uint32_t nextCodePoint(std::string_view s, size_t& i) {
  const auto b0 = static_cast<unsigned char>(s[i]);
  size_t len = b0 < 0x80 ? 1 : (b0 >> 5) == 0x6 ? 2 : (b0 >> 4) == 0xE ? 3 : (b0 >> 3) == 0x1E ? 4 : 0;
  if (len == 0 || i + len > s.size()) {
    i += 1;
    return 0x110000u + b0;  // outside Unicode: cannot collide with a real code point
  }
  uint32_t cp = len == 1 ? b0 : (b0 & (0xFF >> (len + 1)));
  for (size_t k = 1; k < len; k++) {
    const auto b = static_cast<unsigned char>(s[i + k]);
    if ((b & 0xC0) != 0x80) {
      i += 1;
      return 0x110000u + b0;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  i += len;
  return cp;
}

std::string_view stripTrailingSlashes(std::string_view p) {
  while (!p.empty() && p.back() == '/') p.remove_suffix(1);
  return p;
}

// Bytes of `path` matched by the whole of `prefix` under FAT name folding, or
// npos when `prefix` is not a prefix of it.
size_t matchFatPrefix(std::string_view path, std::string_view prefix) {
  size_t i = 0;
  size_t j = 0;
  while (j < prefix.size()) {
    if (i >= path.size()) return std::string_view::npos;
    if (fatUpcase(nextCodePoint(path, i)) != fatUpcase(nextCodePoint(prefix, j))) return std::string_view::npos;
  }
  return i;
}

}  // namespace

bool isSameOrInside(std::string_view path, std::string_view ancestor) {
  path = stripTrailingSlashes(path);
  ancestor = stripTrailingSlashes(ancestor);
  if (ancestor.empty()) return true;  // the root contains everything
  const size_t matched = matchFatPrefix(path, ancestor);
  return matched != std::string_view::npos && (matched == path.size() || path[matched] == '/');
}

bool isSameFatPath(std::string_view a, std::string_view b) {
  a = stripTrailingSlashes(a);
  b = stripTrailingSlashes(b);
  return matchFatPrefix(a, b) == a.size();
}

void sanitizePathComponentForFat32(const char* input, char* output, size_t maxLen) {
  if (maxLen == 0) {
    return;
  }

  size_t i = 0;
  for (; i < maxLen - 1 && input[i] != '\0'; i++) {
    const char c = input[i];
    if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
        c == ' ' || (c > 0x00 && c <= 0x1f)) {
      output[i] = '-';
    } else {
      output[i] = c;
    }
  }
  output[i] = '\0';
}

}  // namespace FsHelpers
