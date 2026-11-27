#pragma once

#include <HalStorage.h>
#include <NetworkUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include <memory>
#include <string>
#include <vector>

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

class CrossPointWebServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  // Used by POST upload handler
  struct UploadState {
    HalFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";
    // The status sent with `error`. 400 unless the refusal has a better one.
    int errorCode = 400;
    // This object outlives each request, and only a file part resets it. True
    // from a file part's start until handleUploadPost() has answered for it:
    // a POST with no file part must not be answered from the last upload's
    // result (review 2026-11-27, R-02).
    bool filePartSeen = false;

    // Upload write buffer - batches small writes into larger SD card operations
    // 4KB is a good balance: large enough to reduce syscall overhead, small enough
    // to keep individual write times short and avoid watchdog issues
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
    // A FIXED ARRAY, not a vector that resizes in the constructor.
    //
    // CrossPointWebServerActivity allocates this whole object with
    // makeUniqueNoThrow and checks the result, precisely so that being out of
    // memory does not take the device down "at the exact moment the owner asked
    // for File Transfer" (its own words). A vector resized in the constructor
    // defeated that from the inside: the object allocation was nothrow and
    // checked, and then the constructor called operator new anyway, which is
    // not nothrow under -fno-exceptions and aborts. Resource Protocol 9, and
    // the same mechanism as B-053. As an array the buffer is part of the one
    // allocation that is already checked.
    uint8_t buffer[UPLOAD_BUFFER_SIZE];
    size_t bufferPos = 0;
  } upload;

  CrossPointWebServer();
  ~CrossPointWebServer();

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;
  uint16_t wsPort = 81;  // WebSocket port
  NetworkUDP udp;
  bool udpActive = false;

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void abortWsUpload(const char* tag);

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot() const;
  void handleJszip() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload() const;
  void handleUpload(UploadState& state) const;
  void handleUploadForm() const;
  void handleUploadPost(UploadState& state) const;
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleDelete() const;

  // Settings handlers
  void handleSettingsPage() const;
  void handleGetSettings() const;
  void handlePostSettings();

  // Font management handlers
  void handleFontsPage() const;
  void handleFontList() const;
  void handleFontUpload();
  void handleFontUploadData();
  void handleFontDelete();

  // Font upload state
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string filePath;
    bool valid = false;
    bool magicChecked = false;
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    // Fixed array for the same reason as UploadState::buffer above.
    uint8_t buffer[BUFFER_SIZE];
    size_t bufferPos = 0;
  } fontUpload;

  // Wi-Fi credential handlers
  void handleGetWifiNetworks() const;
  void handlePostWifiNetwork();
  void handleDeleteWifiNetwork();
  // Keys and tokens (owner 2026-11-26): write-only. GET says only whether each
  // key is set and its last 4 characters; POST sets or clears one.
  void handleGetKeys() const;
  void handlePostKeys();
};

// True while a WebSocket upload is writing `path`, or anything beneath it when
// `path` is a folder. That upload's file stays open for write across many
// handleClient() ticks, and HTTP/WebDAV requests are served in between: a
// delete, rename, move or overwrite of the file (or of a folder holding it)
// under that open handle orphans the upload, or -- when the freed directory
// slot is reused before the upload closes -- syncs the upload's cluster chain
// into another file's entry. Mutating handlers refuse while this is true.
bool webUploadIsWriting(const char* path);

// Why the card would not create `name` in `folder`, as the reply to a failed
// upload: a name the card cannot store (400, FsHelpers::checkFatName), a
// folder that is not there (409), or neither, which leaves the card itself
// (500). Call it AFTER the open or rename has failed.
struct CreateFailure {
  int status;
  String reason;
};
CreateFailure explainCreateFailure(const String& folder, const String& name);
