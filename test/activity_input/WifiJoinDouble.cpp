// Link doubles for the saved-network join (src/network/SavedWifiJoin.h).
//
// LibraryUpdateActivity and FontUpdateActivity include SavedWifiJoin.h, which
// since d1f0f33cc (2026-10-20) reaches the credential store and the radio
// diagnostics. Nothing in this suite drives a join: there are no saved
// networks, the diagnostics report nothing, and the store never reads the
// card. The real WifiDiagnostics.cpp hangs off ESP-IDF Wi-Fi events and cannot
// build on the host, so all of it is stubbed here rather than linked.
#include <PersistableStore.h>

#include "WifiCredentialStore.h"
#include "network/WifiDiagnostics.h"

bool PersistableStoreBase::readDocFromFile(const char*, JsonDocument&) { return false; }

void WifiCredentialStore::toJson(JsonDocument&) const {}
bool WifiCredentialStore::fromJson(JsonVariantConst) { return true; }
std::optional<WifiCredential> WifiCredentialStore::findCredential(const std::string&) const { return std::nullopt; }
std::optional<std::string> WifiCredentialStore::getSsidAt(size_t) const { return std::nullopt; }
size_t WifiCredentialStore::getCredentialCount() const { return 0; }
void WifiCredentialStore::setLastConnectedSsid(const std::string&) {}
std::string WifiCredentialStore::getLastConnectedSsid() const { return {}; }

namespace WifiDiagnostics {
void begin() {}
uint8_t lastDisconnectReason() { return 0; }
uint8_t authFailureCount() { return 0; }
void resetCounters() {}
bool isAssociated() { return false; }
void writeFailureLog(const char*, bool) {}
const char* reasonName(uint8_t) { return ""; }
}  // namespace WifiDiagnostics
