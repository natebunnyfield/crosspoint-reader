// B-053: Update Fonts aborted the device reading an 18 KB manifest. The body
// accumulated into a std::string, whose doubling growth reached operator new --
// not nothrow under -fno-exceptions -- so the refused step from 11024 to 22048
// bytes raised bad_alloc and terminated. These tests pin the replacement.
//
// The host cannot reproduce the abort: desktop new succeeds where a 380 KB part
// refuses. What IS provable here, and is what the fix actually changed, is that
// no path through fetchUrlToBuffer calls a throwing allocator, that a declared
// Content-Length is reserved exactly once instead of grown into, and that every
// refusal comes back as an error with the buffer released.

#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "Fake.h"
#include "HttpDownloader.h"

namespace {

const HttpDownloader::HeaderList kNoHeaders;

std::string bodyOfSize(size_t n) {
  std::string s;
  s.reserve(n);
  for (size_t i = 0; i < n; ++i) s.push_back(static_cast<char>('a' + (i % 26)));
  return s;
}

class HttpBody : public ::testing::Test {
 protected:
  void SetUp() override { fakehttp::reset(); }
};

// The exact case that crashed: 18,108 bytes arriving in 1 KB pieces. The old
// accumulator died on the growth step partway through; this must complete.
TEST_F(HttpBody, TheManifestThatAbortedTheDeviceNowReadsWhole) {
  auto& s = fakehttp::script();
  s.body = bodyOfSize(18108);
  s.declaredLength = 18108;
  s.chunk = 1024;

  HttpDownloader::Body out;
  ASSERT_EQ(HttpDownloader::OK, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 64 * 1024, out));
  EXPECT_EQ(18108u, out.len);
  EXPECT_EQ(s.body, std::string(out.c_str(), out.len));
}

// The reserve happens once, off the announced length, before a byte is copied.
TEST_F(HttpBody, ADeclaredLengthIsAnnouncedExactlyOnce) {
  auto& s = fakehttp::script();
  s.body = bodyOfSize(4000);
  s.declaredLength = 4000;

  HttpDownloader::Body out;
  ASSERT_EQ(HttpDownloader::OK, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 64 * 1024, out));
  EXPECT_EQ(1u, s.sizeAnnouncements);
  EXPECT_EQ(4000u, out.len);
}

// The buffer is always NUL-terminated, so c_str() is safe to hand to a parser
// that wants a C string rather than a pointer and a length.
TEST_F(HttpBody, TheBufferIsNulTerminated) {
  auto& s = fakehttp::script();
  s.body = "{\"version\":1}";
  s.declaredLength = s.body.size();

  HttpDownloader::Body out;
  ASSERT_EQ(HttpDownloader::OK, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 1024, out));
  EXPECT_STREQ("{\"version\":1}", out.c_str());
  EXPECT_EQ(13u, out.len);
}

// A server that declares nothing (a chunked response) still works: the growth
// path is bounded and nothrow rather than absent.
TEST_F(HttpBody, AChunkedResponseWithNoDeclaredLengthStillReadsWhole) {
  auto& s = fakehttp::script();
  s.body = bodyOfSize(9000);
  s.declaredLength = 0;
  s.chunk = 512;

  HttpDownloader::Body out;
  ASSERT_EQ(HttpDownloader::OK, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 64 * 1024, out));
  EXPECT_EQ(9000u, out.len);
  EXPECT_EQ(s.body, std::string(out.c_str(), out.len));
}

// A body that DECLARES more than the cap is refused at the announcement, before
// the transport streams a single chunk. The dataCallbacks assertion is the load
// -bearing one: without the check in the size hook, ensure() would still refuse
// this, but only after the first chunk had already been handed over -- which is
// how the first version of this test passed against a deliberately broken
// build. Do not weaken it to "returns TOO_LARGE".
TEST_F(HttpBody, ADeclaredLengthOverTheCapIsRefusedBeforeAnyBytesAreHeld) {
  auto& s = fakehttp::script();
  s.body = bodyOfSize(5000);
  s.declaredLength = 5000;

  HttpDownloader::Body out;
  EXPECT_EQ(HttpDownloader::TOO_LARGE, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 4096, out));
  EXPECT_FALSE(out);
  EXPECT_EQ(0u, out.len);
  EXPECT_EQ(1u, s.sizeAnnouncements);
  EXPECT_EQ(0u, s.dataCallbacks) << "the body was streamed even though its declared size was over the cap";
}

