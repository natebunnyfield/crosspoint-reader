#pragma once
#include <HalStorage.h>

#include <functional>
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
                                           const DataCallback& onData);

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "");
};
