#include "FontUpdater.h"

#include <algorithm>

// clang-format off
// Same include-order constraint as OtaUpdater.cpp / LibraryUpdater.cpp:
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first.
#include "HttpDownloader.h"
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SdCardFontRegistry.h>
#include <mbedtls/sha256.h>
// clang-format on

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontSyncPlan.h"
#include "GithubAuth.h"
#include "GithubReleaseAssets.h"
#include "SdCardFontSystem.h"

namespace {

// THE CONTENT REPO, not the firmware repo, and not the public crosspoint-fonts
// repo either: these bitmaps come from the claude-tools fonts release, tagged
// fonts-latest and refreshed in place by its scripts/publish_fonts.py. Private,
// because five of the twelve families are built from licensed outlines. See the
// class comment.
constexpr char fontsReleaseUrl[] =
    "https://api.github.com/repos/natebunnyfield/claude-tools/releases/tags/fonts-latest";
constexpr char manifestAssetName[] = "manifest.json";
// The repo itself, probed ONLY to disambiguate a 404 on the release above --
// GitHub answers 404 for a private repo whether the release is missing or the
// token cannot see it. Same reasoning, at length, in LibraryUpdater.cpp.
constexpr char fontsRepoUrl[] = "https://api.github.com/repos/natebunnyfield/claude-tools";

constexpr size_t SHA_CHUNK = 1024;

// The ledger of "what this .cpfont looked like when its digest was last
// checked". Beside the reader's other state, and beside library_sync.json,
// because it is this firmware's bookkeeping.
constexpr char syncRecordsPath[] = "/.crosspoint/font_sync.json";
// Bump when the shape of a record changes. A file at any other version is
// IGNORED WHOLE, which costs one hashing pass and can never mis-skip.
constexpr int SYNC_RECORDS_VERSION = 1;
// The highest manifest.json this build knows how to act on. publish_fonts.py
// stamps "version": 1 today.
constexpr int MAX_MANIFEST_VERSION = 1;

// The manifest is the one response held whole in RAM, so it gets an explicit
// ceiling. It is a BACKSTOP against a hostile or corrupt response, NOT headroom:
// the heap gives out long before the cap does. This device refused a
// 22,049-byte contiguous block with Wi-Fi and wolfSSL resident (B-053, measured
// off the crash stack), so the real ceiling is whatever contiguous run a spent
// ESP32-C3 heap will hand over -- empirically somewhere under ~22 KB, which at
// the measured ~1.4 KB a family is roughly fourteen to sixteen families, not
// the forty-five this number would suggest. Do not read it as a family budget.
// Over the cap is an error, never an abort.
constexpr size_t MAX_MANIFEST_BYTES = 64 * 1024;

// The cache root the layout caches live under: /.crosspoint/epub_<hash>/sections.
constexpr char cacheRoot[] = "/.crosspoint";
constexpr char cacheDirPrefix[] = "epub_";

// The device offers at most this many SD families anyway
// (SdCardFontRegistry::MAX_SD_FAMILIES), and the manifest comes off the
// network: a hostile or corrupt response must not be able to grow these
// vectors until the ~380 KB heap is gone. Same reasoning as the kMaxAssets cap
// in GithubReleaseAssets.h. A family ships six cuts; sixteen is generous.
constexpr size_t MAX_FAMILIES = 128;
constexpr size_t MAX_FILES_PER_FAMILY = 16;

constexpr const char* LOG_MODULE = "FONTUPD";

// Eight lines, deliberately not shared with LibraryUpdater.cpp's copy. Unlike
// the token (a credential contract) and the release parser (200 lines of state
// machine), this is a well-known conversion with nothing to get subtly wrong,
// and a header for it would couple two updaters for no protection.
void hexDigest(const unsigned char digest[32], char outHex[65]) {
  static const char* hex = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    outHex[i * 2] = hex[digest[i] >> 4];
    outHex[i * 2 + 1] = hex[digest[i] & 0x0F];
  }
  outHex[64] = '\0';
}

// "<root>/<Family>", "<root>/.<Family>.part", "<root>/.<Family>.old".
//
// The two staging names begin with a DOT on purpose, and it is load-bearing
// rather than cosmetic: SdCardFontRegistry::scanRoot skips every directory
// whose name starts with '.' or '_' (SdCardFontRegistry.cpp:180, there for
// macOS ._* and .Trashes). So a family mid-download, and the previous copy
// while the swap is in flight, are both INVISIBLE to the picker. Nothing else
// in the system needs to know they exist.
std::string installDir(const char* root, const std::string& name) { return std::string(root) + "/" + name; }
std::string stageDirFor(const char* root, const std::string& name) { return std::string(root) + "/." + name + ".part"; }
std::string backupDirFor(const char* root, const std::string& name) { return std::string(root) + "/." + name + ".old"; }

// What the card says about a file right now. Size and modification time come
// from the same open, so they describe one moment.
fontsync::CardStamp stampOf(const std::string& path) {
  fontsync::CardStamp stamp;
  HalFile file;
  if (!Storage.openFileForRead(LOG_MODULE, path, file)) return stamp;
  stamp.bytes = file.size();
  stamp.haveMtime = file.getModifyDateTime(&stamp.fatDate, &stamp.fatTime);
  file.close();
  return stamp;
}

}  // namespace

