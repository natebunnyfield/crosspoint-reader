#pragma once

#include <spi_flash_mmap.h>

#include <cstddef>
#include <cstdint>

// The ESP image layout constants, shared by the two halves of the firmware
// updater.
//
// They lived in an anonymous namespace inside FirmwareFlasher.cpp until
// validateImageFile() was split out into its own translation unit (S-014 in the
// simulator repo). The split exists because the two halves have very different
// requirements: the validator only READS a file and does arithmetic, while the
// flasher erases and writes flash and moves the boot pointer. A host simulator
// can run the first and must never run the second, and while they shared a .cpp
// neither could be had without the other.
//
// Declared at `firmware_flash` scope rather than in a nested namespace so the
// existing unqualified uses in both files keep resolving — the split is meant to
// move code, not rewrite it.
namespace firmware_flash {

constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;
constexpr size_t MIN_FIRMWARE_SIZE = 64 * 1024;
constexpr size_t SEC = SPI_FLASH_SEC_SIZE;  // 4 KiB
constexpr size_t BLK = 64 * 1024;           // 64 KiB block-erase granularity
constexpr size_t CHUNK = 4096;
constexpr size_t SHA_TRAILER = 32;
constexpr uint8_t CHECKSUM_SEED = 0xEF;
constexpr size_t HEADER_SIZE = 24;
constexpr size_t SEG_HEADER_SIZE = 8;

}  // namespace firmware_flash
