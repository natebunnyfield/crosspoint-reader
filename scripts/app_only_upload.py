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
# Those refusals run as a pre-action of `upload`, NOT while this script is
# read. At read time the esptool branch has not parsed the partition table:
# ESP32_APP_OFFSET is still None (builder/main.py:864; `-t envdump` shows it),
# and it only becomes 0xe0000 when _update_max_upload_size(), a pre-action of
# `checkprogsize` (which `upload` depends on), runs _parse_partitions(). The
# first version checked at read time and so refused EVERY upload ("app offset
# '' does not match ota_0"), 2026-11-05.
#
# `pio run -e picco -t upload_plan` builds, runs the same checks and prints the
# exact command `-t upload` would run, without looking for a port or running
# it, so the write can be inspected before a device is attached.
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


def address_like_flags(flags):
    """Flags that look like a flash address (0x...). The base esptool flags carry
    none, so after stripping any such token is an image this script did not know
    about; the app's own offset arrives separately, as $ESP32_APP_OFFSET."""
    out = []
    for f in flags:
        s = str(f).strip().strip('"')
        if s[:2].lower() == "0x":
            try:
                int(s, 16)
            except ValueError:
                continue
            out.append(s)
    return out


def app_only_problems(final_flags, extra_images, app_offset, ota0):
    """Every reason the upload would write something other than the app at ota_0.

    `final_flags` are the substituted UPLOADERFLAGS at action time, `app_offset`
    the substituted $ESP32_APP_OFFSET then, `ota0` the partition table's ota_0.
    Empty list = the command writes exactly one image, at ota_0.
    """
    problems = []
    leftover = leftover_image_flags(final_flags, extra_images)
    if leftover:
        problems.append("extra images still on the command line: %s" % leftover)
    stray = address_like_flags(final_flags)
    if stray:
        problems.append("flash addresses still on the command line: %s" % stray)
    try:
        offset = int(app_offset, 0) if app_offset else None
    except ValueError:
        offset = None
    if ota0 is None or offset is None or offset != ota0:
        problems.append(
            "app offset %r does not match ota_0 %r" % (app_offset, hex(ota0) if ota0 is not None else None)
        )
    return problems


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

    csv_path = env.subst("$PARTITIONS_TABLE_CSV")
    with open(csv_path) as fp:
        ota0 = ota0_offset_from_csv(fp.read())

    # Runs at ACTION time (see the header): by then `checkprogsize` has parsed
    # the partition table and $ESP32_APP_OFFSET holds the offset UPLOADCMD uses.
    def _check(source, env):
        final_flags = [env.subst(str(f)) for f in env.get("UPLOADERFLAGS", [])]
        app_offset = env.subst("$ESP32_APP_OFFSET")
        command = env.subst("$UPLOADCMD", source=source)
        return app_only_problems(final_flags, extra, app_offset, ota0), app_offset, command

    def _verify_before_upload(target, source, env):
        problems, app_offset, _ = _check(source, env)
        if problems:
            sys.stderr.write(
                "ERROR: scripts/app_only_upload.py: refusing to upload (%s): %s.\n"
                % (csv_path, "; ".join(problems))
            )
            return 1
        print(
            "app_only_upload: stripped %d extra image(s); uploading only %s at %s"
            % (removed, source[0], app_offset)
        )
        return 0

    def _print_upload_plan(target, source, env):
        problems, app_offset, command = _check(source, env)
        print("app_only_upload: stripped %d extra image(s) the platform appended: %s" % (removed, extra))
        ota0_text = hex(ota0) if ota0 is not None else None
        print("app_only_upload: ota_0 in %s = %s; $ESP32_APP_OFFSET = %s" % (csv_path, ota0_text, app_offset))
        print("app_only_upload: `-t upload` would run (after port detection):\n  %s" % command)
        if problems:
            sys.stderr.write("ERROR: scripts/app_only_upload.py: upload would be REFUSED: %s.\n" % "; ".join(problems))
            return 1
        print("app_only_upload: OK, one image: %s at %s" % (source[0], app_offset))
        return 0

    if "upload" in COMMAND_LINE_TARGETS:
        env.AddPreAction("upload", env.VerboseAction(_verify_before_upload, "Checking the upload writes only the app"))

    plan = env.AddCustomTarget(
        name="upload_plan",
        dependencies="$BUILD_DIR/${PROGNAME}.bin",
        actions=env.VerboseAction(_print_upload_plan, "Planning the app-only upload"),
        title="Upload plan (no device)",
        description="Build, then print the exact app-only upload command without running it",
    )
    env.Depends(plan, "checkprogsize")


try:
    Import("env")  # noqa: F821  (injected by SCons)
except NameError:  # imported outside SCons (host check of the helpers above)
    env = None

if env is not None:
    _scons_main(env)