// And a body that declares nothing and then EXCEEDS the cap is refused at the
// growth step. This is the one a declared-length check alone would miss.
TEST_F(HttpBody, AnUndeclaredBodyOverTheCapIsRefusedWhileGrowing) {
  auto& s = fakehttp::script();
  s.body = bodyOfSize(5000);
  s.declaredLength = 0;
  s.chunk = 256;

  HttpDownloader::Body out;
  EXPECT_EQ(HttpDownloader::TOO_LARGE, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 4096, out));
  EXPECT_FALSE(out);
}

// A transport failure partway through releases the buffer rather than handing
// back a truncated body that would parse as valid-looking JSON.
TEST_F(HttpBody, ATransportFailureLeavesNothingBehind) {
  auto& s = fakehttp::script();
  s.body = bodyOfSize(8000);
  s.declaredLength = 8000;
  s.chunk = 1024;
  s.failAfter = 3000;

  HttpDownloader::Body out;
  EXPECT_EQ(HttpDownloader::HTTP_ERROR, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 64 * 1024, out));
  EXPECT_FALSE(out);
  EXPECT_EQ(0u, out.len);
}

// The whole point of B-053: an allocation the platform refuses comes back as an
// error. On device that is a 22 KB block on a spent heap; on the host the only
// way to make new fail on demand is to ask for an absurd one, so the cap is set
// above what any allocator will hand over and the server declares that much.
// Without this, nothing exercises the nullptr branch at all -- mutation testing
// on 2026-09-07 showed the OOM path was the one uncovered guard.
TEST_F(HttpBody, AnAllocationRefusalIsReportedRatherThanFatal) {
  auto& s = fakehttp::script();
  s.body = "unreachable";
  // Large enough that no allocator can satisfy it. 1<<60 is NOT enough: this
  // host reserves it lazily and hands back a pointer, which is how the first
  // version of this test failed against correct code.
  s.declaredLength = std::numeric_limits<size_t>::max() / 2;

  HttpDownloader::Body out;
  EXPECT_EQ(HttpDownloader::OUT_OF_MEMORY,
            HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, std::numeric_limits<size_t>::max(), out));
  EXPECT_FALSE(out);
  EXPECT_EQ(0u, s.dataCallbacks) << "the body was streamed after the buffer could not be allocated";
}

// An empty body is a success with an empty buffer, not an error and not a null
// c_str(): the manifest parser is entitled to say "this did not parse".
TEST_F(HttpBody, AnEmptyBodyIsNotAnError) {
  auto& s = fakehttp::script();
  s.declaredLength = 0;

  HttpDownloader::Body out;
  EXPECT_EQ(HttpDownloader::OK, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 1024, out));
  EXPECT_EQ(0u, out.len);
  EXPECT_STREQ("", out.c_str());
}

// Reusing an out-parameter that already holds a body must not append to it.
TEST_F(HttpBody, AReusedBufferStartsClean) {
  HttpDownloader::Body out;
  {
    auto& s = fakehttp::script();
    s.body = bodyOfSize(2000);
    s.declaredLength = 2000;
    ASSERT_EQ(HttpDownloader::OK, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 64 * 1024, out));
    ASSERT_EQ(2000u, out.len);
  }
  fakehttp::reset();
  auto& s = fakehttp::script();
  s.body = bodyOfSize(100);
  s.declaredLength = 100;
  ASSERT_EQ(HttpDownloader::OK, HttpDownloader::fetchUrlToBuffer("u", kNoHeaders, 64 * 1024, out));
  EXPECT_EQ(100u, out.len);
  EXPECT_EQ(s.body, std::string(out.c_str(), out.len));
}

}  // namespace
