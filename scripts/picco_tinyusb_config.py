# Make a USB Drive build (-DFREEINK_CAP_USB_MSC=1) work on top of the fork's
# custom_sdkconfig core rebuild: option B' in docs/usb-drive-picco.md ("Build
# config"), where every claim here is measured.
#
# NOT WIRED INTO ANY ENV. The owner rules between B' (this script) and A (the
# Picco on the prebuilt core, which needs no script). For B', add
# `pre:scripts/picco_tinyusb_config.py` to [env:picco]'s extra_scripts together
# with the USB lines the doc lists. Until then this file does nothing.
#
# Two problems, one build middleware:
#
# 1. The rebuilt core has no TinyUSB configuration. custom_sdkconfig makes
#    pioarduino rebuild the ESP-IDF libraries for the board's memory variant,
#    and that rebuild has no `arduino_tinyusb` component -- it exists only in
#    Espressif's lib-builder, and so does the Kconfig that defines every
#    CONFIG_TINYUSB_* symbol. Kconfig drops symbols nothing defines, so the
#    rebuilt <variant>/include/sdkconfig.h has none of them even when
#    custom_sdkconfig sets them (31 CONFIG_TINYUSB_* lines into the generated
#    sdkconfig.defaults, 0 out). USB.cpp, USBMSC.cpp and esp32-hal-tinyusb.c
#    then compile to nothing and the SDK's UsbMassStorage cannot compile
#    (USBMSC.h declares its class only under CONFIG_TINYUSB_MSC_ENABLED).
#    The prebuilt libarduino_tinyusb.a is still in the package (the rebuild
#    replaces only the archives it compiles) and still linked
#    (-larduino_tinyusb in pioarduino-build.py). It was compiled against the
#    STOCK sdkconfig, so its numeric CONFIG_TINYUSB_* values (class enables,
#    buffer sizes, CDC port count) are read from a stock variant header of the
#    same package and added to every compile, where tusb_config.h and the
#    Arduino core read them. Read from the package, never typed here, so a
#    platform upgrade cannot leave the two out of step. String values
#    (descriptor names) are not ABI and are left out.
#
# 2. The core rebuild compiles the project's lib_deps too, with the project's
#    -D flags (PlatformIO turns BUILD_FLAGS into CPPDEFINES before any framework
#    script runs, piobuild.py ProcessProgramDeps), against that TinyUSB-less
#    config. With FREEINK_CAP_USB_MSC=1 the SDK's UsbMassStorage.cpp fails
#    there ("'USBMSC' does not name a type"), so the rebuild itself fails and
#    the build never gets past it. Those rebuild-pass objects are thrown away
#    (espidf.py deletes the build dir once the libraries are copied), so during
#    that pass alone the USB Drive defines are stripped from each library
#    source.
#
# Why a middleware and not BUILD_FLAGS: a pre: script cannot tell the two
# passes apart (pioarduino decides to rebuild after it runs), and flags added
# in pre: would also reach the rebuild's ESP-IDF components, among them the
# Arduino core's esp32-hal-tinyusb.c, which has no TinyUSB headers there. The
# middleware runs per source file, after the framework script has set
# ARDUINO_LIB_COMPILE_FLAG ("Build" during the core rebuild, espidf.py:821;
# "Inactive" otherwise, main.py:867), and only on files PlatformIO itself
# compiles (piobuild.py CollectBuildFiles, 307-326): the Arduino core, the
# libraries and src/. The ESP-IDF components the rebuild compiles never pass
# through it.
#
# The build fails (fail closed) when the stock header or the prebuilt archive
# is missing, or the stock values do not enable TinyUSB with MSC. It does
# nothing in an env without -DFREEINK_CAP_USB_MSC=1.

import re
from pathlib import Path

Import("env")  # noqa: F821  (SCons)

TAG = "picco_tinyusb_config"
# A variant no env in this fork rebuilds: the Picco rebuilds dio_opi, the
# Sticky qio_opi, and the C3 envs use the esp32c3 tree.
STOCK_VARIANT = "qio_qspi"
DEFINE = re.compile(r"^#define\s+(CONFIG_TINYUSB_[A-Z0-9_]+)\s+(-?(?:0x[0-9A-Fa-f]+|[0-9]+))\s*$")
USB_DRIVE_FLAG = "FREEINK_CAP_USB_MSC"


