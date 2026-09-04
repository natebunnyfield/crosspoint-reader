#!/usr/bin/env python3
"""scripts/stamp_app_desc.py — the 32 bytes it writes, and the ones it must not.

B-033. That script edits a LINKED FIRMWARE IMAGE on the release path, which is
the most expensive place in this repository to be wrong: a malformed descriptor
or a stale hash produces an image the bootloader refuses, and the symptom
appears on a device rather than in a build log.

Every case here is one way to get that wrong:

  * writing the version but not re-hashing -> image rejected;
  * writing the version but not re-checksumming -> image rejected. B-046: this
    one SHIPPED, in every release from 1.5.17-BD to 1.5.21-BD, because the
    fixture below used to be a flat blob with no segment table and so could
    not tell a stale checksum byte from a fresh one;
  * writing past 32 bytes -> the bootloader-read fields behind it corrupted;
  * writing when the magic is absent -> a byte range clobbered on some future
    layout this script does not understand;
  * failing to NUL-terminate -> the version runs into project_name.

Synthetic images throughout: this must not need a real 5 MB build to run, or it
will not be run. But the synthetic image is a REAL image now -- header, one
segment, the padded checksum byte, the hash trailer -- and image_valid() below
re-implements the device validator's walk independently of the script, so the
script is never grading its own work.
"""
import functools
import hashlib
import importlib.util
import operator
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "scripts", "stamp_app_desc.py")

DESC_OFFSET = 0x20
DESC_MAGIC = 0xABCD5432
VERSION_OFFSET = DESC_OFFSET + 16

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print(f"  FAIL: {what}")


class FakeEnv(dict):
    """Enough SCons env for the script: CPPDEFINES and AddPostAction."""

    def __init__(self, version):
        super().__init__()
        self["CPPDEFINES"] = [("CROSSPOINT_VERSION", f'\\"{version}\\"'), "OTHER_FLAG"]
        self.post = None

    def AddPostAction(self, target, action):
        self.post = action


