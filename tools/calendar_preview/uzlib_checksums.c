#include <stdint.h>
#include <stddef.h>
/* Checksums are unused on the font-decompression path (raw inflate). */
uint32_t uzlib_adler32(const void *d, unsigned int l, uint32_t p) { (void)d;(void)l; return p; }
uint32_t uzlib_crc32(const void *d, unsigned int l, uint32_t p) { (void)d;(void)l; return p; }
