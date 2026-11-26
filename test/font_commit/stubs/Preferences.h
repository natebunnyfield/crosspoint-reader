// Host stub for Arduino-ESP32's Preferences (NVS), for the key store that
// FontUpdater.cpp reaches through GithubAuth.h -> util/KeyStore.h. In memory,
// per process; enough of the API KeyStore.h calls. Nothing is ever stored by
// the font-commit tests, so every read answers "not set".
#pragma once
#include <cstddef>
#include <map>
#include <string>

class Preferences {
 public:
  bool begin(const char* name, bool /*readOnly*/ = false) {
    ns_ = name ? name : "";
    return true;
  }
  void end() {}
  // std::string stands in for Arduino String: KeyStore.h uses only
  // length() and c_str() on the result.
  std::string getString(const char* key, const char* def = "") {
    const auto it = store().find(ns_ + "/" + key);
    return it == store().end() ? std::string(def) : it->second;
  }
  size_t putString(const char* key, const char* value) {
    store()[ns_ + "/" + key] = value ? value : "";
    return value ? std::string(value).size() : 0;
  }
  bool remove(const char* key) { return store().erase(ns_ + "/" + key) != 0; }

 private:
  static std::map<std::string, std::string>& store() {
    static std::map<std::string, std::string> s;
    return s;
  }
  std::string ns_;
};
