#!/usr/bin/env python3
"""scripts/stamp_app_desc.py — the 32 bytes it writes, and the ones it must not.

B-033. That script edits a LINKED FIRMWARE IMAGE on the release path, which is
the most expensive place in this repository to be wrong: a malformed descriptor
or a stale hash produces an image the bootloader refuses, and the symptom
appears on a device rather than in a build log.

Every case here is one way to get that wrong:

  * writing the version but not re-hashing -> image rejected;
  * writing past 32 bytes -> the bootloader-read fields behind it corrupted;
  * writing when the magic is absent -> a byte range clobbered on some future
    layout this script does not understand;
  * failing to NUL-terminate -> the version runs into project_name.

Synthetic images throughout: this must not need a real 5 MB build to run, or it
will not be run.
"""
import hashlib
import importlib.util
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


def build_image(magic=DESC_MAGIC, version=b"1.0.0-OLD", hash_appended=1):
    """A minimal image with a valid header, a descriptor, and a real SHA256."""
    img = bytearray(b"\0" * (DESC_OFFSET + 256 + 64))
    img[0] = 0xE9
    img[1] = 7
    img[23] = hash_appended
    struct.pack_into("<I", img, DESC_OFFSET, magic)
    struct.pack_into("<I", img, DESC_OFFSET + 4, 0)          # secure_version
    img[VERSION_OFFSET:VERSION_OFFSET + 32] = version.ljust(32, b"\0")
    img[DESC_OFFSET + 48:DESC_OFFSET + 80] = b"crosspoint-reader".ljust(32, b"\0")
    struct.pack_into("<H", img, DESC_OFFSET + 176, 0)        # min_efuse_blk_rev_full
    struct.pack_into("<H", img, DESC_OFFSET + 178, 199)      # max_efuse_blk_rev_full
    img[DESC_OFFSET + 180] = 16                              # mmu_page_size
    if hash_appended:
        img[-32:] = hashlib.sha256(bytes(img[:-32])).digest()
    return bytes(img)


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

    # --- The version is written, and the image stays valid. ---
    out = run(mod, env, build_image())
    check(field(out, VERSION_OFFSET, 32) == "1.5.16-BD", "the version is written")
    check(hashlib.sha256(out[:-32]).digest() == out[-32:], "the appended sha256 is recomputed")
    check(out[0] == 0xE9, "the image header survives")

    # --- THE FIELDS THE BOOTLOADER READS ARE UNTOUCHED. ---
    check(struct.unpack_from("<I", out, DESC_OFFSET + 4)[0] == 0, "secure_version untouched")
    check(struct.unpack_from("<H", out, DESC_OFFSET + 176)[0] == 0, "min_efuse_blk_rev_full untouched")
    check(struct.unpack_from("<H", out, DESC_OFFSET + 178)[0] == 199, "max_efuse_blk_rev_full untouched")
    check(out[DESC_OFFSET + 180] == 16, "mmu_page_size untouched")
    check(field(out, DESC_OFFSET + 48, 32) == "crosspoint-reader", "project_name untouched")

    # --- Exactly 32 bytes move, plus the 32-byte hash. Nothing else. ---
    before = build_image()
    after = run(mod, env, before)
    diff = [i for i in range(len(before)) if before[i] != after[i]]
    check(all(VERSION_OFFSET <= i < VERSION_OFFSET + 32 or i >= len(before) - 32 for i in diff),
          "only the version field and the hash differ")

    # --- No magic means no write. A layout this does not understand is left alone. ---
    nomagic = build_image(magic=0xDEADBEEF)
    check(run(mod, env, nomagic) == nomagic, "an image without the descriptor magic is untouched")

    # --- Already correct: a no-op, and still valid. ---
    good = build_image(version=b"1.5.16-BD")
    check(run(mod, env, good) == good, "an already-correct image is left byte-identical")

    # --- No hash appended: patched, and no phantom hash invented. ---
    nohash = build_image(hash_appended=0)
    outn = run(mod, env, nohash)
    check(field(outn, VERSION_OFFSET, 32) == "1.5.16-BD", "version written without an appended hash")
    check(outn[-32:] == nohash[-32:], "no hash is written when the image declares none")

    # --- A long version truncates and stays NUL-terminated. ---
    modl, envl = load_script("X" * 60)
    outl = run(modl, envl, build_image())
    check(len(field(outl, VERSION_OFFSET, 32)) == 31, "an over-long version truncates to 31 chars")
    check(outl[VERSION_OFFSET + 31] == 0, "the version field stays NUL-terminated")
    check(field(outl, DESC_OFFSET + 48, 32) == "crosspoint-reader",
          "an over-long version does not run into project_name")

    if failures:
        print(f"stamp_app_desc_test: {len(failures)} FAILURES")
        return 1
    print("stamp_app_desc_test: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