def _usb_drive_build(flags):
    return any(str(f).replace(" ", "") in (f"-D{USB_DRIVE_FLAG}=1", f"-D{USB_DRIVE_FLAG}") for f in flags)


def _read_defines(header):
    values = {}
    for line in header.read_text(encoding="utf-8", errors="replace").splitlines():
        m = DEFINE.match(line.strip())
        if m:
            values[m.group(1)] = m.group(2)
    return values


def _define_name(entry):
    if isinstance(entry, (tuple, list)):
        return str(entry[0]) if entry else ""
    return str(entry).split("=", 1)[0]


def _fail(message):
    print(f"ERROR: scripts/{TAG}.py: {message}")
    env.Exit(1)  # noqa: F821


def _tinyusb_values():
    libs = env.PioPlatform().get_package_dir("framework-arduinoespressif32-libs")  # noqa: F821
    if not libs:
        _fail("framework-arduinoespressif32-libs is not installed")
    board = env.BoardConfig()  # noqa: F821
    root = Path(libs) / board.get("build.mcu", "")
    stock_header = root / STOCK_VARIANT / "include" / "sdkconfig.h"
    archive = root / "lib" / "libarduino_tinyusb.a"
    if not stock_header.is_file():
        _fail(f"no stock sdkconfig.h at {stock_header}")
    if not archive.is_file():
        _fail(f"no prebuilt {archive.name} at {archive}: nothing would provide TinyUSB")
    stock = _read_defines(stock_header)
    if stock.get("CONFIG_TINYUSB_ENABLED") != "1" or stock.get("CONFIG_TINYUSB_MSC_ENABLED") != "1":
        _fail(
            f"{stock_header} does not enable TinyUSB with MSC (has the {STOCK_VARIANT} variant been "
            "rebuilt?); USB Drive cannot be configured from it"
        )
    # Never shadow a value the active variant's own sdkconfig.h defines: a
    # future core rebuild that keeps TinyUSB carries its own, and those win.
    active_header = root / board.get("build.arduino.memory_type", "") / "include" / "sdkconfig.h"
    active = _read_defines(active_header) if active_header.is_file() else {}
    return {k: v for k, v in stock.items() if k not in active}, len(active)


def main():
    if not _usb_drive_build(env.get("BUILD_FLAGS", [])):  # noqa: F821
        return

    values, already = _tinyusb_values()
    extra = [(k, v) for k, v in sorted(values.items())]
    print(
        f"{TAG}: {len(extra)} CONFIG_TINYUSB_* values from the stock {STOCK_VARIANT} sdkconfig.h "
        f"({already} already in the active variant); MSC={values.get('CONFIG_TINYUSB_MSC_ENABLED', 'active')}, "
        f"MSC_BUFSIZE={values.get('CONFIG_TINYUSB_MSC_BUFSIZE', 'active')}"
    )
    announced = {"rebuild": False}

    # The middleware edits the CPPDEFINES of the env that compiles the file (a
    # library's, the Arduino core's or src/'s) and hands the SAME File node on.
    # It must not return env.Object(...): pioarduino registers a middleware of
    # its own in the core-rebuild pass (espidf.py _skip_prj_source_files, which
    # calls node.srcnode()) that runs after this one and fails on a NodeList.
    # Every file of an env passes through here before that env compiles
    # anything (CollectBuildFiles runs the middlewares, then BuildSources /
    # BuildLibrary compile with the same env), so each env is changed before
    # its first compile.
    def middleware(node_env, node):
        if node_env.subst("$ARDUINO_LIB_COMPILE_FLAG") == "Build":
            if not announced["rebuild"]:
                announced["rebuild"] = True
                print(f"{TAG}: core rebuild pass, compiling libraries without the USB Drive defines")
            raw = node_env.get("CPPDEFINES", [])
            defines = list(raw.items()) if isinstance(raw, dict) else list(raw)
            kept = [
                d for d in defines if _define_name(d) != USB_DRIVE_FLAG and not _define_name(d).startswith("CONFIG_TINYUSB_")
            ]
            if len(kept) != len(defines):
                node_env.Replace(CPPDEFINES=kept)
            return node
        node_env.AppendUnique(CPPDEFINES=extra)
        return node

    env.AddBuildMiddleware(middleware)  # noqa: F821


main()
