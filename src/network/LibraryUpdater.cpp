#include "LibraryUpdater.h"

// clang-format off
// Same include-order constraint as OtaUpdater.cpp: HttpDownloader.h pulls
// Arduino/SdFat, whose macros collide with lwip's ip4_addr.h unless seen first.
#include "HttpDownloader.h"
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <StreamingJsonParser.h>
#include <mbedtls/sha256.h>
// clang-format on

#include <cstdio>
#include <cstring>
#include <functional>

#include "CrossPointSettings.h"
#include "LibrarySyncPlan.h"

namespace {

// THE CONTENT REPO, not the firmware repo: books come from the claude-tools
// library release, tagged library-latest and refreshed in place by its
// scripts/publish_library.py. The repo is private; see the class comment.
constexpr char libraryReleaseUrl[] = "https://api.github.com/repos/natebunnyfield/claude-tools/releases/tags/library-latest";
constexpr char manifestAssetName[] = "manifest.json";
constexpr char booksDir[] = "/books/";
constexpr size_t SHA_CHUNK = 1024;

std::string bearerHeaderValue() {
  // Built in one place so no call site ever holds the raw token where a log
  // line could pick it up. NEVER log the returned value.
  return std::string("Bearer ") + SETTINGS.githubToken;
}

// Streaming parse of the release-by-tag JSON, collecting EVERY asset's API
// `url` (not browser_download_url — that does not serve a private repo's
// assets). Same StreamingJsonParser skeleton as ReleaseJsonParser; kept
// separate because that parser keys on browser_download_url and keeps only
// firmware.bin, and this one must keep the whole asset list.
class LibraryReleaseParser {
 public:
  struct Asset {
    std::string name;
    std::string url;
    size_t size = 0;
  };

