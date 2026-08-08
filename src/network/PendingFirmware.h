#pragma once

#include <string>

// Hand-off between the WebDAV PUT handler and the file-transfer activity.
//
// A firmware image uploaded to /firmware/ must NOT be flashed inside the PUT
// handler. Flashing takes tens of seconds and ends in a reboot, so doing it
// there kills the connection mid-request and the sender cannot tell "upload
// failed" from "upload worked and the device is already restarting". The
// handler therefore records the path and returns 201 immediately;
// CrossPointWebServerActivity::loop() picks it up on the next tick, once the
// response is on the wire.
//
// Deliberately a plain slot rather than a queue: one pending image at a time is
// the whole requirement, and a later upload replacing an earlier one is the
// behaviour you want anyway.
namespace pending_firmware {

// Set by the WebDAV PUT handler. Empty when nothing is waiting.
inline std::string& slot() {
  static std::string path;
  return path;
}

inline void set(const char* sdPath) { slot() = sdPath ? sdPath : ""; }

inline bool waiting() { return !slot().empty(); }

// Returns the path and clears the slot, so a failed or cancelled update does
// not re-trigger on every subsequent loop tick.
inline std::string take() {
  std::string p;
  p.swap(slot());
  return p;
}

// The reserved directory. Any *.bin PUT directly into it is offered as a
// firmware update. A directory rather than a single reserved filename so the
// release's own name -- 20260807T0709Z-crosspoint-<sha>.bin -- survives the
// upload and can be shown in the confirmation prompt, which is the only thing
// telling the owner WHICH build they are about to flash.
constexpr const char* DIR = "/firmware/";

// True for "/firmware/<something>.bin" and nothing else. Not recursive: a
// nested path is a file the owner is storing, not one they are installing.
inline bool isUpdatePath(const std::string& path) {
  const size_t dirLen = 10;  // strlen("/firmware/")
  if (path.size() <= dirLen || path.compare(0, dirLen, DIR) != 0) return false;
  if (path.find('/', dirLen) != std::string::npos) return false;
  const std::string name = path.substr(dirLen);
  // Needs a stem: ".bin" on its own is a hidden file, not an image.
  if (name.size() <= 4) return false;
  if (name[0] == '.') return false;
  const std::string ext = name.substr(name.size() - 4);
  return ext == ".bin" || ext == ".BIN";
}

}  // namespace pending_firmware