def load_script(version):
    """Import the script with Import()/env injected, as SCons would."""
    env = FakeEnv(version)
    spec = importlib.util.spec_from_file_location("stamp_app_desc", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    mod.Import = lambda _name: None
    mod.env = env
    sys.modules["stamp_app_desc"] = mod
    spec.loader.exec_module(mod)
    return mod, env


def xor(data, seed=0):
    return functools.reduce(operator.xor, data, seed)


def build_image(magic=DESC_MAGIC, version=b"1.0.0-OLD", hash_appended=1, checksum_ok=True,
                segment_overruns=False):
    """A minimal but real image, laid out the way esptool lays one out.

    24-byte header, ONE segment whose data begins with the app descriptor, an
    odd-length tail so the 16-byte pad is non-trivial, the XOR checksum in the
    pad's last byte, then a real SHA256 trailer. Deliberately-broken variants
    are what the refusal cases feed the script.
    """
    seg = bytearray(b"\0" * 256 + b"\x5a" * 37)
    struct.pack_into("<I", seg, 0, magic)
    struct.pack_into("<I", seg, 4, 0)                        # secure_version
    seg[16:48] = version.ljust(32, b"\0")
    seg[48:80] = b"crosspoint-reader".ljust(32, b"\0")
    struct.pack_into("<H", seg, 176, 0)                      # min_efuse_blk_rev_full
    struct.pack_into("<H", seg, 178, 199)                    # max_efuse_blk_rev_full
    seg[180] = 16                                            # mmu_page_size

    img = bytearray(24)
    img[0] = 0xE9
    img[1] = 1                                               # segment count
    img[23] = hash_appended
    declared = len(seg) + (1000 if segment_overruns else 0)
    img += struct.pack("<II", 0x3C000020, declared) + seg
    pad_end = (len(img) + 16) & ~15
    img += b"\0" * (pad_end - len(img))
    img[-1] = xor(seg, 0xEF) ^ (0 if checksum_ok else 0x5A)
    if hash_appended:
        img += hashlib.sha256(bytes(img)).digest()
    return bytes(img)


def image_valid(img):
    """The device validator's walk, written here rather than imported from the script."""
    if len(img) < 24 or img[0] != 0xE9:
        return False
    pos, acc = 24, 0xEF
    for _ in range(img[1]):
        if pos + 8 > len(img):
            return False
        n = struct.unpack_from("<I", img, pos + 4)[0]
        pos += 8
        if pos + n > len(img):
            return False
        acc = xor(img[pos:pos + n], acc)
        pos += n
    pad_end = (pos + 16) & ~15
    if pad_end + (32 if img[23] else 0) != len(img):
        return False
    if img[pad_end - 1] != acc:
        return False
    if img[23] and hashlib.sha256(img[:-32]).digest() != img[-32:]:
        return False
    return True


def run(mod, env, image):
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as fh:
        fh.write(image)
        path = fh.name
    try:
        mod.stamp(None, [path], env)
        with open(path, "rb") as fh:
            return fh.read()
    finally:
        os.unlink(path)


def field(img, off, length):
    return img[off:off + length].split(b"\0")[0].decode(errors="replace")


def main():
    mod, env = load_script("1.5.16-BD")

    # --- The fixture can tell: the rewrite must MOVE the checksum, or the
    # checksum cases below would pass against the stamper that shipped B-046.
    old_field = b"1.0.0-OLD".ljust(32, b"\0")
    new_field = b"1.5.16-BD".ljust(32, b"\0")
    check(xor(old_field) != xor(new_field), "the fixture's version rewrite changes the checksum")
    check(image_valid(build_image()), "the fixture itself is a valid image")

    # --- The version is written, and the image stays valid. ---
    out = run(mod, env, build_image())
    check(field(out, VERSION_OFFSET, 32) == "1.5.16-BD", "the version is written")
    check(hashlib.sha256(out[:-32]).digest() == out[-32:], "the appended sha256 is recomputed")
    check(image_valid(out), "the image checksum is recomputed (B-046)")
    check(out[0] == 0xE9, "the image header survives")

    # --- THE FIELDS THE BOOTLOADER READS ARE UNTOUCHED. ---
    check(struct.unpack_from("<I", out, DESC_OFFSET + 4)[0] == 0, "secure_version untouched")
    check(struct.unpack_from("<H", out, DESC_OFFSET + 176)[0] == 0, "min_efuse_blk_rev_full untouched")
    check(struct.unpack_from("<H", out, DESC_OFFSET + 178)[0] == 199, "max_efuse_blk_rev_full untouched")
    check(out[DESC_OFFSET + 180] == 16, "mmu_page_size untouched")
    check(field(out, DESC_OFFSET + 48, 32) == "crosspoint-reader", "project_name untouched")

    # --- Exactly 32 bytes move, plus the checksum byte, plus the 32-byte hash. Nothing else. ---
    before = build_image()
    after = run(mod, env, before)
    checksum_at = len(before) - 32 - 1
    diff = [i for i in range(len(before)) if before[i] != after[i]]
    check(checksum_at in diff, "the checksum byte is among the changed bytes")
    check(all(VERSION_OFFSET <= i < VERSION_OFFSET + 32 or i == checksum_at or i >= len(before) - 32
              for i in diff),
          "only the version field, the checksum byte and the hash differ")

    # --- No magic means no write. A layout this does not understand is left alone. ---
    nomagic = build_image(magic=0xDEADBEEF)
    check(run(mod, env, nomagic) == nomagic, "an image without the descriptor magic is untouched")

    # --- A checksum that is already wrong is not esptool's output: refused, untouched. ---
    badsum = build_image(checksum_ok=False)
    check(run(mod, env, badsum) == badsum, "an image whose checksum is already wrong is untouched")

    # --- A segment table that overruns the file: refused, untouched. ---
    overrun = build_image(segment_overruns=True)
    check(run(mod, env, overrun) == overrun, "an image whose segment table overruns the file is untouched")

    # --- Already correct: a no-op, and still valid. ---
    good = build_image(version=b"1.5.16-BD")
    check(run(mod, env, good) == good, "an already-correct image is left byte-identical")

    # --- No hash appended: patched, checksummed, and no phantom hash invented. ---
    nohash = build_image(hash_appended=0)
    outn = run(mod, env, nohash)
    check(field(outn, VERSION_OFFSET, 32) == "1.5.16-BD", "version written without an appended hash")
    check(len(outn) == len(nohash), "no hash is written when the image declares none")
    check(image_valid(outn), "the checksum is recomputed when no hash is appended")

    # --- A long version truncates and stays NUL-terminated. ---
    modl, envl = load_script("X" * 60)
    outl = run(modl, envl, build_image())
    check(len(field(outl, VERSION_OFFSET, 32)) == 31, "an over-long version truncates to 31 chars")
    check(outl[VERSION_OFFSET + 31] == 0, "the version field stays NUL-terminated")
    check(field(outl, DESC_OFFSET + 48, 32) == "crosspoint-reader",
          "an over-long version does not run into project_name")
    check(image_valid(outl), "an over-long version still yields a valid image")

    if failures:
        print(f"stamp_app_desc_test: {len(failures)} FAILURES")
        return 1
    print("stamp_app_desc_test: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