FontUpdater::FontError FontUpdater::fetchManifest(StepCallback onStep, void* ctx) {
  if (githubauth::tokenValue(LOG_MODULE).empty()) {
    // Not an error to retry -- the screen tells the owner where the token goes,
    // and on a host build that is the host's settings app rather than a file.
    return NO_TOKEN;
  }

  // Cleared FIRST and set only on the OK return at the bottom, so every early
  // return below -- no token, no network, 404, bad JSON, too new -- leaves it
  // false and removeUnlistedFamilies() refuses to touch the card.
  manifestOk_ = false;
  families.clear();

  const HttpDownloader::HeaderList apiHeaders = {
      {"Accept", "application/vnd.github+json"},
      {"Authorization", githubauth::bearerHeaderValue(LOG_MODULE)},
  };

  auto parser = makeUniqueNoThrow<GithubReleaseAssetParser>();
  if (!parser) {
    LOG_ERR(LOG_MODULE, "OOM: release JSON parser");
    return OOM_ERROR;
  }
  GithubReleaseAssetParser& releaseParser = *parser;
  if (onStep) onStep(ctx, CheckStep::CONTACTING);
  const HttpDownloader::DownloadError fetched = HttpDownloader::fetchUrlWithHeaders(
      fontsReleaseUrl, apiHeaders, [&releaseParser](const uint8_t* data, size_t len) {
        releaseParser.feed(reinterpret_cast<const char*>(data), len);
        return true;
      });
  if (fetched == HttpDownloader::UNAUTHORIZED) {
    LOG_ERR(LOG_MODULE, "GitHub rejected the token (401/403)");
    return BAD_TOKEN;
  }
  if (fetched == HttpDownloader::NOT_FOUND) {
    // A 404 here is AMBIGUOUS: missing release, or a token that cannot see the
    // repo. One extra request, only on the failure path, separates them.
    const HttpDownloader::DownloadError repoSeen =
        HttpDownloader::fetchUrlWithHeaders(fontsRepoUrl, apiHeaders, [](const uint8_t*, size_t) { return true; });
    if (repoSeen == HttpDownloader::OK) {
      LOG_DBG(LOG_MODULE, "Repo is visible; the fonts release really is absent");
      return NO_RELEASE;
    }
    LOG_ERR(LOG_MODULE, "The token cannot see %s (release 404, repo %s)", fontsRepoUrl,
            repoSeen == HttpDownloader::NOT_FOUND ? "404" : "unreachable");
    return NO_REPO_ACCESS;
  }
  if (fetched != HttpDownloader::OK) {
    LOG_ERR(LOG_MODULE, "Release fetch failed");
    return HTTP_ERROR;
  }
  if (releaseParser.hasError()) {
    LOG_ERR(LOG_MODULE, "Release JSON did not parse");
    return JSON_PARSE_ERROR;
  }

  const auto& assets = releaseParser.getAssets();
  LOG_DBG(LOG_MODULE, "Release lists %u assets", static_cast<unsigned>(assets.size()));

  const GithubReleaseAssetParser::Asset* manifestAsset = nullptr;
  for (const auto& asset : assets) {
    if (asset.name == manifestAssetName) {
      manifestAsset = &asset;
      break;
    }
  }
  if (!manifestAsset) {
    LOG_ERR(LOG_MODULE, "Release has no %s asset", manifestAssetName);
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
  // ONE nothrow block, sized from Content-Length. This used to accumulate into
  // a std::string, whose doubling growth reached operator new -- not nothrow
  // under -fno-exceptions -- and terminated the device on the step from 11024
  // to 22048 bytes while reading an 18 KB manifest. B-053.
  HttpDownloader::Body manifestBody;
  const HttpDownloader::DownloadError manifestFetched =
      HttpDownloader::fetchUrlToBuffer(manifestAsset->url, assetHeaders, MAX_MANIFEST_BYTES, manifestBody);
  if (manifestFetched == HttpDownloader::TOO_LARGE || manifestFetched == HttpDownloader::OUT_OF_MEMORY) {
    LOG_ERR(LOG_MODULE, "Manifest does not fit in %u bytes of RAM", static_cast<unsigned>(MAX_MANIFEST_BYTES));
    return OOM_ERROR;
  }
  if (manifestFetched != HttpDownloader::OK) {
    LOG_ERR(LOG_MODULE, "Manifest fetch failed");
    return HTTP_ERROR;
  }

  JsonDocument doc;
  if (deserializeJson(doc, manifestBody.c_str(), manifestBody.len) != DeserializationError::Ok) {
    LOG_ERR(LOG_MODULE, "Manifest JSON did not parse");
    return JSON_PARSE_ERROR;
  }
  manifestBody.reset();

  const int manifestVersion = doc["version"] | 1;
  if (manifestVersion > MAX_MANIFEST_VERSION) {
    LOG_ERR(LOG_MODULE, "Manifest is version %d; this firmware understands %d", manifestVersion, MAX_MANIFEST_VERSION);
    return MANIFEST_TOO_NEW;
  }

  JsonArrayConst manifestFamilies = doc["families"].as<JsonArrayConst>();
  if (manifestFamilies.isNull()) {
    LOG_ERR(LOG_MODULE, "Manifest has no families array");
    return JSON_PARSE_ERROR;
  }

  families.reserve(manifestFamilies.size() < MAX_FAMILIES ? manifestFamilies.size() : MAX_FAMILIES);
  for (JsonObjectConst entry : manifestFamilies) {
    if (families.size() >= MAX_FAMILIES) break;
    Family family;
    family.name = entry["family"] | "";
    if (!fontsync::isSafeFamilyName(family.name.c_str())) {
      LOG_ERR(LOG_MODULE, "Skipping malformed family name in the manifest");
      continue;
    }

    JsonArrayConst entryFiles = entry["files"].as<JsonArrayConst>();
    if (entryFiles.isNull()) {
      LOG_ERR(LOG_MODULE, "%s: manifest entry has no files array", family.name.c_str());
      continue;
    }
    family.files.reserve(entryFiles.size() < MAX_FILES_PER_FAMILY ? entryFiles.size() : MAX_FILES_PER_FAMILY);

    // ANY malformed or unmatched file DROPS THE WHOLE FAMILY, rather than
    // installing the rest of it. That is the same all-or-nothing rule
    // fontsync::commitVerdict enforces later, applied at parse time: a family
    // built from five of the six entries the publisher wrote is precisely the
    // broken state this feature exists to prevent, and dropping it means the
    // run reports "unchanged" for a family it could not understand instead of
    // shipping a hole.
    bool familyOk = true;
    // NO TWO ENTRIES MAY CLAIM THE SAME POINT SIZE. Two shapes reach here and
    // neither is catchable by validating one name at a time: a literal repeat
    // of the same `file` (openFileForWrite is O_TRUNC, so the second download
    // rewrites the first's path and both "verify", and the commit gate counts
    // two), and two different names whose sizes collide once discovery has
    // parsed them -- scanDirectory then drops the second as a duplicate
    // (SdCardFontRegistry.cpp:138-148) and the family lands one cut short with
    // every count saying otherwise. fontFileSize already refuses leading
    // zeros, the easiest form of the second shape; this closes the general
    // case. Adversarial review, 2026-09-07: commitVerdict's own comment
    // worried about "a fifth somehow counted twice", and a duplicated manifest
    // entry is exactly how that happens.
    //
    // A linear rescan rather than a seen[256] table: a family holds at most
    // MAX_FILES_PER_FAMILY entries, so this is at worst 120 short-string
    // parses, and the table would be 256 bytes of stack against the Resource
    // Protocol's 256-byte ceiling for one function's locals.
    for (JsonObjectConst fileEntry : entryFiles) {
      if (family.files.size() >= MAX_FILES_PER_FAMILY) {
        familyOk = false;
        break;
      }
      FontFile file;
      file.file = fileEntry["file"] | "";
      file.bytes = fileEntry["bytes"] | 0;
      file.sha256 = fileEntry["sha256"] | "";
      const char* assetName = fileEntry["asset"] | "";
      // isSafeFontFileName is strict about the "<Family>_<size>.cpfont" shape
      // because SdCardFontRegistry parses exactly that; a differently-named
      // file would be written, verified, counted -- and then ignored by
      // discovery. See FontSyncPlan.h.
      const uint8_t pointSize = fontsync::fontFileSize(family.name.c_str(), file.file.c_str());
      if (pointSize == 0 || file.sha256.size() != 64 || file.bytes == 0) {
        LOG_ERR(LOG_MODULE, "%s: malformed manifest file entry", family.name.c_str());
        familyOk = false;
        break;
      }
      bool duplicateSize = false;
      for (const auto& already : family.files) {
        if (fontsync::fontFileSize(family.name.c_str(), already.file.c_str()) == pointSize) {
          duplicateSize = true;
          break;
        }
      }
      if (duplicateSize) {
        LOG_ERR(LOG_MODULE, "%s: two manifest entries claim %u pt", family.name.c_str(),
                static_cast<unsigned>(pointSize));
        familyOk = false;
        break;
      }
      for (const auto& asset : assets) {
        if (asset.name == assetName) {
          file.url = asset.url;
          break;
        }
      }
      if (file.url.empty()) {
        // The manifest promises a file the release does not carry: the
        // publisher uploads them together, so this is a half-updated release.
        LOG_ERR(LOG_MODULE, "%s: no release asset for %s", family.name.c_str(), file.file.c_str());
        familyOk = false;
        break;
      }
      family.files.push_back(std::move(file));
    }

    if (!familyOk || family.files.empty()) {
      LOG_ERR(LOG_MODULE, "Skipping %s entirely -- a family installs whole or not at all", family.name.c_str());
      continue;
    }
    families.push_back(std::move(family));
  }

  // The asset list is ~13 KB of strings on a full release and every url worth
  // keeping has already been copied. Free it before the sync starts rather than
  // holding it for the whole run beside the downloads' own buffers.
  parser.reset();

  LOG_INF(LOG_MODULE, "Manifest lists %u families", static_cast<unsigned>(families.size()));
  manifestOk_ = true;
  return OK;
}

// --- the ledger -------------------------------------------------------------

void FontUpdater::loadSyncRecords() {
  if (recordsLoaded) return;
  recordsLoaded = true;  // set FIRST: a missing or unreadable file means "no records", and
                         // retrying the read per family would cost a card open each time.

  HalFile file;
  if (!Storage.openFileForRead(LOG_MODULE, syncRecordsPath, file)) {
    LOG_DBG(LOG_MODULE, "No font ledger yet; every file will be hashed once");
    return;
  }
  // A corrupt directory entry can report any size at all, and this allocates
  // exactly what it is told. 64 KB is far more than 12 families x 6 files ever
  // needs; a file past it is not a ledger, and reading none only ever costs a
  // hashing pass.
  constexpr size_t MAX_LEDGER_BYTES = 64 * 1024;
  const size_t ledgerBytes = file.size();
  if (ledgerBytes > MAX_LEDGER_BYTES) {
    LOG_ERR(LOG_MODULE, "Font ledger is %u bytes; ignoring it", static_cast<unsigned>(ledgerBytes));
    file.close();
    return;
  }
  // NOT a std::string: resize() reaches operator new, which is not nothrow
  // under -fno-exceptions, so a refusal on a spent heap aborts instead of
  // degrading. That is B-053, and this is the same mechanism on the ledger's
  // read path. Failing here costs one hashing pass and nothing else.
  auto body = makeUniqueNoThrow<char[]>(ledgerBytes + 1);
  if (!body) {
    LOG_ERR(LOG_MODULE, "No memory for the font ledger; hashing everything once");
    file.close();
    return;
  }
  if (ledgerBytes) file.read(body.get(), ledgerBytes);
  body[ledgerBytes] = '\0';
  file.close();

  JsonDocument doc;
  if (deserializeJson(doc, body.get(), ledgerBytes) != DeserializationError::Ok) {
    LOG_ERR(LOG_MODULE, "Font ledger did not parse; hashing everything once");
    return;
  }
  const int version = doc["version"] | 0;
  if (version != SYNC_RECORDS_VERSION) {
    LOG_INF(LOG_MODULE, "Font ledger is version %d, not %d; hashing everything once", version, SYNC_RECORDS_VERSION);
    return;
  }
  const JsonArrayConst ledgerFiles = doc["files"].as<JsonArrayConst>();
  // Resource Protocol 7: one allocation, not log2(N) reallocs. On a FIRST run
  // there is no ledger, so ledgerFiles is empty and this reserved nothing --
  // then 78 push_backs doubled to a capacity-128 vector, a 7,168-byte
  // contiguous operator new on an already-spent heap. Floor it at the number a
  // real card actually holds.
  records.reserve(std::max<size_t>(ledgerFiles.size(), 96));
  for (JsonObjectConst entry : ledgerFiles) {
    StoredRecord record;
    record.key = entry["f"] | "";
    record.sha = entry["s"] | "";
    record.bytes = entry["b"] | 0;
    record.fatDate = static_cast<uint16_t>(entry["d"] | 0);
    record.fatTime = static_cast<uint16_t>(entry["t"] | 0);
    if (record.key.empty() || record.sha.size() != 64) continue;
    records.push_back(std::move(record));
  }
  LOG_DBG(LOG_MODULE, "Font ledger: %u records", static_cast<unsigned>(records.size()));
}

const FontUpdater::StoredRecord* FontUpdater::findRecord(const std::string& key) const {
  for (const auto& record : records) {
    if (record.key == key) return &record;
  }
  return nullptr;
}

void FontUpdater::putRecord(const std::string& key, const fontsync::CardStamp& stamp, const std::string& sha) {
  // No mtime means nothing worth recording: hashVerdict refuses to skip without
  // one, so a record carrying a zero would be read once and rejected forever.
  if (!stamp.haveMtime) return;
  for (auto& record : records) {
    if (record.key != key) continue;
    record.bytes = stamp.bytes;
    record.fatDate = stamp.fatDate;
    record.fatTime = stamp.fatTime;
    record.sha = sha;
    recordsDirty = true;
    return;
  }
  StoredRecord record;
  record.key = key;
  record.bytes = stamp.bytes;
  record.fatDate = stamp.fatDate;
  record.fatTime = stamp.fatTime;
  record.sha = sha;
  records.push_back(std::move(record));
  recordsDirty = true;
}

void FontUpdater::flushSyncRecords() {
  if (!recordsDirty) return;

  // DROP RECORDS FOR FILES THE MANIFEST NO LONGER LISTS. putRecord only ever
  // adds or updates, so without this the ledger grows monotonically across
  // every family that was ever published -- and past MAX_LEDGER_BYTES
  // loadSyncRecords ignores the whole file, silently reverting to hashing ~80
  // MB on every run. That is a slow, invisible regression rather than an
  // error. Adversarial review, 2026-09-07.
  //
  // Safe only because this runs at the END of a complete run: finishRun() is
  // reached solely from the tick after the last family, so `families` is the
  // whole manifest, never a partial view. The guard makes that explicit rather
  // than relying on it -- an empty manifest prunes nothing.
  if (!families.empty()) {
    const size_t before = records.size();
    std::vector<StoredRecord> kept;
    kept.reserve(records.size());
    for (auto& record : records) {
      for (const auto& family : families) {
        bool listed = false;
        for (const auto& file : family.files) {
          if (record.key.size() == family.name.size() + 1 + file.file.size() &&
              record.key.compare(0, family.name.size(), family.name) == 0 && record.key[family.name.size()] == '/' &&
              record.key.compare(family.name.size() + 1, std::string::npos, file.file) == 0) {
            listed = true;
            break;
          }
        }
        if (listed) {
          kept.push_back(std::move(record));
          break;
        }
      }
    }
    if (kept.size() != before) {
      LOG_DBG(LOG_MODULE, "Font ledger: dropped %u records the manifest no longer lists",
              static_cast<unsigned>(before - kept.size()));
    }
    records = std::move(kept);
  }

  JsonDocument doc;
  doc["version"] = SYNC_RECORDS_VERSION;
  JsonArray array = doc["files"].to<JsonArray>();
  for (const auto& record : records) {
    JsonObject entry = array.add<JsonObject>();
    entry["f"] = record.key;
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
    LOG_ERR(LOG_MODULE, "No memory to serialize the font ledger; the next run will hash again");
    return;
  }
  serializeJson(doc, body.get(), bodyLen + 1);

  HalFile file;
  if (!Storage.openFileForWrite(LOG_MODULE, syncRecordsPath, file)) {
    // Leave recordsDirty set, same as the library ledger: a second run in the
    // same session must try again rather than believe a write that never
    // happened.
    LOG_ERR(LOG_MODULE, "Cannot write the font ledger; the next run will hash again");
    return;
  }
  const size_t written = file.write(body.get(), bodyLen);
  file.close();
  if (written != bodyLen) {
    LOG_ERR(LOG_MODULE, "Font ledger write was short (%u of %u bytes); the next run will hash again",
            static_cast<unsigned>(written), static_cast<unsigned>(bodyLen));
    return;  // still dirty, and a short JSON file fails its own version check
  }
  recordsDirty = false;
  LOG_DBG(LOG_MODULE, "Font ledger written: %u records", static_cast<unsigned>(records.size()));
}

bool FontUpdater::computeCardSha256(const std::string& path, char outHex[65]) {
  HalFile file;
  if (!Storage.openFileForRead(LOG_MODULE, path, file)) return false;

  auto buf = makeUniqueNoThrow<uint8_t[]>(SHA_CHUNK);
  if (!buf) {
    LOG_ERR(LOG_MODULE, "OOM: sha256 read buffer");
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

// --- the sync ---------------------------------------------------------------

size_t FontUpdater::countMatchingFiles(const Family& family, const char* root) {
  const std::string dir = installDir(root, family.name);
  size_t matching = 0;
  for (const auto& file : family.files) {
    const std::string path = dir + "/" + file.file;
    const bool exists = Storage.exists(path.c_str());
    const fontsync::CardStamp stamp = exists ? stampOf(path) : fontsync::CardStamp{};
    if (fontsync::fileVerdict(exists, stamp.bytes, file.bytes) != fontsync::FileVerdict::CHECK_SHA) continue;

    const std::string key = family.name + "/" + file.file;
    const StoredRecord* stored = findRecord(key);
    fontsync::SyncRecord record;
    if (stored != nullptr) {
      record.present = true;
      record.bytes = stored->bytes;
      record.fatDate = stored->fatDate;
      record.fatTime = stored->fatTime;
      record.sha = stored->sha.c_str();
    }
    if (fontsync::hashVerdict(stamp, record, file.sha256.c_str()) == fontsync::HashVerdict::SKIP_HASH) {
      ++matching;
      continue;
    }
    char cardSha[65];
    if (computeCardSha256(path, cardSha) && fontsync::shaMatches(cardSha, file.sha256.c_str())) {
      // Record what was just proved, so the next run passes this file over.
      putRecord(key, stamp, file.sha256);
      ++matching;
    }
  }
  return matching;
}

void FontUpdater::recoverStaleStaging(const char* root, const std::string& name) {
  const std::string dest = installDir(root, name);
  const std::string backup = backupDirFor(root, name);
  const std::string stage = stageDirFor(root, name);

  // A run that died BETWEEN the two commit renames left the family absent and
  // its previous copy in .old. Put it back before anything else looks at the
  // card: that is what makes "absent" a self-healing state rather than a lost
  // font. This is the only reason the backup is a rename rather than a delete.
  if (Storage.exists(backup.c_str())) {
    if (!Storage.exists(dest.c_str())) {
      if (Storage.rename(backup.c_str(), dest.c_str())) {
        LOG_INF(LOG_MODULE, "Restored %s from an interrupted install", name.c_str());
      } else {
        LOG_ERR(LOG_MODULE, "Could not restore %s from %s", name.c_str(), backup.c_str());
      }
    } else {
      // Both present: the previous run committed and died before the cleanup.
      Storage.removeDir(backup.c_str());
    }
  }
  // Whatever is in a staging directory from a previous run is a partial
  // download with no way to know how partial. Start over.
  if (Storage.exists(stage.c_str())) {
    LOG_DBG(LOG_MODULE, "Discarding an abandoned staging dir for %s", name.c_str());
    Storage.removeDir(stage.c_str());
  }
}

size_t FontUpdater::stageFamily(const Family& family, const std::string& stageDir, bool& failed,
                                ProgressCallback onProgress, void* ctx) {
  const HttpDownloader::HeaderList assetHeaders = {
      {"Accept", "application/octet-stream"},
      {"Authorization", githubauth::bearerHeaderValue(LOG_MODULE)},
  };

  size_t verified = 0;
  for (size_t i = 0; i < family.files.size(); ++i) {
    const FontFile& file = family.files[i];
    const std::string path = stageDir + "/" + file.file;

    HalFile out;
    if (!Storage.openFileForWrite(LOG_MODULE, path, out)) {
      LOG_ERR(LOG_MODULE, "Cannot open %s for write", path.c_str());
      lastFailure_ = fontsync::FailureKind::STORAGE;
      failed = true;
      return verified;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, /*is224=*/0);

    currentFile = i;
    processedSize = 0;
    totalSize = file.bytes;
    int lastReportedPct = -1;
    bool writeOk = true;
    const HttpDownloader::DownloadError fetched =
        HttpDownloader::fetchUrlWithHeaders(file.url, assetHeaders, [&](const uint8_t* data, size_t len) {
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

    // Three failures, three verdicts, because each sends the owner somewhere
    // else: a write that failed is the card, a fetch that failed is the
    // network, bytes that arrived and do not match are the manifest.
    if (!writeOk) {
      LOG_ERR(LOG_MODULE, "Write of %s failed after %u bytes", path.c_str(), static_cast<unsigned>(processedSize));
      lastFailure_ = fontsync::FailureKind::STORAGE;
      failed = true;
      return verified;
    }
    if (fetched != HttpDownloader::OK) {
      LOG_ERR(LOG_MODULE, "Download of %s failed (%d) after %u of %u bytes", file.file.c_str(),
              static_cast<int>(fetched), static_cast<unsigned>(processedSize), static_cast<unsigned>(file.bytes));
      lastFailure_ = fontsync::FailureKind::NETWORK;
      failed = true;
      return verified;
    }
    if (processedSize != file.bytes || !fontsync::shaMatches(gotSha, file.sha256.c_str())) {
      LOG_ERR(LOG_MODULE, "Download of %s failed verification (%u of %u bytes)", file.file.c_str(),
              static_cast<unsigned>(processedSize), static_cast<unsigned>(file.bytes));
      lastFailure_ = fontsync::FailureKind::VERIFY;
      failed = true;
      return verified;
    }
    ++verified;
    currentFile = verified;
  }
  return verified;
}

FontUpdater::FamilyResult FontUpdater::syncFamily(size_t index, ProgressCallback onProgress, void* ctx) {
  lastFailure_ = fontsync::FailureKind::NONE;
  if (index >= families.size()) return FamilyResult::FAILED;
  const Family& family = families[index];

  // Reset the per-family counters HERE, before the compare stage, not at the
  // first download: the activity computes this family's share from them and
  // would otherwise show the PREVIOUS family's final 100% while this one is
  // still being stamped and sha-checked. Same defect LibraryUpdater fixed in
  // the 2026-09-04 review.
  resetFamilyProgress();
  fileCount = family.files.size();

  loadSyncRecords();

  // The family's OWN root if it is installed, else the default write root --
  // exactly FontInstaller::ensureFamilyDir's choice (FontInstaller.cpp:60-63).
  // Writing to the other root would be silently inert: /.fonts is scanned
  // first and wins on a name collision (SdCardFontRegistry.cpp:210-214), so a
  // "new" copy in /fonts under a family already in /.fonts would never be read
  // and the update would look like it did nothing.
  //
  // RECOVERY RUNS FIRST, AND OVER BOTH ROOTS. A family left absent by a crash
  // between the two commit renames has no install directory, so findFamilyRoot
  // would answer nullptr and the root chosen for recovery would be the DEFAULT
  // one -- which is the hidden root whenever it exists, even for a family whose
  // previous copy is sitting in <visible>/.<Family>.old. Sweeping both roots
  // before asking where the family lives costs six Storage::exists calls per
  // family and removes that whole class of miss. Neither root existing is fine:
  // exists() simply answers false.
  recoverStaleStaging(SdCardFontRegistry::FONTS_DIR_HIDDEN, family.name);
  recoverStaleStaging(SdCardFontRegistry::FONTS_DIR_VISIBLE, family.name);

  const char* root = SdCardFontRegistry::findFamilyRoot(family.name.c_str());
  const bool existed = root != nullptr;
  if (!existed) root = SdCardFontRegistry::defaultWriteRoot();

  const size_t matching = existed ? countMatchingFiles(family, root) : 0;
  if (fontsync::familyVerdict(family.files.size(), matching) == fontsync::FamilyVerdict::UNCHANGED) {
    LOG_DBG(LOG_MODULE, "Unchanged: %s (%u files)", family.name.c_str(), static_cast<unsigned>(matching));
    return FamilyResult::UNCHANGED;
  }
  LOG_INF(LOG_MODULE, "%s: %u of %u files current -- installing the whole family", family.name.c_str(),
          static_cast<unsigned>(matching), static_cast<unsigned>(family.files.size()));

  const std::string dest = installDir(root, family.name);
  const std::string stage = stageDirFor(root, family.name);
  const std::string backup = backupDirFor(root, family.name);

  // NOTHING ELSE CREATES THE FONT ROOT on a card that has never held an SD
  // font. Same trap as /books in LibraryUpdater: the manifest check two screens
  // earlier passes and then every file fails to open, with the network
  // blameless. Made here, on the first install, so a card that never runs this
  // does not grow an empty /.fonts.
  if (!Storage.ensureDirectoryExists(root) || !Storage.mkdir(stage.c_str())) {
    LOG_ERR(LOG_MODULE, "Cannot create %s on the card", stage.c_str());
    lastFailure_ = fontsync::FailureKind::STORAGE;
    return FamilyResult::FAILED;
  }

  bool failed = false;
  const size_t verified = stageFamily(family, stage, failed, onProgress, ctx);

  if (fontsync::commitVerdict(family.files.size(), verified, failed) == fontsync::CommitVerdict::DISCARD) {
    // THE INSTALLED FAMILY HAS NOT BEEN TOUCHED. Everything so far happened
    // inside a directory discovery cannot see; deleting it puts the card back
    // exactly where it started, which is the whole reason for staging.
    LOG_ERR(LOG_MODULE, "%s: %u of %u files verified -- discarding, the installed family is untouched",
            family.name.c_str(), static_cast<unsigned>(verified), static_cast<unsigned>(family.files.size()));
    Storage.removeDir(stage.c_str());
    if (lastFailure_ == fontsync::FailureKind::NONE) lastFailure_ = fontsync::FailureKind::VERIFY;
    return FamilyResult::FAILED;
  }

  // COMMIT: two renames, with the first undone if the second fails. Between
  // them the family is ABSENT rather than partial, and recoverStaleStaging
  // above puts it back if power is lost in that window.
  if (existed && !Storage.rename(dest.c_str(), backup.c_str())) {
    LOG_ERR(LOG_MODULE, "Cannot move %s aside; keeping what is installed", dest.c_str());
    Storage.removeDir(stage.c_str());
    lastFailure_ = fontsync::FailureKind::STORAGE;
    return FamilyResult::FAILED;
  }
  if (!Storage.rename(stage.c_str(), dest.c_str())) {
    LOG_ERR(LOG_MODULE, "Cannot move the staged %s into place; rolling back", family.name.c_str());
    if (existed && !Storage.rename(backup.c_str(), dest.c_str())) {
      // The previous copy is still whole, under a name discovery ignores. The
      // next run's recoverStaleStaging renames it back.
      LOG_ERR(LOG_MODULE, "Rollback failed too; %s is in %s until the next run", family.name.c_str(), backup.c_str());
    }
    Storage.removeDir(stage.c_str());
    lastFailure_ = fontsync::FailureKind::STORAGE;
    return FamilyResult::FAILED;
  }
  // THE OLD FAMILY DIRECTORY GOES IN FULL, INCLUDING ITS HI-RES <N>x SUBTREES.
  // This deletion is DELIBERATE. Do not "restore" a carry-over here.
  //
  // What is being deleted, and why it is easy to delete by accident: a family
  // directory is not only its six base cuts. A host build at
  // CROSSPOINT_RENDER_SCALE > 1 reads
  // <root>/<Family>/<N>x/<Family>_<size>.cpfont (SdCardFontManager.cpp:32-35,
  // :105), and fs_/fonts carries 2x and 3x subtrees today. This feature
  // publishes only the base tier -- the 2x/3x set is several times 80 MB and
  // was rejected on payload size -- so the staged directory never has an <N>x/.
  // Storage::removeDir is recursive (SDCardManager.cpp:380-411) and
  // scanDirectory skips subdirectories (SdCardFontRegistry.cpp:120-123), so
  // without this comment the loss would be both total and invisible: no error
  // at any point, and one LOG_INF on a scaled host
  // (SdCardFontManager.cpp:128). An adversarial review found it on 2026-09-07
  // and an implementation that carried the tiers over was written and then
  // withdrawn.
  //
  // OWNER RULING 2026-09-07, deleting them: the X4 renders at scale 1
  // (cp::renderScale(), SdCardFontManager.cpp:105) and never reads 2x/3x, so
  // deleting costs the device nothing and keeps each family internally
  // consistent. Only a RENDER_SCALE=2 host -- the iOS build -- notices, and
  // falling back to base-tier glyphs is better than reading a hi-res tier that
  // is older than the 1x cuts beside it. A carried-over tier is exactly that:
  // built from a different set of outlines with different metrics, next to base
  // cuts this run has just replaced.
  //
  // The consequence to know: a scaled host must re-seed its hi-res tiers out of
  // band after an Update Fonts run (install-sim-fonts.py / ios/seedfonts).
  if (existed) Storage.removeDir(backup.c_str());

  // Record the digests, which are known for certain: they were computed over
  // the bytes as they streamed in. The next run reads no part of this family.
  for (const auto& file : family.files) {
    putRecord(family.name + "/" + file.file, stampOf(dest + "/" + file.file), file.sha256);
  }

  installedAny = true;
  if (strcmp(SETTINGS.sdFontFamilyName, family.name.c_str()) == 0) activeFamilyChanged = true;

  LOG_INF(LOG_MODULE, "%s: %s (%u files)", existed ? "Updated" : "Added", family.name.c_str(),
          static_cast<unsigned>(family.files.size()));
  return existed ? FamilyResult::UPDATED : FamilyResult::ADDED;
}

// --- end of run -------------------------------------------------------------

size_t FontUpdater::removeUnlistedFamilies(std::vector<std::string>& removed) {
  // THE GATE. "I could not read the manifest" and "the manifest is empty" are
  // both "I do not know what belongs here", and neither may ever be acted on as
  // "so delete everything". fetchManifest() clears manifestOk_ on entry and
  // sets it only on its OK return, so every failure path lands here false.
  if (!manifestOk_ || families.empty()) {
    LOG_DBG(LOG_MODULE, "No usable manifest; removing nothing");
    return 0;
  }

  const char* roots[] = {SdCardFontRegistry::FONTS_DIR_HIDDEN, SdCardFontRegistry::FONTS_DIR_VISIBLE};
  size_t count = 0;
  for (const char* root : roots) {
    HalFile dir = Storage.open(root);
    if (!dir || !dir.isDirectory()) continue;

    // Names first, deletions after: removing entries while the parent's own
    // directory walk is open is not something SdFat promises.
    std::vector<std::string> candidates;
    candidates.reserve(16);
    char nameBuffer[128];
    while (true) {
      HalFile entry = dir.openNextFile();
      if (!entry) break;
      const bool isDir = entry.isDirectory();
      if (isDir) entry.getName(nameBuffer, sizeof(nameBuffer));
      entry.close();
      if (!isDir) continue;
      // '.' and '_' are what discovery skips (SdCardFontRegistry.cpp:181), so
      // they are never families -- they are this feature's own .<Family>.part
      // and .<Family>.old, macOS forks and .Trashes. Not ours to delete here.
      if (nameBuffer[0] == '.' || nameBuffer[0] == '_') continue;
      candidates.emplace_back(nameBuffer);
    }
    dir.close();

    for (const auto& name : candidates) {
      bool listed = false;
      for (const auto& family : families) {
        if (family.name == name) {
          listed = true;
          break;
        }
      }
      if (listed) continue;
      // The SAME validator the install side uses. A name it refuses is a name
      // this run could not have created and will not delete -- which makes
      // "delete something outside a font root" unrepresentable rather than
      // merely unlikely: no separator survives isSafeFamilyName, so
      // "<root>/<name>" cannot leave <root>.
      if (!fontsync::isSafeFamilyName(name.c_str())) {
        LOG_ERR(LOG_MODULE, "Refusing to remove an unsafe directory name under %s", root);
        continue;
      }
      const std::string path = installDir(root, name);
      if (!Storage.removeDir(path.c_str())) {
        LOG_ERR(LOG_MODULE, "Could not remove %s", path.c_str());
        continue;
      }
      LOG_INF(LOG_MODULE, "Removed %s (not in the manifest)", path.c_str());
      removed.push_back(name);
      ++count;
    }
  }

  if (count != 0) removedAny = true;
  return count;
}

void FontUpdater::clearStaleSectionCaches() {
  // Layout caches normally invalidate themselves: each section file stores the
  // fontId it was built with and is rejected when it differs (Section.cpp:362),
  // and fontId is computeFontId(contentHash, family, pointSize)
  // (SdCardFontManager.cpp:52). contentHash is an FNV-1a over the .cpfont
  // global header plus each style's TOC entry (SdCardFont.cpp:780-818) -- so a
  // rebuild that moves a glyph count, a kern entry count or a data offset
  // changes the id and the caches go on their own.
  //
  // THE HOLE: it does NOT cover glyph bitmaps or kern matrix bytes. A rebuild
  // that changes kern VALUES while every count stays identical keeps the same
  // id, and the old pagination is reused with no error anywhere. This run knows
  // the file changed -- the sha256 said so -- so it clears rather than hoping.
  //
  // Only the sections/ subdirectory: book.bin and progress.bin are not laid out
  // by the font, and dropping progress.bin would cost the owner his place in
  // every book to fix a repagination.
  HalFile root = Storage.open(cacheRoot);
  if (!root || !root.isDirectory()) return;

  std::vector<std::string> sectionDirs;
  sectionDirs.reserve(16);  // Resource Protocol 7; a card holds a few dozen books at most
  char nameBuffer[128];
  while (true) {
    HalFile entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }
    entry.getName(nameBuffer, sizeof(nameBuffer));
    entry.close();
    if (strncmp(nameBuffer, cacheDirPrefix, sizeof(cacheDirPrefix) - 1) != 0) continue;
    // Collected first, removed after: removing a directory while its parent's
    // iterator is open is not something SdFat's directory walk promises.
    sectionDirs.push_back(std::string(cacheRoot) + "/" + nameBuffer + "/sections");
  }
  root.close();

  unsigned cleared = 0;
  for (const auto& path : sectionDirs) {
    if (!Storage.exists(path.c_str())) continue;
    if (Storage.removeDir(path.c_str())) ++cleared;
  }
  LOG_INF(LOG_MODULE, "Cleared %u stale layout cache(s) after replacing the active font family", cleared);
}

void FontUpdater::finishRun() {
  flushSyncRecords();
  // REMOVALS COUNT AS WELL AS INSTALLS. A run that only deleted families still
  // has to re-discover, or the picker keeps offering a family that is no longer
  // on the card until the next reboot -- and SdCardFontSystem::ensureLoaded
  // only clears SETTINGS.sdFontFamilyName for a family that "disappeared" once
  // it has re-discovered and found it gone (SdCardFontSystem.cpp:122-127,
  // :152-155). Gating this on installedAny alone made a removal-only run a
  // silent no-op on both counts.
  if (!installedAny && !removedAny) return;

  // The picker enumerates the registry, and the resident font is reloaded only
  // after a re-discovery -- ensureLoaded force-reloads when the registry was
  // dirty precisely because "the file contents on disk may have changed"
  // (SdCardFontSystem.cpp:93-100). Without this a newly installed family does
  // not appear until reboot, and a replaced one keeps rendering from the old
  // resident file.
  sdFontSystem.markRegistryDirty();

  if (activeFamilyChanged) clearStaleSectionCaches();
}
