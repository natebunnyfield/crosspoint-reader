# Upload ONLY the application image, at the ota_0 offset taken from the
# partition table. For boards that keep their vendor bootloader, partition
# table, otadata and recovery partition (Onyx BOOX Picco): pio's default upload
# would also write the Arduino bootloader at 0x0, the partition table at 0x8000
# and boot_app0.bin at the otadata offset (0xE000 on the prebuilt-core path,
# which is INSIDE the Picco's stock nvs at 0x9000-0x15000).
#
# Ported from upstream 261726f61 (crosspoint-reader #3947) with the fix for
# upstream issue #4248. Upstream's version only did
#
#     env.Replace(FLASH_EXTRA_IMAGES=[])
#
# which is too late: this is a `post:` script, and by the time it runs the
# platform's builder/main.py (pioarduino 55.03.x, esptool branch) has ALREADY
# looped over FLASH_EXTRA_IMAGES and appended every "<offset> <path>" pair to
# UPLOADERFLAGS. Clearing the list afterwards changes nothing, so
# `pio run -e picco -t upload` still wrote the bootloader, the partition table
# and boot_app0.bin, destroying the vendor bootloader. This script also strips
# those already-appended pairs (esptool form and openocd `program_esp` form),
# then REFUSES the upload if anything other than the app is still on the
# command line, or if the app offset is not the partition table's ota_0.
#
# It also refuses the targets that cannot be made app-only: `erase` and
# `erase_upload` erase the whole chip (vendor bootloader, NVS and the safeboot
# recovery partition with it), and `uploadfs` / `uploadfsota` overwrite the
# stock spiffs partition. CrossPoint must only ever write the app slot.
#
# The pure helpers below take plain lists so they can be exercised without
# SCons; the SCons entry point is at the bottom.


def _unix(path):
    return str(path).replace("\\", "/")


def strip_extra_image_flags(flags, extra_images):
    """Remove every extra image the platform appended to UPLOADERFLAGS.

    `flags` is the UPLOADERFLAGS list; `extra_images` is a list of
    (offset, substituted_path) pairs, i.e. FLASH_EXTRA_IMAGES after env.subst().
    Returns (new_flags, removed_count).
    """
    pairs = {(str(offset), str(path)) for offset, path in extra_images}
    openocd = {
        "program_esp {%s} %s verify" % (_unix(path), offset) for offset, path in extra_images
    }
    out = []
    removed = 0
    i = 0
    n = len(flags)
    while i < n:
        cur = str(flags[i])
        nxt = str(flags[i + 1]) if i + 1 < n else None
        # esptool: ... "<offset>" "<path>" ...
        if nxt is not None and (cur, nxt) in pairs:
            removed += 1
            i += 2
            continue
        # openocd: ... "-c" "program_esp {<path>} <offset> verify" ...
        if cur == "-c" and nxt is not None and nxt in openocd:
            removed += 1
            i += 2
            continue
        out.append(flags[i])
        i += 1
    return out, removed


def leftover_image_flags(flags, extra_images):
    """Flags that still name an extra image after stripping (must be empty)."""
    paths = set()
    for _, path in extra_images:
        paths.add(str(path))
        paths.add(_unix(path))
    return [f for f in flags if str(f) in paths or any(p and p in str(f) for p in paths)]


def ota0_offset_from_csv(csv_text):
    """The ota_0 partition offset from a partition-table CSV, as an int (or None)."""
    for line in csv_text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        tokens = [t.strip() for t in line.split(",")]
        if len(tokens) >= 5 and tokens[2] == "ota_0" and tokens[3]:
            return int(tokens[3], 0)
    return None


REFUSED_TARGETS = ("erase", "erase_upload", "uploadfs", "uploadfsota")


def _scons_main(env):
    import sys

    from SCons.Script import COMMAND_LINE_TARGETS  # noqa: F401  (SCons-only import)

    pioenv = env.subst("$PIOENV")
    refused = [t for t in REFUSED_TARGETS if t in COMMAND_LINE_TARGETS]
    if refused:
        sys.stderr.write(
            "ERROR: scripts/app_only_upload.py: target(s) %s refused for env '%s'. "
            "This board keeps its vendor bootloader, NVS and recovery partition; "
            "only `-t upload` (the app, at ota_0) is allowed.\n" % (", ".join(refused), pioenv)
        )
        env.Exit(1)

    extra = [(str(offset), env.subst(path)) for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
    flags = list(env.get("UPLOADERFLAGS", []))
    new_flags, removed = strip_extra_image_flags(flags, extra)
    env.Replace(UPLOADERFLAGS=new_flags, FLASH_EXTRA_IMAGES=[])

    if "upload" not in COMMAND_LINE_TARGETS:
        return

    # Fail closed: nothing but the app may remain on the uploader command line.
    leftover = leftover_image_flags(new_flags, extra)
    if leftover:
        sys.stderr.write(
            "ERROR: scripts/app_only_upload.py: extra images still on the uploader command line "
            "after stripping: %s. Refusing to upload.\n" % leftover
        )
        env.Exit(1)

    csv_path = env.subst("$PARTITIONS_TABLE_CSV")
    with open(csv_path) as fp:
        ota0 = ota0_offset_from_csv(fp.read())
    app_offset = env.subst("$ESP32_APP_OFFSET")
    if ota0 is None or not app_offset or int(app_offset, 0) != ota0:
        sys.stderr.write(
            "ERROR: scripts/app_only_upload.py: app offset %r does not match ota_0 %r in %s. "
            "Refusing to upload.\n" % (app_offset, hex(ota0) if ota0 is not None else None, csv_path)
        )
        env.Exit(1)

    print(
        "app_only_upload: stripped %d extra image(s); uploading only the app at %s" % (removed, app_offset)
    )


try:
    Import("env")  # noqa: F821  (injected by SCons)
except NameError:  # imported outside SCons (host check of the helpers above)
    env = None

if env is not None:
    _scons_main(env)
