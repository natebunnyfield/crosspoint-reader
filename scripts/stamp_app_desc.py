"""Write the real version into the ESP-IDF app descriptor, post-link.

B-033. The descriptor's `version` field is supposed to carry this build's
version. It does not: `esp_app_desc.c.o` arrives PREBUILT inside
`~/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/lib/libesp_app_format.a`,
already compiled -- on 2026-08-21, against this project -- so every image since
has shipped `1.5.1-B2-43-g7211621a3` no matter what the source tree says. That
string is what OTA metadata and the web UI's build line report. The BOOT SCREEN
was always right, because that reads `CROSSPOINT_VERSION`, a define this repo
owns.

WHY A POST-BUILD PATCH RATHER THAN A SOURCE OVERRIDE. `esp_app_desc` is a weak
symbol, so a strong definition here would win at link time -- but overriding it
means supplying the WHOLE struct, and its tail is not padding:
`min_efuse_blk_rev_full`, `max_efuse_blk_rev_full` and `mmu_page_size` are read
by the bootloader. Hardcoding them would fix a stale string by introducing a
stale struct -- the identical bug one field over, and the next framework bump
would silently invalidate it. This patch touches THIRTY-TWO BYTES and leaves
every field the bootloader reads exactly as Espressif's build computed it.

WHAT IT DOES, and each step refuses rather than guesses:

  1. finds the descriptor at offset 0x20 (24-byte image header + 8-byte segment
     header) and verifies its magic is 0xABCD5432 -- if the layout ever moves,
     this does nothing rather than corrupting a byte range;
  2. walks the segment table to the image's XOR checksum byte (the last byte of
     the 16-byte padding after the final segment) and confirms it matches the
     segment data as esptool wrote it -- a layout that does not add up to the
     file size, or a checksum that is already wrong, is not esptool's output
     and is left alone;
  3. writes the version, NUL-padded to 32 bytes, truncating at 31 characters so
     the field is always terminated;
  4. rewrites the checksum byte for the patched segment data. B-046: this step
     was missing, and every release from 1.5.17-BD to 1.5.21-BD shipped with the
     byte stale -- the device validator, esp_ota_end() and the bootloader all
     check it, so no install path accepted those images;
  5. recomputes the appended SHA256 when the image declares one (extended
     header byte 23), because the descriptor lives inside a hashed segment and a
     patched image with a stale hash is an image the bootloader rejects.

A patched image is byte-identical to the original except those 32 bytes, the
checksum byte and the 32-byte hash.
"""
import functools
import hashlib
import operator
import struct

Import("env")  # noqa: F821

DESC_OFFSET = 0x20
DESC_MAGIC = 0xABCD5432
VERSION_OFFSET = DESC_OFFSET + 16
VERSION_LEN = 32
HASH_APPENDED_BYTE = 23
HASH_LEN = 32

IMAGE_MAGIC = 0xE9
HEADER_LEN = 24
SEG_COUNT_BYTE = 1
SEG_HEADER_LEN = 8
CHECKSUM_SEED = 0xEF


def walk_segments(image):
    """(offset of the checksum byte, XOR of the segment data), or None.

    The ESP image layout: a 24-byte header whose byte 1 is the segment count;
    each segment as an 8-byte header (load address, data length) followed by
    its data; padding to the next 16-byte boundary whose LAST byte is the XOR
    of every segment data byte seeded with 0xEF; then, when header byte 23 is
    set, a 32-byte SHA256 of everything before it. This is the same walk the
    device's validator (src/network/FirmwareImageValidator.cpp) and the
    bootloader perform, and it refuses on the same conditions.
    """
    if len(image) < HEADER_LEN or image[0] != IMAGE_MAGIC:
        return None
    pos = HEADER_LEN
    xor = CHECKSUM_SEED
    for _ in range(image[SEG_COUNT_BYTE]):
        if pos + SEG_HEADER_LEN > len(image):
            return None
        (length,) = struct.unpack_from("<I", image, pos + 4)
        pos += SEG_HEADER_LEN
        if pos + length > len(image):
            return None
        xor ^= functools.reduce(operator.xor, image[pos:pos + length], 0)
        pos += length
    pad_end = (pos + 16) & ~15
    trailer = HASH_LEN if image[HASH_APPENDED_BYTE] else 0
    if pad_end + trailer != len(image):
        return None
    return pad_end - 1, xor


def _version_from_flags(env):
    """The CROSSPOINT_VERSION define this build is using, or None.

    Read from the build flags rather than recomputed, so this can never disagree
    with what the boot screen shows -- making them agree is the entire point.
    """
    for item in env.get("CPPDEFINES", []):
        if isinstance(item, (list, tuple)) and len(item) == 2 and item[0] == "CROSSPOINT_VERSION":
            return str(item[1]).strip().strip('\\"').strip('"')
    return None


def stamp(source, target, env):
    path = str(target[0])
    if not path.endswith(".bin"):
        return

    version = _version_from_flags(env)
    if not version:
        print("stamp_app_desc: no CROSSPOINT_VERSION define found; leaving the descriptor alone")
        return

    with open(path, "rb") as fh:
        image = bytearray(fh.read())

    if len(image) < DESC_OFFSET + 256:
        print("stamp_app_desc: image too short for an app descriptor; skipped")
        return

    magic = struct.unpack_from("<I", image, DESC_OFFSET)[0]
    if magic != DESC_MAGIC:
        print(f"stamp_app_desc: no descriptor magic at 0x{DESC_OFFSET:X} "
              f"(found 0x{magic:08X}); skipped rather than guessed")
        return

    walked = walk_segments(image)
    if walked is None:
        print("stamp_app_desc: segment table does not add up to the file size; skipped rather than guessed")
        return
    checksum_at, checksum_was = walked
    if image[checksum_at] != checksum_was:
        print(f"stamp_app_desc: stored checksum 0x{image[checksum_at]:02X} != computed 0x{checksum_was:02X} "
              "before any patch; not esptool's output, skipped rather than guessed")
        return

    was = bytes(image[VERSION_OFFSET:VERSION_OFFSET + VERSION_LEN]).split(b"\0")[0].decode(errors="replace")
    if was == version:
        return  # already correct; nothing to do and nothing to say

    field = version.encode()[: VERSION_LEN - 1].ljust(VERSION_LEN, b"\0")
    image[VERSION_OFFSET:VERSION_OFFSET + VERSION_LEN] = field

    # The descriptor is segment data, so the rewrite moved the checksum.
    # Recompute from the data rather than trusting arithmetic on the old byte.
    _, checksum_now = walk_segments(image)
    image[checksum_at] = checksum_now

    if image[HASH_APPENDED_BYTE] == 1:
        image[-HASH_LEN:] = hashlib.sha256(bytes(image[:-HASH_LEN])).digest()
        rehashed = ", sha256 recomputed"
    else:
        rehashed = ""

    with open(path, "wb") as fh:
        fh.write(bytes(image))

    print(f"stamp_app_desc: app descriptor version {was!r} -> {version!r}, "
          f"checksum 0x{checksum_was:02X} -> 0x{checksum_now:02X}{rehashed}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", stamp)  # noqa: F821
