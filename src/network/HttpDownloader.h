#pragma once
#include <HalStorage.h>
#include <Memory.h>

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * HTTP client utility for fetching content and downloading files. Built on
 * esp_http_client: https is verified against the CA bundle, plain http is
 * used for local servers (transport is chosen from the URL scheme).
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;
  // Called once, before the first body byte reaches DataCallback, with the
  // Content-Length the server declared -- 0 when it declared none (a chunked
  // response). Return false to refuse the body before any of it is buffered.
  // Exists so a caller that must hold the whole body can size its buffer
  // exactly once instead of growing into it. See fetchUrlToBuffer and B-053.
  using SizeCallback = std::function<bool(size_t total)>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
    // 404, kept apart from HTTP_ERROR because the two mean opposite things to a
    // reader. A 404 is a SUCCESSFUL conversation with GitHub whose answer is
    // "there is nothing here"; HTTP_ERROR is "the conversation did not happen".
    // Collapsing them printed "Could not reach GitHub" at an owner whose device
    // had reached GitHub perfectly and been told the fork has no releases.
    NOT_FOUND,
    // 401 and 403, kept apart for the SAME reason 404 is, and it became the
    // likely failure the moment the token stopped coming from a carefully
    // edited file. The library release lives in a private repo; a token that is
    // mistyped, expired, or missing the repo's scope is answered by GitHub, not
    // by silence -- so reporting it as "could not reach GitHub" sends the owner
    // to debug WiFi that is working perfectly. Typed on a phone keyboard, a
    // wrong token is now the FIRST thing to suspect rather than the last.
    UNAUTHORIZED,
    // The body is bigger than the caller said it could hold. Kept apart from
    // HTTP_ERROR for the same reason NOT_FOUND is: the conversation succeeded.
    TOO_LARGE,
    // No buffer could be allocated for the body. Reported rather than fatal,
    // which is the whole point of fetchUrlToBuffer -- see B-053.
    OUT_OF_MEMORY,
  };

  /**
   * A whole response body in one nothrow heap block, always NUL-terminated.
   *
   * Exists because the obvious accumulator -- std::string::append inside the
   * data callback -- reaches operator new, and operator new is NOT nothrow
   * under -fno-exceptions: a refused growth step raises bad_alloc, which
   * terminates. That is B-053, and it aborted Update Fonts in 1.5.29-BD on an
   * 18 KB manifest, on the growth step from 11024 to 22048 bytes.
   */
  struct Body {
    std::unique_ptr<char[]> data;
    size_t len = 0;

    const char* c_str() const { return data ? data.get() : ""; }
    explicit operator bool() const { return data != nullptr; }
    void reset() {
      data.reset();
      len = 0;
    }
  };

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "");

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "");

  /**
   * Same, but reports WHY it failed rather than just that it did. Used by the
   * update check, where "no release published" and "no network" need different
   * words in front of a person.
   */
  static DownloadError fetchUrlWithStatus(const std::string& url, const DataCallback& onData,
                                          const std::string& username = "", const std::string& password = "");

  // Extra request headers, sent verbatim. Exists for the GitHub asset API,
  // where a private repo needs "Authorization: Bearer <token>" plus
  // "Accept: application/octet-stream" — Basic auth cannot express either.
  using HeaderList = std::vector<std::pair<std::string, std::string>>;

  /**
   * fetchUrlWithStatus with custom request headers. Same streaming contract.
   */
  static DownloadError fetchUrlWithHeaders(const std::string& url, const HeaderList& headers,
                                           const DataCallback& onData, const SizeCallback& onSize = nullptr);

  /**
   * Fetch a whole body into one nothrow block, refusing anything over
   * maxBytes. Never aborts: OUT_OF_MEMORY and TOO_LARGE come back as errors.
   *
   * Defined here rather than in the .cpp on purpose. It is written entirely in
   * terms of fetchUrlWithHeaders, so the suites that substitute their own
   * fetchUrlWithHeaders (test/font_commit) get this for free and cannot
   * accidentally reach the network through it.
   */
  static DownloadError fetchUrlToBuffer(const std::string& url, const HeaderList& headers, size_t maxBytes,
                                        Body& out) {
    out.reset();

    size_t capacity = 0;
    bool oom = false;
    bool tooLarge = false;

    // Grow to hold `need` bytes plus a NUL. Nothrow the whole way down: a
    // refusal stops the transfer and is reported, it never terminates.
    auto ensure = [&](size_t need) {
      if (need <= capacity) return true;
      if (need > maxBytes) {
        tooLarge = true;
        return false;
      }
      // Double, but never past the cap -- `want <= maxBytes / 2` is what keeps
      // `want *= 2` from wrapping when a caller passes a very large maxBytes.
      // Found by a test that asked for an absurd body on purpose.
      size_t want = capacity ? capacity : 1024;
      while (want < need && want <= maxBytes / 2) want *= 2;
      if (want < need) want = need;  // never hand back less than was asked for
      auto grown = makeUniqueNoThrow<char[]>(want + 1);
      if (!grown) {
        oom = true;
        return false;
      }
      if (out.len) memcpy(grown.get(), out.data.get(), out.len);
      out.data = std::move(grown);
      capacity = want;
      return true;
    };

    const DownloadError result = fetchUrlWithHeaders(
        url, headers,
        [&](const uint8_t* data, size_t len) {
          if (!ensure(out.len + len)) return false;
          memcpy(out.data.get() + out.len, data, len);
          out.len += len;
          out.data[out.len] = '\0';
          return true;
        },
        [&](size_t total) {
          // The normal path: one allocation of exactly the declared size, so
          // the doubling never happens. A server that declares nothing falls
          // through to ensure()'s growth, which is bounded and nothrow.
          //
          // ensure() is also what refuses an over-cap declaration, and it does
          // so here, before the transport streams a byte. An explicit
          // `total > maxBytes` alongside it would be exactly the same test
          // written twice. HttpBodyTest's dataCallbacks assertion pins the
          // refuse-before-streaming behavior.
          return total == 0 || ensure(total);
        });

    if (tooLarge) {
      out.reset();
      return TOO_LARGE;
    }
    if (oom) {
      out.reset();
      return OUT_OF_MEMORY;
    }
    if (result != OK) out.reset();
    return result;
  }

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "");
};
