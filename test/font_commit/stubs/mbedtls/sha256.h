// Host SHA-256 for the font-commit suite. mbedtls is not linked here, so this
// is a straight implementation of FIPS 180-4 with mbedtls's function names.
//
// It is the REAL algorithm rather than a deterministic placeholder on purpose:
// the manifest this suite feeds FontUpdater carries digests, and a placeholder
// would only prove the updater compares its own output with itself. The suite
// asserts the empty-string vector (e3b0c442...) before anything else, so a
// wrong implementation fails loudly instead of agreeing with itself.
#pragma once

#include <cstddef>
#include <cstdint>

struct mbedtls_sha256_context {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t buffer[64];
  size_t buflen;
};

void mbedtls_sha256_init(mbedtls_sha256_context* ctx);
void mbedtls_sha256_free(mbedtls_sha256_context* ctx);
int mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int is224);
int mbedtls_sha256_update(mbedtls_sha256_context* ctx, const unsigned char* input, size_t ilen);
int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, unsigned char output[32]);
