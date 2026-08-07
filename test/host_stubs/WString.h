// Host stub for Arduino's WString.h.
//
// Pulled in only by lib/FsHelpers/FsHelpers.h:2, which declares a `const
// String&` convenience overload beside each `std::string_view` one. Nothing
// host-side owns an Arduino String, so those overloads are dead code here —
// this exists so the header parses.
//
// Every constructor is EXPLICIT, unlike the real Arduino String. That is
// deliberate: an implicit `const char*` -> String would make
// `hasEpubExtension("x.epub")` ambiguous between the String and the
// string_view overload. Explicit ctors keep the String overloads unreachable
// by conversion, which is exactly what we want.
#pragma once

#include <cstddef>
#include <string>

class String {
 public:
  String() = default;
  explicit String(const char* s) : s_(s ? s : "") {}
  explicit String(std::string s) : s_(std::move(s)) {}

  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }

 private:
  std::string s_;
};
