#pragma once

#include <cstddef>

// Fills out with this unit's stable ID: the last 3 bytes of the factory
// (efuse) MAC as 6 lowercase hex chars, e.g. "ef0001". Unique per physical
// device — unlike the X3/X4 model type — so per-device SD-card files
// (/sleep_<id>.bmp) stay distinct between two units of the same model.
// outSize must be >= 7; the result is always null-terminated.
void getDeviceIdHex(char* out, size_t outSize);
