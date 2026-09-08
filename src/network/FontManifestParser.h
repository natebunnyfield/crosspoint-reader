#pragma once

#include <StreamingJsonParser.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Streaming parse of the fonts manifest.json, one family at a time.
//
// WHY THIS EXISTS RATHER THAN ArduinoJson. The manifest used to be held whole
// in RAM and then parsed into a JsonDocument. Measured on 2026-09-07: the text
// is 18,108 bytes in ONE contiguous block and the document is a further ~14.6
// KB across 185 blocks, both live at the same instant because the buffer is
// only released after the parse. That is a ~33 KB peak on a device that is
// MEASURED refusing a 22,049-byte contiguous allocation with Wi-Fi and wolfSSL
// resident (B-053). Nothing about sizing that buffer more tightly fixes it --
// the buffer should not exist. Here the JSON is consumed as it arrives off the
// socket and nothing larger than one family is ever resident.
//
// The shape, which publish_fonts.py writes:
//
//   { "version": 1, "generated": "...",
//     "families": [ { "family": "DanteMT",
//                     "files": [ { "file": "DanteMT_10.cpfont",
//                                  "asset": "DanteMT_10.cpfont",
//                                  "bytes": 66794,
//                                  "sha256": "<64 hex>" }, ... ] }, ... ] }
//
// Deliberately dumb: it collects, it does not validate. Every rule about what a
// family or a file must look like -- the name shape, the point-size parse, the
// duplicate-size refusal, the all-or-nothing family rule, matching a file to a
// release asset -- stays in FontUpdater::fetchManifest where it is written down
// with its reasoning. This class only decides when one family has finished
// arriving, hands it over, and forgets it.
//
// Modelled on GithubReleaseAssetParser, which does the same job for the release
// JSON one function earlier. A third hand-written JSON state machine is not
// something to maintain, so the two share StreamingJsonParser underneath.

class FontManifestParser {
 public:
  struct RawFile {
    std::string file;
    std::string asset;
    std::string sha256;
    size_t bytes = 0;
  };
  struct RawFamily {
    std::string name;
    std::vector<RawFile> files;
  };

  // Called as each family object closes. Return false to stop collecting: the
  // parser keeps consuming bytes (the transfer is not ours to abort) but
  // reports nothing further.
  using FamilyCallback = bool (*)(void* ctx, RawFamily& family);