  LibraryReleaseParser()
      : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                             sOnArrayStart, sOnArrayEnd}) {}

  void feed(const char* data, size_t len) { parser.feed(data, len); }
  bool hasError() const { return parser.hasError(); }
  const std::vector<Asset>& getAssets() const { return assets; }

 private:
  enum class Position : uint8_t { TOP_LEVEL, IN_ASSETS_ARRAY, IN_ASSET_OBJECT };
  enum class LastKey : uint8_t { NONE, ASSETS, ASSET_NAME, ASSET_URL, ASSET_SIZE };

  static void sOnKey(void* ctx, const char* key, size_t len) {
    auto* self = static_cast<LibraryReleaseParser*>(ctx);
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
    auto* self = static_cast<LibraryReleaseParser*>(ctx);
    if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
      if (self->lastKey == LastKey::ASSET_NAME)
        self->current.name.assign(value, len);
      else if (self->lastKey == LastKey::ASSET_URL)
        self->current.url.assign(value, len);
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnNumber(void* ctx, const char* value, size_t /*len*/) {
    auto* self = static_cast<LibraryReleaseParser*>(ctx);
    if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
      self->current.size = static_cast<size_t>(strtoul(value, nullptr, 10));
    }
    self->lastKey = LastKey::NONE;
  }

  static void sOnBool(void* ctx, bool) { static_cast<LibraryReleaseParser*>(ctx)->lastKey = LastKey::NONE; }
  static void sOnNull(void* ctx) { static_cast<LibraryReleaseParser*>(ctx)->lastKey = LastKey::NONE; }

  static void sOnObjectStart(void* ctx) {
    auto* self = static_cast<LibraryReleaseParser*>(ctx);
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
    auto* self = static_cast<LibraryReleaseParser*>(ctx);
    switch (self->position) {
      case Position::TOP_LEVEL:
        if (self->depth > 0) self->depth--;
        break;
      case Position::IN_ASSET_OBJECT:
        self->assetDepth--;
        if (self->assetDepth == 0) {
          if (!self->current.name.empty() && !self->current.url.empty()) {
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
    auto* self = static_cast<LibraryReleaseParser*>(ctx);
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
    auto* self = static_cast<LibraryReleaseParser*>(ctx);
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

void hexDigest(const unsigned char digest[32], char outHex[65]) {
  static const char* hex = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    outHex[i * 2] = hex[digest[i] >> 4];
    outHex[i * 2 + 1] = hex[digest[i] & 0x0F];
  }
  outHex[64] = '\0';
}

}  // namespace

LibraryUpdater::LibraryError LibraryUpdater::fetchManifest() {
  if (SETTINGS.githubToken[0] == '\0') {
    // Not an error to retry — the screen tells the owner where the token goes.
    return NO_TOKEN;
  }

  books.clear();

  const HttpDownloader::HeaderList apiHeaders = {
      {"Accept", "application/vnd.github+json"},
      {"Authorization", bearerHeaderValue()},
  };

  auto parser = makeUniqueNoThrow<LibraryReleaseParser>();
  if (!parser) {
    LOG_ERR("LIB", "OOM: release JSON parser");
    return OOM_ERROR;
  }
  LibraryReleaseParser& releaseParser = *parser;
  const HttpDownloader::DownloadError fetched =
      HttpDownloader::fetchUrlWithHeaders(libraryReleaseUrl, apiHeaders, [&releaseParser](const uint8_t* data, size_t len) {
        releaseParser.feed(reinterpret_cast<const char*>(data), len);
        return true;
      });
  if (fetched == HttpDownloader::NOT_FOUND) {
    // GitHub answered; the answer is "no such release" (or the token cannot
    // see the repo — GitHub reports both as 404 for a private repo).
    LOG_DBG("LIB", "No library release at %s", libraryReleaseUrl);
    return NO_RELEASE;
  }
  if (fetched != HttpDownloader::OK) {
    LOG_ERR("LIB", "Release fetch failed");
    return HTTP_ERROR;
  }
  if (releaseParser.hasError()) {
    LOG_ERR("LIB", "Release JSON did not parse");
    return JSON_PARSE_ERROR;
  }

  const auto& assets = releaseParser.getAssets();
  LOG_DBG("LIB", "Release lists %u assets", static_cast<unsigned>(assets.size()));

  const LibraryReleaseParser::Asset* manifestAsset = nullptr;
  for (const auto& asset : assets) {
    if (asset.name == manifestAssetName) {
      manifestAsset = &asset;
      break;
    }
  }
  if (!manifestAsset) {
    LOG_ERR("LIB", "Release has no %s asset", manifestAssetName);
    return NO_MANIFEST;
  }

  // Asset bodies from a private repo come through the asset API url with
  // octet-stream Accept; GitHub then 302s to a CDN, which HttpDownloader's
  // redirect handling follows.
  const HttpDownloader::HeaderList assetHeaders = {
      {"Accept", "application/octet-stream"},
      {"Authorization", bearerHeaderValue()},
  };

  std::string manifestBody;
  const HttpDownloader::DownloadError manifestFetched = HttpDownloader::fetchUrlWithHeaders(
      manifestAsset->url, assetHeaders, [&manifestBody](const uint8_t* data, size_t len) {
        manifestBody.append(reinterpret_cast<const char*>(data), len);
        return true;
      });
  if (manifestFetched != HttpDownloader::OK) {
    LOG_ERR("LIB", "Manifest fetch failed");
    return HTTP_ERROR;
  }

  JsonDocument doc;
  if (deserializeJson(doc, manifestBody) != DeserializationError::Ok) {
    LOG_ERR("LIB", "Manifest JSON did not parse");
    return JSON_PARSE_ERROR;
  }
  JsonArrayConst manifestBooks = doc["books"].as<JsonArrayConst>();
  if (manifestBooks.isNull()) {
    LOG_ERR("LIB", "Manifest has no books array");
    return JSON_PARSE_ERROR;
  }

  for (JsonObjectConst entry : manifestBooks) {
    Book book;
    book.file = entry["file"] | "";
    book.bytes = entry["bytes"] | 0;
    book.sha256 = entry["sha256"] | "";
    const char* assetName = entry["asset"] | "";
    if (!librarysync::isSafeFileName(book.file.c_str()) || book.sha256.size() != 64) {
      LOG_ERR("LIB", "Skipping malformed manifest entry");
      continue;
    }
    for (const auto& asset : assets) {
      if (asset.name == assetName) {
        book.url = asset.url;
        break;
      }
    }
    if (book.url.empty()) {
      // Manifest promises a book the release does not carry: the publisher
      // uploads them together, so this means a half-updated release.
      LOG_ERR("LIB", "No release asset for %s", book.file.c_str());
      continue;
    }
    books.push_back(std::move(book));
  }

  LOG_INF("LIB", "Manifest lists %u books", static_cast<unsigned>(books.size()));
  return OK;
}

bool LibraryUpdater::computeCardSha256(const std::string& path, char outHex[65]) {
  HalFile file;
  if (!Storage.openFileForRead("LIB", path, file)) return false;

  auto buf = makeUniqueNoThrow<uint8_t[]>(SHA_CHUNK);
  if (!buf) {
    LOG_ERR("LIB", "OOM: sha256 read buffer");
    return false;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, /*is224=*/0);
  while (true) {
    const int got = file.read(buf.get(), SHA_CHUNK);
    if (got < 0) {
      mbedtls_sha256_free(&sha);
      return false;
    }
    if (got == 0) break;
    mbedtls_sha256_update(&sha, buf.get(), static_cast<size_t>(got));
  }
  unsigned char digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  hexDigest(digest, outHex);
  return true;
}

LibraryUpdater::BookResult LibraryUpdater::syncBook(size_t index, ProgressCallback onProgress, void* ctx) {
  if (index >= books.size()) return BookResult::FAILED;
  const Book& book = books[index];

  const std::string destPath = booksDir + book.file;
  const bool existed = Storage.exists(destPath.c_str());
  size_t cardBytes = 0;
  if (existed) {
    HalFile f;
    if (Storage.openFileForRead("LIB", destPath, f)) cardBytes = f.size();
  }

  if (librarysync::sizeVerdict(existed, cardBytes, book.bytes) == librarysync::SizeVerdict::CHECK_SHA) {
    char cardSha[65];
    if (computeCardSha256(destPath, cardSha) && librarysync::shaMatches(cardSha, book.sha256.c_str())) {
      LOG_DBG("LIB", "Unchanged: %s", book.file.c_str());
      return BookResult::UNCHANGED;
    }
  }

  // Download to a temp name beside the destination, verifying the digest as
  // the bytes stream, then rename into place — the same crash-safety shape as
  // ProgressFile::writeAtomic. A power cut mid-download costs a .part file,
  // never the book that was already on the card.
  const std::string partPath = destPath + ".part";
  if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());

  HalFile out;
  if (!Storage.openFileForWrite("LIB", partPath, out)) {
    LOG_ERR("LIB", "Cannot open %s for write", partPath.c_str());
    return BookResult::FAILED;
  }

  const HttpDownloader::HeaderList assetHeaders = {
      {"Accept", "application/octet-stream"},
      {"Authorization", bearerHeaderValue()},
  };

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, /*is224=*/0);

  processedSize = 0;
  totalSize = book.bytes;
  int lastReportedPct = -1;
  bool writeOk = true;
  const HttpDownloader::DownloadError fetched =
      HttpDownloader::fetchUrlWithHeaders(book.url, assetHeaders, [&](const uint8_t* data, size_t len) {
        if (out.write(data, len) != len) {
          writeOk = false;
          return false;
        }
        mbedtls_sha256_update(&sha, data, len);
        processedSize += len;
        if (onProgress && totalSize > 0) {
          const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
          if (pct != lastReportedPct) {
            lastReportedPct = pct;
            onProgress(ctx);
          }
        }
        return true;
      });

  unsigned char digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  out.close();

  char gotSha[65];
  hexDigest(digest, gotSha);

  if (fetched != HttpDownloader::OK || !writeOk || processedSize != book.bytes ||
      !librarysync::shaMatches(gotSha, book.sha256.c_str())) {
    LOG_ERR("LIB", "Download of %s failed verification (%u of %u bytes)", book.file.c_str(),
            static_cast<unsigned>(processedSize), static_cast<unsigned>(book.bytes));
    Storage.remove(partPath.c_str());
    return BookResult::FAILED;
  }

  if (existed) Storage.remove(destPath.c_str());
  if (!Storage.rename(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("LIB", "Rename into place failed: %s", destPath.c_str());
    Storage.remove(partPath.c_str());
    return BookResult::FAILED;
  }

  if (existed) {
    // The reader's caches under /.crosspoint/epub_<hash> are keyed by PATH and
    // validated only by a format version, never against the epub's bytes — so
    // a replaced book would keep rendering from the OLD book's layout cache,
    // whose zip offsets no longer exist. Clearing the dir makes the next open
    // rebuild. This also drops progress.bin: the reading position of an
    // UPDATED book does not survive, and a position into the old pagination
    // would be fiction anyway. Unchanged books keep theirs.
    const std::string cachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(destPath));
    if (Storage.exists(cachePath.c_str())) {
      Storage.removeDir(cachePath.c_str());
      LOG_DBG("LIB", "Cleared stale cache for %s", book.file.c_str());
    }
  }

  LOG_INF("LIB", "%s: %s", existed ? "Updated" : "Added", book.file.c_str());
  return existed ? BookResult::UPDATED : BookResult::ADDED;
}
