#pragma once

#include <cstdlib>
#include <cstring>
#include <string>

// Percent-decoding for a WebDAV PATH: the request path and the path inside a
// MOVE or COPY Destination header.
//
// The handler used to run both through WebServer::urlDecode, which is the
// server's decoder for query strings and form fields: there a '+' is a space.
// In a path a '+' is a '+' (RFC 3986 section 3.3), and the handler's own
// PROPFIND listing writes it that way (urlEncodePath leaves '+' alone). So the
// address the listing gave for "C++ Primer.epub", /books/C++%20Primer.epub,
// was read back as "C   Primer.epub" and answered 404, and a name typed into
// the address with a '+' (curl -T x.txt http://host/books/Raw+Plus.txt) was
// stored with a space and answered 201
// (docs/review-passes-2026-11-27.md, the '+' finding).
//
// Everything else is WebServer::urlDecode's rule, kept the same on purpose
// (arduino-esp32 3.3.7 libraries/WebServer/src/Parsing.cpp:591): a '%' takes
// the next two characters whatever they are and runs them through strtol, and
// a '%' with fewer than two characters after it stays a '%'. The one
// difference is the '+'.
//
// Header-only and free of any device or host header, so a host test can pin
// the rule without linking the server.
namespace davpath {

inline std::string decode(const char* text) {
  std::string out;
  if (!text) return out;
  const size_t len = strlen(text);
  out.reserve(len);
  size_t i = 0;
  while (i < len) {
    const char c = text[i++];
    if (c == '%' && i + 1 < len) {
      char hex[] = "0x00";
      hex[2] = text[i++];
      hex[3] = text[i++];
      out.push_back(static_cast<char>(strtol(hex, nullptr, 16)));
    } else {
      out.push_back(c);
    }
  }
  return out;
}

}  // namespace davpath