  FontManifestParser(FamilyCallback onFamily, void* ctx, size_t maxFilesPerFamily)
      : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                             sOnArrayStart, sOnArrayEnd}),
        familyCb(onFamily),
        familyCtx(ctx),
        maxFiles(maxFilesPerFamily) {}

  void feed(const char* data, size_t len) { parser.feed(data, len); }
  bool hasError() const { return parser.hasError(); }

  // Absent from the manifest means 1: publish_fonts.py stamped no version at
  // all before the field existed, and those manifests are version 1.
  int version() const { return manifestVersion; }
  // Distinguishes "no families array" (a manifest we do not understand) from
  // "an empty families array" (a real, if odd, answer). The removal gate turns
  // on exactly that difference -- an unparsed manifest must delete nothing.
  bool sawFamiliesArray() const { return sawFamilies; }
  // Did the top-level object actually CLOSE?
  //
  // This is the difference between a manifest and the first two thirds of one,
  // and a streaming parser cannot infer it the way a buffered one gets it for
  // free: ArduinoJson refused a truncated document outright, whereas this one
  // has already handed over every family that arrived whole and has no opinion
  // about the ones that did not. Without this check a body cut mid-transfer
  // returns OK with a SHORT family list -- and the sync is a mirror, so every
  // family the truncation removed would be deleted from the card. Caught by
  // FontCommit.ATruncatedManifestRemovesNothing on the first run of the
  // streaming parser, which is exactly the test that exists for it.
  // The top-level object closing is sufficient and is the whole test: a
  // families array cut mid-stream can never close the object that encloses it,
  // so this catches every truncation. Requiring the array to have closed TOO
  // conflated "truncated" with "has no families array" -- a manifest of
  // {"version":1} is complete and simply says nothing, which is what
  // sawFamiliesArray() is for.
  bool documentComplete() const { return topLevelClosed; }
  size_t familiesSeen() const { return seen; }

 private:
  enum class Position : uint8_t { TOP_LEVEL, IN_FAMILIES_ARRAY, IN_FAMILY_OBJECT, IN_FILES_ARRAY, IN_FILE_OBJECT };
  enum class LastKey : uint8_t {
    NONE,
    VERSION,
    FAMILIES,
    FAMILY_NAME,
    FILES,
    FILE_NAME,
    FILE_ASSET,
    FILE_BYTES,
    FILE_SHA
  };

  static bool keyIs(const char* key, size_t len, const char* want, size_t wantLen) {
    return len == wantLen && memcmp(key, want, wantLen) == 0;
  }

  static void sOnKey(void* ctx, const char* key, size_t len) {
    auto* self = static_cast<FontManifestParser*>(ctx);
    self->lastKey = LastKey::NONE;
    switch (self->position) {
      case Position::TOP_LEVEL:
        if (self->depth == 1) {
          if (keyIs(key, len, "version", 7))
            self->lastKey = LastKey::VERSION;
          else if (keyIs(key, len, "families", 8))
            self->lastKey = LastKey::FAMILIES;
        }
        break;
      case Position::IN_FAMILY_OBJECT:
        if (self->familyDepth == 1) {
          if (keyIs(key, len, "family", 6))
            self->lastKey = LastKey::FAMILY_NAME;
          else if (keyIs(key, len, "files", 5))
            self->lastKey = LastKey::FILES;
        }
        break;
      case Position::IN_FILE_OBJECT:
        if (self->fileDepth == 1) {
          if (keyIs(key, len, "file", 4))
            self->lastKey = LastKey::FILE_NAME;
          else if (keyIs(key, len, "asset", 5))
            self->lastKey = LastKey::FILE_ASSET;
          else if (keyIs(key, len, "bytes", 5))
            self->lastKey = LastKey::FILE_BYTES;
          else if (keyIs(key, len, "sha256", 6))
            self->lastKey = LastKey::FILE_SHA;
        }
        break;
      default:
        break;
    }
  }

  static void sOnString(void* ctx, const char* value, size_t len) {
    auto* self = static_cast<FontManifestParser*>(ctx);
    switch (self->lastKey) {
      case LastKey::FAMILY_NAME:
        self->current.name.assign(value, len);
        break;
      case LastKey::FILE_NAME:
        self->currentFile.file.assign(value, len);
        break;
      case LastKey::FILE_ASSET:
        self->currentFile.asset.assign(value, len);
        break;
      case LastKey::FILE_SHA:
        self->currentFile.sha256.assign(value, len);
        break;
      default:
        break;
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnNumber(void* ctx, const char* value, size_t len) {
    auto* self = static_cast<FontManifestParser*>(ctx);
    // StreamingJsonParser hands the raw token; it is NUL-terminated in its own
    // buffer, but take the length seriously rather than trusting that.
    char buf[24];
    const size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, value, n);
    buf[n] = '\0';
    if (self->lastKey == LastKey::FILE_BYTES) {
      self->currentFile.bytes = static_cast<size_t>(strtoul(buf, nullptr, 10));
    } else if (self->lastKey == LastKey::VERSION) {
      self->manifestVersion = static_cast<int>(strtol(buf, nullptr, 10));
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnBool(void* ctx, bool) { static_cast<FontManifestParser*>(ctx)->lastKey = LastKey::NONE; }
  static void sOnNull(void* ctx) { static_cast<FontManifestParser*>(ctx)->lastKey = LastKey::NONE; }

  static void sOnArrayStart(void* ctx) {
    auto* self = static_cast<FontManifestParser*>(ctx);
    if (self->position == Position::TOP_LEVEL && self->lastKey == LastKey::FAMILIES) {
      self->position = Position::IN_FAMILIES_ARRAY;
      self->sawFamilies = true;
    } else if (self->position == Position::IN_FAMILY_OBJECT && self->lastKey == LastKey::FILES) {
      self->position = Position::IN_FILES_ARRAY;
    } else if (self->position == Position::TOP_LEVEL) {
      self->depth++;  // some other top-level array; keep the key depth honest
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnArrayEnd(void* ctx) {
    auto* self = static_cast<FontManifestParser*>(ctx);
    if (self->position == Position::IN_FAMILIES_ARRAY) {
      self->position = Position::TOP_LEVEL;
    } else if (self->position == Position::IN_FILES_ARRAY) {
      self->position = Position::IN_FAMILY_OBJECT;
    } else if (self->position == Position::TOP_LEVEL && self->depth > 1) {
      self->depth--;
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnObjectStart(void* ctx) {
    auto* self = static_cast<FontManifestParser*>(ctx);
    switch (self->position) {
      case Position::TOP_LEVEL:
        self->depth++;
        break;
      case Position::IN_FAMILIES_ARRAY:
        self->position = Position::IN_FAMILY_OBJECT;
        self->familyDepth = 1;
        self->current = RawFamily{};
        // Six cuts a family today. Reserving that costs 6 * sizeof(RawFile)
        // once instead of three reallocations, and Resource Protocol 7 asks
        // for it -- but never reserve maxFiles, which is the hostile-input
        // bound, not the expected one.
        self->current.files.reserve(6);
        break;
      case Position::IN_FAMILY_OBJECT:
        self->familyDepth++;
        break;
      case Position::IN_FILES_ARRAY:
        self->position = Position::IN_FILE_OBJECT;
        self->fileDepth = 1;
        self->currentFile = RawFile{};
        break;
      case Position::IN_FILE_OBJECT:
        self->fileDepth++;
        break;
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnObjectEnd(void* ctx) {
    auto* self = static_cast<FontManifestParser*>(ctx);
    switch (self->position) {
      case Position::TOP_LEVEL:
        if (self->depth > 0) {
          self->depth--;
          if (self->depth == 0) self->topLevelClosed = true;
        }
        break;
      case Position::IN_FAMILY_OBJECT:
        if (self->familyDepth > 1) {
          self->familyDepth--;
        } else {
          // One family has finished arriving. Hand it over and drop it: this is
          // the moment that keeps the peak at one family instead of thirteen.
          self->position = Position::IN_FAMILIES_ARRAY;
          self->seen++;
          if (self->collecting && self->familyCb) {
            self->collecting = self->familyCb(self->familyCtx, self->current);
          }
          self->current = RawFamily{};
        }
        break;
      case Position::IN_FILE_OBJECT:
        if (self->fileDepth > 1) {
          self->fileDepth--;
        } else {
          self->position = Position::IN_FILES_ARRAY;
          // Over the per-family bound the entry is dropped, not the family --
          // FontUpdater sees a short file list and applies its own
          // all-or-nothing rule to it, which is where that decision lives.
          if (self->current.files.size() < self->maxFiles) {
            self->current.files.push_back(std::move(self->currentFile));
          }
          self->currentFile = RawFile{};
        }
        break;
      default:
        break;
    }
    self->lastKey = LastKey::NONE;
  }

  StreamingJsonParser parser;
  FamilyCallback familyCb = nullptr;
  void* familyCtx = nullptr;
  size_t maxFiles = 16;

  Position position = Position::TOP_LEVEL;
  LastKey lastKey = LastKey::NONE;
  int depth = 0;
  int familyDepth = 0;
  int fileDepth = 0;
  int manifestVersion = 1;
  bool sawFamilies = false;
  bool topLevelClosed = false;
  bool collecting = true;
  size_t seen = 0;

  RawFamily current;
  RawFile currentFile;
};
