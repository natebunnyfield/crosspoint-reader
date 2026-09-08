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
#include "GithubAuth.h"
#include "GithubReleaseAssets.h"
#include "LibrarySyncPlan.h"
#include "RecentBooksStore.h"

namespace {

// THE CONTENT REPO, not the firmware repo: books come from the claude-tools
// library release, tagged library-latest and refreshed in place by its
// scripts/publish_library.py. The repo is private; see the class comment.
constexpr char libraryReleaseUrl[] =
    "https://api.github.com/repos/natebunnyfield/claude-tools/releases/tags/library-latest";
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
// The same folder without its trailing slash, for the directory calls: SdFat
// resolves "/books/" as a path to a file named "" inside /books.
constexpr char booksFolder[] = "/books";
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

// The one response held whole in RAM gets an explicit ceiling. Over it is an
// error, never an abort -- see B-053 and FontUpdater's copy of this constant.
constexpr size_t MAX_MANIFEST_BYTES = 64 * 1024;

// The token and the Authorization header come from network/GithubAuth.h, which
// is shared with FontUpdater: the fonts release is in the SAME private repo, so
// it is the same credential, read the same three ways, and NEVER logged.
constexpr const char* LOG_MODULE = "LIB";

void hexDigest(const unsigned char digest[32], char outHex[65]) {
  static const char* hex = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    outHex[i * 2] = hex[digest[i] >> 4];
    outHex[i * 2 + 1] = hex[digest[i] & 0x0F];
  }
  outHex[64] = '\0';
}

}  // namespace

