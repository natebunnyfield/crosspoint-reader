// How the WebDAV handler reads a path off the wire.
//
// docs/review-passes-2026-11-27.md, the '+' finding: the handler decoded its
// paths with WebServer::urlDecode, the form decoder, so a '+' became a space.
// PROPFIND listed "C++ Primer.epub" as /books/C++%20Primer.epub and a GET of
// that address answered 404; `curl -T x.txt http://host/books/Raw+Plus.txt`
// stored "Raw Plus.txt" and answered 201. The simulator's
// tests/test_blink_upload.sh pins both against the running server; this pins
// the rule itself, including what was deliberately left as it was.
#include <gtest/gtest.h>

#include <string>

#include "network/DavPathDecode.h"

namespace {

// The address PROPFIND hands out for "C++ Primer.epub" names that file.
TEST(DavPathDecode, PlusInAPathIsAPlus) {
  EXPECT_EQ(davpath::decode("/books/C++%20Primer.epub"), "/books/C++ Primer.epub");
  EXPECT_EQ(davpath::decode("/books/Raw+Plus.txt"), "/books/Raw+Plus.txt");
  EXPECT_EQ(davpath::decode("+"), "+");
}

// What curl -T sends for the same name (it escapes the '+') still works.
TEST(DavPathDecode, EscapedPlusIsAPlus) {
  EXPECT_EQ(davpath::decode("/books/C%2B%2B%20Primer.epub"), "/books/C++ Primer.epub");
  EXPECT_EQ(davpath::decode("/books/C%2b%2b%20Primer.epub"), "/books/C++ Primer.epub");
}

// One pass only: "100%2525.epub" is the file "100%25.epub", not "100%.epub".
TEST(DavPathDecode, DecodesOnce) {
  EXPECT_EQ(davpath::decode("/books/100%2525.epub"), "/books/100%25.epub");
  EXPECT_EQ(davpath::decode("/a%252Bb"), "/a%2Bb");
}

// Every name the listing escapes comes back as it was.
TEST(DavPathDecode, ListedEscapesRoundTrip) {
  EXPECT_EQ(davpath::decode("/books/a%26b%20%231.epub"), "/books/a&b #1.epub");
  EXPECT_EQ(davpath::decode("/books/100%25%20Proof.epub"), "/books/100% Proof.epub");
  EXPECT_EQ(davpath::decode("/books/%C3%91and%C3%BA%20q%3F.epub"),
            "/books/\xC3\x91"
            "and\xC3\xBA q?.epub");
  EXPECT_EQ(davpath::decode("/books/semi;colon=eq.epub"), "/books/semi;colon=eq.epub");
}

// Unchanged from the server's decoder (arduino-esp32 3.3.7 Parsing.cpp:591):
// a '%' with fewer than two characters after it stays, and an escape that is
// not two hex digits goes through strtol ("%zz" is a NUL byte, "%4z" is 0x04).
TEST(DavPathDecode, MalformedEscapesAreAsTheServerDecodesThem) {
  EXPECT_EQ(davpath::decode("/a%"), "/a%");
  EXPECT_EQ(davpath::decode("/a%4"), "/a%4");
  EXPECT_EQ(davpath::decode("/a%41"), "/aA");
  EXPECT_EQ(davpath::decode("/a%zzb"), std::string("/a\0b", 4));
  EXPECT_EQ(davpath::decode("/a%4zb"), std::string("/a\x04"
                                                   "b",
                                                   4));
}

TEST(DavPathDecode, EmptyAndNull) {
  EXPECT_EQ(davpath::decode(""), "");
  EXPECT_EQ(davpath::decode(nullptr), "");
}

}  // namespace
