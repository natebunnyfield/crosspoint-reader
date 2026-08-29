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
#ifdef SIMULATOR
// The host's own settings surface, where a platform with no way to edit the
// card's settings.json keeps this token instead. Simulator-only by
// construction: the header is part of the simulator library and folds to a
// constant everywhere but a phone. See SimHostSettings.h.
#include <SimHostSettings.h>
#endif

namespace {

// THE CONTENT REPO, not the firmware repo: books come from the claude-tools
// library release, tagged library-latest and refreshed in place by its
// scripts/publish_library.py. The repo is private; see the class comment.
constexpr char libraryReleaseUrl[] = "https://api.github.com/repos/natebunnyfield/claude-tools/releases/tags/library-latest";
constexpr char manifestAssetName[] = "manifest.json";
// The repo itself, probed ONLY to disambiguate a 404 on the release above.
// GitHub answers 404 for a private repo whether the release is missing or the
// token cannot see the repo -- it will not confirm the repo exists to someone
// who may not be entitled to know. So the release endpoint alone cannot tell
// "nothing published" from "wrong token", and the owner is left guessing at the
// one moment he most needs to be told. Asking about the REPO separates them:
// 404 here means the token cannot see it, 200 means it can and the release
// really is absent.
constexpr char libraryRepoUrl[] = "https://api.github.com/repos/natebunnyfield/claude-tools";
constexpr char booksDir[] = "/books/";
constexpr size_t SHA_CHUNK = 1024;

// The ledger of "what this book looked like when its digest was last checked".
// Beside the reader's other state, not under /books/, because it is this
// firmware's bookkeeping and a card plugged into a computer should show books.
constexpr char syncRecordsPath[] = "/.crosspoint/library_sync.json";
// Bump when the shape of a record changes. A file at any other version is
// IGNORED WHOLE, which costs one hashing pass and can never mis-skip: the
// alternative -- reading fields whose meaning may have moved -- decides
// downloads from numbers it does not understand.
constexpr int SYNC_RECORDS_VERSION = 1;
// The highest manifest.json this build knows how to act on. The published
// manifest carries no "version" key today, which is why the default is 1: an
// unversioned file is version 1 by definition, and the check only bites once
// the publisher starts stamping them.
constexpr int MAX_MANIFEST_VERSION = 1;

// The token, from wherever this build's owner can actually set one.
//
// SETTINGS.githubToken is the answer on hardware, where it is hand-edited into
// /.crosspoint/settings.json on the card. A HOST BUILD MAY HAVE NO WAY TO EDIT
// THAT FILE -- an iPhone does not -- so the simulator offers a settings surface
// of its own and it wins when it holds anything. It is deliberately not copied
// INTO SETTINGS at boot: that field is persisted by the next settings save, and
// on iOS the directory it saves to is served over the LAN by File Transfer and
// WebDAV. One fewer copy of a credential, for no loss of function.
//
// NEVER LOG THE RETURN VALUE, here or at any call site.
std::string githubTokenValue() {
#ifdef SIMULATOR
  char hosted[sizeof(SETTINGS.githubToken)] = {};
  const size_t hostedLength = sim_host_settings::githubToken(hosted, sizeof(hosted));
  if (hostedLength != 0) {
    // Length only. A token longer than the field is a paste error, and it will
    // fail authentication with a 401 that says nothing about why -- so the one
    // place that can tell says so, without the bytes.
    if (hostedLength > sizeof(hosted) - 1) {
      LOG_ERR("LIB", "host GitHub token is %u bytes; the field holds %u -- truncated, and it will not authenticate",
              static_cast<unsigned>(hostedLength), static_cast<unsigned>(sizeof(hosted) - 1));
    }
    return std::string(hosted);
  }
#endif
  return std::string(SETTINGS.githubToken);
}

std::string bearerHeaderValue() {
  // Built in one place so no call site ever holds the raw token where a log
  // line could pick it up. NEVER log the returned value.
  return std::string("Bearer ") + githubTokenValue();
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
  if (githubTokenValue().empty()) {
    // Not an error to retry — the screen tells the owner where the token goes,
    // and on a host build that is the host's settings app rather than a file.
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
  if (fetched == HttpDownloader::UNAUTHORIZED) {
    // GitHub answered, and the answer was "not you". Reported apart from
    // HTTP_ERROR because that one says "could not reach GitHub" and sends the
    // owner to debug a network that is working. This became the LIKELY failure
    // the moment the token stopped coming from a carefully edited file: typed
    // on a phone keyboard, a wrong token is the first thing to suspect.
    LOG_ERR("LIB", "GitHub rejected the token (401/403)");
    return BAD_TOKEN;
  }
  if (fetched == HttpDownloader::NOT_FOUND) {
    // A 404 here is AMBIGUOUS and the screen used to have to say so. GitHub
    // answers 404 for a private repo whether the release is missing or the
    // token cannot see it. So ask about the REPO, which separates them: this
    // costs one extra request, only on the failure path, and turns "one of two
    // things is wrong" into an answer.
    size_t repoBytes = 0;
    const HttpDownloader::DownloadError repoSeen = HttpDownloader::fetchUrlWithHeaders(
        libraryRepoUrl, apiHeaders, [&repoBytes](const uint8_t*, size_t len) {
          repoBytes += len;
          return true;
        });
    if (repoSeen == HttpDownloader::OK) {
      LOG_DBG("LIB", "Repo is visible; the release really is absent");
      return NO_RELEASE;
    }
    LOG_ERR("LIB", "The token cannot see %s (release 404, repo %s)", libraryRepoUrl,
            repoSeen == HttpDownloader::NOT_FOUND ? "404" : "unreachable");
    return NO_REPO_ACCESS;
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
  // The manifest had no versioning at all until this check existed. Refusing an
  // unknown version is the point of adding one: a future manifest that redefines
  // "bytes" or "sha256" would otherwise be acted on silently, and the damage is
  // downloads over books that were fine.
  const int manifestVersion = doc["version"] | 1;
  if (manifestVersion > MAX_MANIFEST_VERSION) {
    LOG_ERR("LIB", "Manifest is version %d; this firmware understands %d", manifestVersion, MAX_MANIFEST_VERSION);
    return MANIFEST_TOO_NEW;
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

void LibraryUpdater::loadSyncRecords() {
  if (recordsLoaded) return;
  recordsLoaded = true;  // set FIRST: a missing or unreadable file means "no records", and
                         // retrying the read per book would cost a card open each time.

  HalFile file;
  if (!Storage.openFileForRead("LIB", syncRecordsPath, file)) {
    LOG_DBG("LIB", "No sync ledger yet; every book will be hashed once");
    return;
  }
  // A corrupt directory entry can report any size at all, and this allocates
  // exactly what it is told. 64 KB is ~500 records against a library of a few
  // dozen; a file past it is not a ledger and reading none is the safe answer,
  // because "no records" only ever costs a hashing pass.
  constexpr size_t MAX_LEDGER_BYTES = 64 * 1024;
  const size_t ledgerBytes = file.size();
  if (ledgerBytes > MAX_LEDGER_BYTES) {
    LOG_ERR("LIB", "Sync ledger is %u bytes; ignoring it", static_cast<unsigned>(ledgerBytes));
    file.close();
    return;
  }
  std::string body;
  body.resize(ledgerBytes);
  if (!body.empty()) file.read(&body[0], body.size());
  file.close();

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    LOG_ERR("LIB", "Sync ledger did not parse; hashing everything once");
    return;
  }
  const int version = doc["version"] | 0;
  if (version != SYNC_RECORDS_VERSION) {
    LOG_INF("LIB", "Sync ledger is version %d, not %d; hashing everything once", version, SYNC_RECORDS_VERSION);
    return;
  }
  for (JsonObjectConst entry : doc["books"].as<JsonArrayConst>()) {
    StoredRecord record;
    record.file = entry["f"] | "";
    record.sha = entry["s"] | "";
    record.bytes = entry["b"] | 0;
    record.fatDate = static_cast<uint16_t>(entry["d"] | 0);
    record.fatTime = static_cast<uint16_t>(entry["t"] | 0);
    // A record with no name or a truncated digest cannot answer anything; drop
    // it rather than let it match a book by accident.
    if (record.file.empty() || record.sha.size() != 64) continue;
    records.push_back(std::move(record));
  }
  LOG_DBG("LIB", "Sync ledger: %u records", static_cast<unsigned>(records.size()));
}

const LibraryUpdater::StoredRecord* LibraryUpdater::findRecord(const std::string& file) const {
  for (const auto& record : records) {
    if (record.file == file) return &record;
  }
  return nullptr;
}

void LibraryUpdater::putRecord(const std::string& file, const librarysync::CardStamp& stamp, const std::string& sha) {
  // No mtime means nothing worth recording: hashVerdict refuses to skip without
  // one, so a record carrying a zero would be read once and rejected forever.
  if (!stamp.haveMtime) return;
  for (auto& record : records) {
    if (record.file != file) continue;
    record.bytes = stamp.bytes;
    record.fatDate = stamp.fatDate;
    record.fatTime = stamp.fatTime;
    record.sha = sha;
    recordsDirty = true;
    return;
  }
  StoredRecord record;
  record.file = file;
  record.bytes = stamp.bytes;
  record.fatDate = stamp.fatDate;
  record.fatTime = stamp.fatTime;
  record.sha = sha;
  records.push_back(std::move(record));
  recordsDirty = true;
}

void LibraryUpdater::flushSyncRecords() {
  if (!recordsDirty) return;

  JsonDocument doc;
  doc["version"] = SYNC_RECORDS_VERSION;
  JsonArray array = doc["books"].to<JsonArray>();
  for (const auto& record : records) {
    JsonObject entry = array.add<JsonObject>();
    entry["f"] = record.file;
    entry["b"] = record.bytes;
    entry["d"] = record.fatDate;
    entry["t"] = record.fatTime;
    entry["s"] = record.sha;
  }
  std::string body;
  serializeJson(doc, body);

  HalFile file;
  if (!Storage.openFileForWrite("LIB", syncRecordsPath, file)) {
    // Leave recordsDirty set. A second sync in the same session must try again
    // rather than believe a write that never happened -- clearing the flag up
    // front made the retry a silent no-op.
    LOG_ERR("LIB", "Cannot write the sync ledger; the next run will hash again");
    return;
  }
  const size_t written = file.write(body.data(), body.size());
  file.close();
  if (written != body.size()) {
    LOG_ERR("LIB", "Sync ledger write was short (%u of %u bytes); the next run will hash again",
            static_cast<unsigned>(written), static_cast<unsigned>(body.size()));
    return;  // same reasoning: still dirty, and a short JSON file fails its own version check
  }
  recordsDirty = false;
  LOG_DBG("LIB", "Sync ledger written: %u records, %u bytes", static_cast<unsigned>(records.size()),
          static_cast<unsigned>(body.size()));
}

// What the card says about a book right now. Size and modification time come
// from the same open, so they describe one moment.
namespace {
librarysync::CardStamp stampOf(const std::string& path) {
  librarysync::CardStamp stamp;
  HalFile file;
  if (!Storage.openFileForRead("LIB", path, file)) return stamp;
  stamp.bytes = file.size();
  stamp.haveMtime = file.getModifyDateTime(&stamp.fatDate, &stamp.fatTime);
  file.close();
  return stamp;
}
}  // namespace

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

  loadSyncRecords();

  const std::string destPath = booksDir + book.file;
  const bool existed = Storage.exists(destPath.c_str());
  const librarysync::CardStamp stamp = existed ? stampOf(destPath) : librarysync::CardStamp{};

  if (librarysync::sizeVerdict(existed, stamp.bytes, book.bytes) == librarysync::SizeVerdict::CHECK_SHA) {
    const StoredRecord* stored = findRecord(book.file);
    librarysync::SyncRecord record;
    if (stored != nullptr) {
      record.present = true;
      record.bytes = stored->bytes;
      record.fatDate = stored->fatDate;
      record.fatTime = stored->fatTime;
      record.sha = stored->sha.c_str();
    }
    if (librarysync::hashVerdict(stamp, record, book.sha256.c_str()) == librarysync::HashVerdict::SKIP_HASH) {
      LOG_DBG("LIB", "Unchanged (size and mtime match the ledger, not read): %s", book.file.c_str());
      return BookResult::UNCHANGED;
    }
    char cardSha[65];
    if (computeCardSha256(destPath, cardSha) && librarysync::shaMatches(cardSha, book.sha256.c_str())) {
      // Record what was just proved, so the next run can pass this book over.
      putRecord(book.file, stamp, book.sha256);
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

  // The digest is known for certain here -- it was computed over the bytes as
  // they streamed in -- so the ledger records the file as it now sits, and the
  // next run reads no part of it.
  putRecord(book.file, stampOf(destPath), book.sha256);

  LOG_INF("LIB", "%s: %s", existed ? "Updated" : "Added", book.file.c_str());
  return existed ? BookResult::UPDATED : BookResult::ADDED;
}
