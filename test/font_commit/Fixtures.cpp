// Host-side definitions for the font-commit suite: SHA-256, the scripted
// "release" behind HttpDownloader, and the two globals FontUpdater links
// against. See Fixtures.h.

#include "Fixtures.h"

#include <cstring>

#include "HttpDownloader.h"  // the REAL one, from src/network -- see stubs/HalStorage.h
#include "SdCardFontSystem.h"

SdCardFontSystem sdFontSystem;

std::string& halStorageRootRef() {
  static std::string root;
  return root;
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4). Real, not a placeholder -- see stubs/mbedtls/sha256.h.
// ---------------------------------------------------------------------------
namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void transform(mbedtls_sha256_context* c, const uint8_t* block) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
  uint32_t e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = h + S1 + ch + K[i] + w[i];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
    const uint32_t t2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = cc;
    cc = b;
    b = a;
    a = t1 + t2;
  }
  c->state[0] += a;
  c->state[1] += b;
  c->state[2] += cc;
  c->state[3] += d;
  c->state[4] += e;
  c->state[5] += f;
  c->state[6] += g;
  c->state[7] += h;
}

}  // namespace

void mbedtls_sha256_init(mbedtls_sha256_context* ctx) { std::memset(ctx, 0, sizeof(*ctx)); }
void mbedtls_sha256_free(mbedtls_sha256_context* ctx) { std::memset(ctx, 0, sizeof(*ctx)); }

int mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int) {
  ctx->state[0] = 0x6a09e667;
  ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372;
  ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f;
  ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab;
  ctx->state[7] = 0x5be0cd19;
  ctx->bitlen = 0;
  ctx->buflen = 0;
  return 0;
}

int mbedtls_sha256_update(mbedtls_sha256_context* ctx, const unsigned char* input, size_t ilen) {
  for (size_t i = 0; i < ilen; ++i) {
    ctx->buffer[ctx->buflen++] = input[i];
    if (ctx->buflen == 64) {
      transform(ctx, ctx->buffer);
      ctx->bitlen += 512;
      ctx->buflen = 0;
    }
  }
  return 0;
}

int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, unsigned char output[32]) {
  size_t i = ctx->buflen;
  ctx->bitlen += ctx->buflen * 8ull;
  ctx->buffer[i++] = 0x80;
  if (i > 56) {
    while (i < 64) ctx->buffer[i++] = 0;
    transform(ctx, ctx->buffer);
    i = 0;
  }
  while (i < 56) ctx->buffer[i++] = 0;
  for (int b = 7; b >= 0; --b) ctx->buffer[56 + (7 - b)] = static_cast<uint8_t>(ctx->bitlen >> (b * 8));
  transform(ctx, ctx->buffer);
  for (int w = 0; w < 8; ++w) {
    output[w * 4] = static_cast<uint8_t>(ctx->state[w] >> 24);
    output[w * 4 + 1] = static_cast<uint8_t>(ctx->state[w] >> 16);
    output[w * 4 + 2] = static_cast<uint8_t>(ctx->state[w] >> 8);
    output[w * 4 + 3] = static_cast<uint8_t>(ctx->state[w]);
  }
  return 0;
}

std::string sha256Hex(const std::string& data) {
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  mbedtls_sha256_update(&c, reinterpret_cast<const unsigned char*>(data.data()), data.size());
  unsigned char d[32];
  mbedtls_sha256_finish(&c, d);
  mbedtls_sha256_free(&c);
  static const char* hex = "0123456789abcdef";
  std::string out(64, '0');
  for (int i = 0; i < 32; ++i) {
    out[i * 2] = hex[d[i] >> 4];
    out[i * 2 + 1] = hex[d[i] & 0x0F];
  }
  return out;
}

// ---------------------------------------------------------------------------
// The scripted release
// ---------------------------------------------------------------------------
namespace fakegh {

Server& server() {
  static Server s;
  return s;
}

void reset() { server() = Server{}; }

}  // namespace fakegh

HttpDownloader::DownloadError HttpDownloader::fetchUrlWithHeaders(const std::string& url, const HeaderList&,
                                                                  const DataCallback& onData,
                                                                  const SizeCallback& onSize) {
  auto& s = fakegh::server();
  s.requested.push_back(url);

  const auto it = s.bodies.find(url);
  if (it == s.bodies.end()) return NOT_FOUND;

  const std::string& body = it->second;
  // Declare the length the way GitHub does, so fetchUrlToBuffer takes its
  // one-shot reserve here rather than the growth path (B-053).
  if (onSize && !onSize(body.size())) return FILE_ERROR;
  const auto fail = s.failures.find(url);
  const size_t stopAfter = fail == s.failures.end() ? body.size() : fail->second.afterBytes;

  // Deliver in chunks so a scripted failure lands MID-FILE, which is the only
  // interesting moment: the staging file already has bytes in it when the
  // download dies.
  constexpr size_t kChunk = 64;
  size_t sent = 0;
  while (sent < body.size() && sent < stopAfter) {
    const size_t n = std::min({kChunk, body.size() - sent, stopAfter - sent});
    if (!onData(reinterpret_cast<const uint8_t*>(body.data() + sent), n)) return ABORTED;
    sent += n;
  }
  if (fail != s.failures.end()) return static_cast<DownloadError>(fail->second.error);
  return OK;
}
