#pragma once

#include <StreamingJsonParser.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Streaming parse of GitHub's release-by-tag JSON, collecting EVERY asset's API
// `url` (not browser_download_url -- that does not serve a PRIVATE repo's
// assets). Shared by LibraryUpdater and FontUpdater, which read two releases on
// the same private repo and need the same asset list from both.
//
// Kept separate from ReleaseJsonParser (the firmware-update one) because that
// parser keys on browser_download_url and keeps only firmware.bin.
//
// Lifted out of LibraryUpdater.cpp's anonymous namespace when Update Fonts
// arrived; the body below is that class unchanged, renamed. A second copy of a
// hand-written JSON state machine is not a thing to maintain twice.

class GithubReleaseAssetParser {
 public:
  struct Asset {
    std::string name;
    std::string url;
    size_t size = 0;
  };

  GithubReleaseAssetParser()
      : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                             sOnArrayStart, sOnArrayEnd}) {}

  void feed(const char* data, size_t len) { parser.feed(data, len); }
  bool hasError() const { return parser.hasError(); }
  const std::vector<Asset>& getAssets() const { return assets; }

 private:
  enum class Position : uint8_t { TOP_LEVEL, IN_ASSETS_ARRAY, IN_ASSET_OBJECT };
  enum class LastKey : uint8_t { NONE, ASSETS, ASSET_NAME, ASSET_URL, ASSET_SIZE };

  static void sOnKey(void* ctx, const char* key, size_t len) {
    auto* self = static_cast<GithubReleaseAssetParser*>(ctx);
    switch (self->position) {
      case Position::TOP_LEVEL:
        if (self->depth == 1 && len == 6 && memcmp(key, "assets", 6) == 0)
          self->lastKey = LastKey::ASSETS;
        else
          self->lastKey = LastKey::NONE;
        break;
      case Position::IN_ASSET_OBJECT:
        if (self->assetDepth == 1) {
          if (len == 4 && memcmp(key, "name", 4) == 0)
            self->lastKey = LastKey::ASSET_NAME;
          else if (len == 3 && memcmp(key, "url", 3) == 0)
            self->lastKey = LastKey::ASSET_URL;
          else if (len == 4 && memcmp(key, "size", 4) == 0)
            self->lastKey = LastKey::ASSET_SIZE;
          else
            self->lastKey = LastKey::NONE;
        }
        break;
      default:
        break;
    }
  }

  static void sOnString(void* ctx, const char* value, size_t len) {
    auto* self = static_cast<GithubReleaseAssetParser*>(ctx);
    if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
      if (self->lastKey == LastKey::ASSET_NAME)
        self->current.name.assign(value, len);
      else if (self->lastKey == LastKey::ASSET_URL)
        self->current.url.assign(value, len);
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnNumber(void* ctx, const char* value, size_t /*len*/) {
    auto* self = static_cast<GithubReleaseAssetParser*>(ctx);
    if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
      self->current.size = static_cast<size_t>(strtoul(value, nullptr, 10));
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnBool(void* ctx, bool) { static_cast<GithubReleaseAssetParser*>(ctx)->lastKey = LastKey::NONE; }
  static void sOnNull(void* ctx) { static_cast<GithubReleaseAssetParser*>(ctx)->lastKey = LastKey::NONE; }

  static void sOnObjectStart(void* ctx) {
    auto* self = static_cast<GithubReleaseAssetParser*>(ctx);
    switch (self->position) {
      case Position::TOP_LEVEL:
        self->depth++;
        self->lastKey = LastKey::NONE;
        break;
      case Position::IN_ASSETS_ARRAY:
        self->position = Position::IN_ASSET_OBJECT;
        self->assetDepth = 1;
        self->current = Asset{};
        self->lastKey = LastKey::NONE;
        break;
      case Position::IN_ASSET_OBJECT:
        self->assetDepth++;
        self->lastKey = LastKey::NONE;
        break;
    }
  }

  static void sOnObjectEnd(void* ctx) {
    auto* self = static_cast<GithubReleaseAssetParser*>(ctx);
    switch (self->position) {
      case Position::TOP_LEVEL:
        if (self->depth > 0) self->depth--;
        break;
      case Position::IN_ASSET_OBJECT:
        self->assetDepth--;
        if (self->assetDepth == 0) {
          // Capped: the release JSON comes from the network (the fixed
          // api.github.com endpoint today, but a hostile or MITM'd response
          // could stream assets without end and grow this vector until the
          // ~380 KB device heap is exhausted -- crafted-input hunt 2026-09-04).
          // A real release has a handful of assets; kMaxAssets is generous.
          constexpr size_t kMaxAssets = 512;
          if (!self->current.name.empty() && !self->current.url.empty() && self->assets.size() < kMaxAssets) {
            // Resource Protocol 7. Unreserved, 79 assets doubles its way to a
            // capacity-128 vector -- one 6,656-byte contiguous operator new
            // with the old 3,328-byte block still live, on the heap that is
            // about to be asked for the manifest. A release carries a handful
            // of assets per family; 96 covers thirteen families with room, and
            // the cap still bounds a hostile response.
            if (self->assets.capacity() == 0) self->assets.reserve(96);
            self->assets.push_back(self->current);
          }
          self->position = Position::IN_ASSETS_ARRAY;
        }
        self->lastKey = LastKey::NONE;
        break;
      default:
        break;
    }
  }

  static void sOnArrayStart(void* ctx) {
    auto* self = static_cast<GithubReleaseAssetParser*>(ctx);
    switch (self->position) {
      case Position::TOP_LEVEL:
        if (self->lastKey == LastKey::ASSETS && self->depth == 1) {
          self->position = Position::IN_ASSETS_ARRAY;
        } else {
          self->depth++;
        }
        self->lastKey = LastKey::NONE;
        break;
      case Position::IN_ASSET_OBJECT:
        self->assetDepth++;
        self->lastKey = LastKey::NONE;
        break;
      default:
        break;
    }
  }

  static void sOnArrayEnd(void* ctx) {
    auto* self = static_cast<GithubReleaseAssetParser*>(ctx);
    switch (self->position) {
      case Position::TOP_LEVEL:
        if (self->depth > 0) self->depth--;
        break;
      case Position::IN_ASSETS_ARRAY:
        self->position = Position::TOP_LEVEL;
        break;
      case Position::IN_ASSET_OBJECT:
        self->assetDepth--;
        self->lastKey = LastKey::NONE;
        break;
    }
  }

  StreamingJsonParser parser;
  Position position = Position::TOP_LEVEL;
  LastKey lastKey = LastKey::NONE;
  uint8_t depth = 0;
  uint8_t assetDepth = 0;
  Asset current;
  std::vector<Asset> assets;
};
