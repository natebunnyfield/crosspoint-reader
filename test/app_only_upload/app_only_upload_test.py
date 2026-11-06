#!/usr/bin/env python3
"""scripts/app_only_upload.py -- the PSRAM link check that gates the Picco upload.

The build trap (header of the script): a Picco build can compile against one
config's sdkconfig.h and link another config's framework libs. That image
builds SUCCESS with plausible sizes and never initializes PSRAM, and the only
place it shows is the linker map: `esp_psram_init` sits under "Discarded input
sections" instead of in the memory map. `-t upload` is itself a build, so the
upload refuses unless the map of that link places esp_psram_init and the octal
driver.

Every case is one way to get that wrong, in either direction:

  * passing the trap image (esp_psram_init only discarded) -> a PSRAM-less
    firmware is written to the device;
  * trusting the cross-reference table or the "Archive member included" lines,
    which name esp_psram_init whether or not --gc-sections kept it;
  * passing a quad driver, or no driver;
  * refusing a coherent image, including the stock-framework shape where
    cpu_start (CONFIG_SPIRAM_BOOT_INIT) is the caller, not esp32-hal-psram ->
    the env can never upload;
  * reading a map left over from an earlier link.

The maps are synthetic but every line is copied in shape from the real
.pio/build/picco/firmware.map (pioarduino 55.03.37, xtensa ld, --cref).
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "..", "scripts", "app_only_upload.py")

LIB = "/Users/x/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/lib"
HAL_PSRAM = ".pio/build/picco/libFrameworkArduino.a(esp32-hal-psram.c.o)"
CPU_START = LIB + "/libesp_system.a(cpu_start.c.o)"
PSRAM_OBJ = LIB + "/libesp_psram.a(esp_psram.c.o)"

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print(f"  FAIL: {what}")


def load_script():
    spec = importlib.util.spec_from_file_location("app_only_upload", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)  # outside SCons: Import() raises NameError, env stays None
    return mod


def make_map(init_placed=True, init_addr=0x420FD6B8, driver="octal", driver_addr=0x403818F8, caller=HAL_PSRAM):
    """A GNU ld map with the four parts in their real order.

    init_placed=False is the trap: .text.esp_psram_init only under "Discarded
    input sections", while the archive and cref parts still name it.
    driver=None leaves no PSRAM driver in the image at all.
    """
    drv_obj = "%s/libesp_psram.a(esp_psram_impl_%s.c.o)" % (LIB, driver)
    out = [
        "Archive member included to satisfy reference by file (symbol)",
        "",
        PSRAM_OBJ,
        "                              %s (esp_psram_init)" % caller,
    ]
    if driver:
        out += [drv_obj, "                              %s (esp_psram_impl_get_cs_io)" % PSRAM_OBJ]
    out += [
        "",
        "Allocating common symbols",
        "Common symbol       size              file",
        "",
        "Discarded input sections",
        "",
        " .literal.esp_psram_get_size",
        "                0x00000000        0x4 %s" % PSRAM_OBJ,
        " .text          0x00000000        0x0 %s" % PSRAM_OBJ,
    ]
    if not init_placed:
        out += [" .text.esp_psram_init", "                0x00000000      0x176 %s" % PSRAM_OBJ]
    out += [
        "",
        "Memory Configuration",
        "",
        "Name             Origin             Length             Attributes",
        "iram0_0_seg      0x40370000         0x00060000         xrw",
        "",
        "Linker script and memory map",
        "",
        "LOAD %s/libesp_psram.a" % LIB,
        ".iram0.text     0x40374000    0x13b33 load address 0x00010020",
        " *fill*         0x403818f8        0x0 ",
    ]
    if driver:
        out += [
            " *libesp_psram.a:esp_psram_impl_octal.*(.literal .literal.* .text .text.*)",
            " .text.esp_psram_impl_enable",
            "                0x%08x      0x3ed %s" % (driver_addr, drv_obj),
            "                                0x404 (size before relaxing)",
            "                0x%08x                esp_psram_impl_enable" % driver_addr,
            " *fill*         0x40381ce5        0x3 ",
            " .text.esp_psram_impl_get_cs_io",
            "                0x40381d10        0x7 %s" % drv_obj,
            "                0x40381d10                esp_psram_impl_get_cs_io",
        ]
    out += [
        "",
        ".flash.text     0x42000020   0xd2a1c8",
        " .text.s_test_psram$constprop$0",
        "                0x420fd640       0x75 %s" % PSRAM_OBJ,
        " *fill*         0x420fd6b5        0x3 ",
    ]
    if init_placed:
        out += [
            " .text.esp_psram_init",
            "                0x%08x      0x176 %s" % (init_addr, PSRAM_OBJ),
            "                0x%08x                esp_psram_init" % init_addr,
        ]
    out += [
        " .text.setup    0x42010000       0x20 .pio/build/picco/src/main.cpp.o",
        "                0x42010000                setup",
        "                0x3fc88000                _data_start = ABSOLUTE (.)",
        "",
        "Cross Reference Table",
        "",
        "Symbol                                            File",
        "esp_psram_impl_enable                             %s" % drv_obj,
        "                                                  %s" % PSRAM_OBJ,
        "esp_psram_init                                    %s" % PSRAM_OBJ,
        "                                                  %s" % caller,
        "esp_psram_io_get_cs_io                            %s" % PSRAM_OBJ,
        "                                                  %s/libspi_flash.a(flash_ops.c.o)" % LIB,
    ]
    return [line + "\n" for line in out]


def problems_for(mod, lines):
    return mod.psram_link_problems(mod.psram_link_facts(lines))


def main():
    mod = load_script()

    # --- Coherent custom-config image (the 2026-11-05 picco build): passes. ---
    facts = mod.psram_link_facts(make_map())
    init = facts["placed"].get("esp_psram_init")
    check(init is not None and init[0] == 0x420FD6B8, "esp_psram_init is found at its memory-map address")
    check(init is not None and init[1] == PSRAM_OBJ, "esp_psram_init is attributed to esp_psram.c.o")
    drv = facts["placed"].get("esp_psram_impl_enable")
    check(drv is not None and drv[1].endswith("esp_psram_impl_octal.c.o)"),
          "the driver is attributed to the octal object")
    check(facts["referrers"] == [HAL_PSRAM], "cref referrers of esp_psram_init are read (and only its own)")
    check(mod.psram_link_problems(facts) == [], "a coherent custom-config image passes")

    # --- Coherent stock-config shape (CONFIG_SPIRAM_BOOT_INIT): cpu_start calls it. ---
    check(problems_for(mod, make_map(caller=CPU_START)) == [], "a cpu_start (BOOT_INIT) caller passes too")
    # The stock package names its objects .c.obj (~/src/boox-picco/hello's map,
    # whose image brought up 8 MB of PSRAM on the device).
    stock = [line.replace(".c.o)", ".c.obj)") for line in make_map(caller=CPU_START)]
    check(problems_for(mod, stock) == [], "stock .c.obj object names pass")

    # --- The trap: esp_psram_init only discarded. Archive + cref still name it. ---
    trap = problems_for(mod, make_map(init_placed=False))
    check(len(trap) == 1 and "esp_psram_init is not in the image" in trap[0], "the trap image is refused")
    check(any("Compile Arduino IDF libs" in p for p in trap), "the refusal says how to rebuild the framework")

    # --- Driver: quad, absent, at address 0. ---
    quad = problems_for(mod, make_map(driver="quad"))
    check(len(quad) == 1 and "not the octal one" in quad[0], "a quad PSRAM driver is refused")
    nodrv = problems_for(mod, make_map(driver=None))
    check(len(nodrv) == 1 and "no PSRAM driver" in nodrv[0], "an image with no PSRAM driver is refused")
    check(len(problems_for(mod, make_map(driver_addr=0))) == 1, "a driver placed at 0x0 is refused")

    # --- esp_psram_init placed at 0x0 is not in the image. ---
    check(len(problems_for(mod, make_map(init_addr=0))) == 1, "esp_psram_init at 0x0 is refused")

    # --- Both gone: a map with no PSRAM at all (a C3 image, or the wrong map). ---
    both = problems_for(mod, make_map(init_placed=False, driver=None))
    check(len(both) == 2, "a map with no PSRAM init and no driver gives both reasons")
    check(len(problems_for(mod, [])) == 2, "an empty map is refused, not passed")

    # --- CRLF maps parse the same. ---
    crlf = [line.replace("\n", "\r\n") for line in make_map()]
    check(problems_for(mod, crlf) == [], "a CRLF map parses the same")

    # --- The map must be from the link that wrote the ELF. ---
    slack = mod.MAP_ELF_SLACK_S
    check(mod.stale_map_problem(None, 1000.0) is not None, "no map at all is refused")
    check(mod.stale_map_problem(1000.0, 1000.0) is None, "a map from the same link passes")
    check(mod.stale_map_problem(1000.0, 1000.0 + slack - 1) is None, "a map written seconds before its ELF passes")
    check(mod.stale_map_problem(1000.0, 1000.0 + slack + 1) is not None,
          "a map older than the ELF by more than the slack is refused")
    check(mod.stale_map_problem(2000.0, 1000.0) is None, "a map newer than the ELF passes")
    check(mod.stale_map_problem(1000.0, None) is None, "a missing ELF is left to the build to report")

    if failures:
        print(f"app_only_upload_test: {len(failures)} FAILURES")
        return 1
    print("app_only_upload_test: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
