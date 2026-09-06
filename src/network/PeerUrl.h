#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// The address a peer types to reach the file-transfer server.
//
// On hardware the server listens on 80 and the URL omits the port, as it always
// has. A host build cannot bind 80 (an unprivileged process), so the
// simulator's shim maps the firmware's 80 to 8080 -- CROSSPOINT_SIM_HTTP_PORT
// moves it -- and nothing above the shim knew: the screen painted and
// QR-encoded http://<ip>/ while the server listened on :8080, so every address
// on it was wrong on a phone (crosspoint-simulator ios/WIFI.md, finding 4, open
// since 2026-08-07). On iOS the .local hostname cannot be claimed at all
// (finding 6), which makes the IP URL the only address a peer can use, and the
// port in it the difference between peer transfer working and not.
//
// Header-only and free of any device or host header, so a host test can pin the
// spelling without linking the screen.
namespace peerurl {

constexpr uint16_t HTTP_DEFAULT_PORT = 80;

// "host" on HTTP's default port, "host:port" on any other.
inline std::string hostWithPort(const std::string& host, uint16_t port) {
  if (port == HTTP_DEFAULT_PORT) return host;
  char suffix[8];  // ":65535" is 6 chars + NUL
  snprintf(suffix, sizeof(suffix), ":%u", static_cast<unsigned>(port));
  return host + suffix;
}

// "http://host/" or "http://host:port/" -- what the QR code encodes.
inline std::string httpUrl(const std::string& host, uint16_t port) {
  return "http://" + hostWithPort(host, port) + "/";
}

}  // namespace peerurl