LibraryUpdater::LibraryError LibraryUpdater::fetchManifest(StepCallback onStep, void* ctx) {
  if (githubauth::tokenValue(LOG_MODULE).empty()) {
    // Not an error to retry — the screen tells the owner where the token goes,
    // and on a host build that is the host's settings app rather than a file.
    return NO_TOKEN;
  }

  books.clear();

  const HttpDownloader::HeaderList apiHeaders = {
      {"Accept", "application/vnd.github+json"},
      {"Authorization", githubauth::bearerHeaderValue(LOG_MODULE)},
  };

  auto parser = makeUniqueNoThrow<GithubReleaseAssetParser>();
  if (!parser) {
    LOG_ERR("LIB", "OOM: release JSON parser");
    return OOM_ERROR;
  }
  GithubReleaseAssetParser& releaseParser = *parser;
  if (onStep) onStep(ctx, CheckStep::CONTACTING);
  const HttpDownloader::DownloadError fetched = HttpDownloader::fetchUrlWithHeaders(
      libraryReleaseUrl, apiHeaders, [&releaseParser](const uint8_t* data, size_t len) {
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
    const HttpDownloader::DownloadError repoSeen =
        HttpDownloader::fetchUrlWithHeaders(libraryRepoUrl, apiHeaders, [&repoBytes](const uint8_t*, size_t len) {
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

  const GithubReleaseAssetParser::Asset* manifestAsset = nullptr;
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
      {"Authorization", githubauth::bearerHeaderValue(LOG_MODULE)},
  };

  if (onStep) onStep(ctx, CheckStep::READING);
  // Same treatment as the fonts manifest, and for the same reason: this had the
  // identical unguarded std::string accumulator. It has not crashed only
  // because a book manifest is smaller than a font one -- luck, not safety.
  // B-053.
  HttpDownloader::Body manifestBody;
  const HttpDownloader::DownloadError manifestFetched =
      HttpDownloader::fetchUrlToBuffer(manifestAsset->url, assetHeaders, MAX_MANIFEST_BYTES, manifestBody);
  if (manifestFetched == HttpDownloader::TOO_LARGE || manifestFetched == HttpDownloader::OUT_OF_MEMORY) {
    LOG_ERR("LIB", "Manifest does not fit in %u bytes of RAM", static_cast<unsigned>(MAX_MANIFEST_BYTES));
    return OOM_ERROR;
  }
  if (manifestFetched != HttpDownloader::OK) {
    LOG_ERR("LIB", "Manifest fetch failed");
    return HTTP_ERROR;
  }

  JsonDocument doc;
  if (deserializeJson(doc, manifestBody.c_str(), manifestBody.len) != DeserializationError::Ok) {
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
  // NOT a std::string: resize() reaches operator new, which is not nothrow
  // under -fno-exceptions, so a refusal on a spent heap aborts instead of
  // degrading. That is B-053, and this is the same mechanism on the ledger's
  // read path. Failing here costs one hashing pass and nothing else.
  auto body = makeUniqueNoThrow<char[]>(ledgerBytes + 1);
  if (!body) {
    LOG_ERR("LIB", "No memory for the library ledger; hashing everything once");
    file.close();
    return;
  }
  if (ledgerBytes) file.read(body.get(), ledgerBytes);
  body[ledgerBytes] = '\0';
  file.close();

  JsonDocument doc;
  if (deserializeJson(doc, body.get(), ledgerBytes) != DeserializationError::Ok) {
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
  // Same reason as the read path above, and this one is worse: the ledger for
  // 78 files measures ~11 KB, and libstdc++'s doubling needed 15,361 bytes new
  // while still holding 7,680 -- 23,041 across two blocks, within a byte of the
  // 22,049 this device is measured refusing (B-053). measureJson gives the
  // exact size, so one nothrow block does it with no growth at all.
  const size_t bodyLen = measureJson(doc);
  auto body = makeUniqueNoThrow<char[]>(bodyLen + 1);
  if (!body) {
    // Leave recordsDirty set: a later run must try again rather than believe a
    // write that never happened.
    LOG_ERR("LIB", "No memory to serialize the library ledger; the next run will hash again");
    return;
  }
  serializeJson(doc, body.get(), bodyLen + 1);

  HalFile file;
  if (!Storage.openFileForWrite("LIB", syncRecordsPath, file)) {
    // Leave recordsDirty set. A second sync in the same session must try again
    // rather than believe a write that never happened -- clearing the flag up
    // front made the retry a silent no-op.
    LOG_ERR("LIB", "Cannot write the sync ledger; the next run will hash again");
    return;
  }
  const size_t written = file.write(body.get(), bodyLen);
  file.close();
  if (written != bodyLen) {
    LOG_ERR("LIB", "Sync ledger write was short (%u of %u bytes); the next run will hash again",
            static_cast<unsigned>(written), static_cast<unsigned>(bodyLen));
    return;  // same reasoning: still dirty, and a short JSON file fails its own version check
  }
  recordsDirty = false;
  LOG_DBG("LIB", "Sync ledger written: %u records, %u bytes", static_cast<unsigned>(records.size()),
          static_cast<unsigned>(bodyLen));
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
  lastFailure_ = librarysync::FailureKind::NONE;
  if (index >= books.size()) return BookResult::FAILED;
  const Book& book = books[index];

  // Reset the per-book counters HERE, before the compare stage, not at the
  // download. The activity repaints the whole-sync bar the moment it moves
  // currentBook on, and it computes this book's share from these two -- so
  // while this book was still being stamped and sha-checked (seconds on a
  // large epub) the bar showed the PREVIOUS book's final 100%, one book
  // ahead of the truth, then fell back when the download started. A two-book
  // sync read 50 -> 100 -> 50 -> 100 (adversarial review 2026-09-04).
  resetBookProgress();

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

  // NOTHING ELSE CREATES /books. A card that has never held a synced book --
  // one loaded by hand, with its epubs at the root or in folders of the
  // owner's own naming -- has no such folder, and every .part open below
  // fails on it while the manifest check two screens earlier passes: every
  // book an error, and the network blameless. That is the reading of the
  // owner's "Update Library results in 22 (all) errors" (2026-09-06, B-048)
  // that the code supports; the device's log ring held only Wi-Fi lines by
  // then, so it is not proven. Made here, on the first download, rather
  // than at boot: a card that never syncs should not grow an empty folder.
  if (!Storage.ensureDirectoryExists(booksFolder)) {
    LOG_ERR("LIB", "Cannot create %s on the card", booksFolder);
    lastFailure_ = librarysync::FailureKind::STORAGE;
    return BookResult::FAILED;
  }

  HalFile out;
  if (!Storage.openFileForWrite("LIB", partPath, out)) {
    LOG_ERR("LIB", "Cannot open %s for write", partPath.c_str());
    lastFailure_ = librarysync::FailureKind::STORAGE;
    return BookResult::FAILED;
  }

  const HttpDownloader::HeaderList assetHeaders = {
      {"Accept", "application/octet-stream"},
      {"Authorization", githubauth::bearerHeaderValue(LOG_MODULE)},
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

  // Three different failures used to share one line and one verdict. They
  // are told apart now because each sends the owner somewhere else: a write
  // that failed is the card, a fetch that failed is the network, and bytes
  // that arrived but do not match are the manifest.
  if (!writeOk) {
    LOG_ERR("LIB", "Write of %s failed after %u bytes", partPath.c_str(), static_cast<unsigned>(processedSize));
    Storage.remove(partPath.c_str());
    lastFailure_ = librarysync::FailureKind::STORAGE;
    return BookResult::FAILED;
  }
  if (fetched != HttpDownloader::OK) {
    LOG_ERR("LIB", "Download of %s failed (%d) after %u of %u bytes", book.file.c_str(), static_cast<int>(fetched),
            static_cast<unsigned>(processedSize), static_cast<unsigned>(book.bytes));
    Storage.remove(partPath.c_str());
    lastFailure_ = librarysync::FailureKind::NETWORK;
    return BookResult::FAILED;
  }
  if (processedSize != book.bytes || !librarysync::shaMatches(gotSha, book.sha256.c_str())) {
    LOG_ERR("LIB", "Download of %s failed verification (%u of %u bytes)", book.file.c_str(),
            static_cast<unsigned>(processedSize), static_cast<unsigned>(book.bytes));
    Storage.remove(partPath.c_str());
    lastFailure_ = librarysync::FailureKind::VERIFY;
    return BookResult::FAILED;
  }

  if (existed) Storage.remove(destPath.c_str());
  if (!Storage.rename(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("LIB", "Rename into place failed: %s", destPath.c_str());
    Storage.remove(partPath.c_str());
    lastFailure_ = librarysync::FailureKind::STORAGE;
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

    // The on-disk cache above is one of two caches keyed by this book's path.
    // The other is RecentBooksStore's JSON ledger: if this book was ever opened
    // and its cover previously failed to generate (no cover.png existed yet, or
    // an undecodable format like SVG), generateThumbBmp() wrote coverBmpPath =
    // "" there, and nothing except re-opening the book would ever try again --
    // a Library sync that finally gives the book a working cover had no effect
    // on the Home screen's Recent grid until the book was opened once more.
    // Confirmed live 2026-09-02: Tico Spanish's cover only appeared after it
    // was manually opened, on a device where the synced file already had a
    // correct cover.png. Un-sticking the sentinel here removes that manual
    // step, matching what opening the book already does (a fresh addBook()
    // call with a non-empty templated path).
    RECENT_BOOKS.resetCoverForPath(destPath, cachePath + "/thumb_[HEIGHT].bmp");
  }

  // The digest is known for certain here -- it was computed over the bytes as
  // they streamed in -- so the ledger records the file as it now sits, and the
  // next run reads no part of it.
  putRecord(book.file, stampOf(destPath), book.sha256);

  LOG_INF("LIB", "%s: %s", existed ? "Updated" : "Added", book.file.c_str());
  return existed ? BookResult::UPDATED : BookResult::ADDED;
}
