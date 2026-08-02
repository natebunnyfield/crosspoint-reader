"""
PlatformIO pre-build script: emit the simulator's supersampling factor as
EXACTLY ONE -DCROSSPOINT_RENDER_SCALE on the compiler command line.

Why this is not just a build_flags entry
----------------------------------------
`[env:simulator]` used to carry `-DCROSSPOINT_RENDER_SCALE=2` directly. PlatformIO
*appends* `PLATFORMIO_BUILD_FLAGS`, so the documented 1x override put a second
definition after it and every translation unit warned:

    <command line>: warning: 'CROSSPOINT_RENDER_SCALE' macro redefined
                              [-Wmacro-redefined]

The override still won (last definition on the command line wins), but the spam
buried real warnings. Prepending `-UCROSSPOINT_RENDER_SCALE` does NOT help:
SCons.Environment.ParseFlags drops unrecognised flags into CCFLAGS, and
`CCCOM = '$CC ... $CCFLAGS $_CCCOMCOM $SOURCES'` (SCons/Tool/cc.py) puts CCFLAGS
*before* `$_CPPDEFFLAGS`, so the undef is consumed before either -D is seen.

Emitting the define from here instead means the value is chosen once, before any
flag is turned into a command line, so there is never a second definition to
redefine.

Usage
-----
    pio run -e simulator_x3                                  # 1x, device-exact (default)
    CROSSPOINT_RENDER_SCALE=2 pio run -e simulator_x3        # 2x supersampled
    CROSSPOINT_RENDER_SCALE=2 pio run -e simulator_x3 -t run_simulator

The env var is read at build time only; it does not need to be set when the
resulting binary is run. Toggling it does not change platformio.ini, so the
build directories survive and SCons recompiles incrementally.

An explicit -DCROSSPOINT_RENDER_SCALE passed through build_flags /
PLATFORMIO_BUILD_FLAGS still wins and is left alone, so the older incantation
keeps working (and stays warning-free, because this script then adds nothing).

Only the simulator envs use this. On device, lib/hal/HalDisplay.h's #ifndef
default of 1 stands; the iOS app sets the macro in ios/CMakeLists.txt.
"""

import os
import sys

Import("env")  # noqa: F821 -- injected by SCons

MACRO = "CROSSPOINT_RENDER_SCALE"

# 1x mirrors the device: the simulator exists to show what the e-ink panel will
# actually show, so a plain `pio run` must not flatter it. The host panel does
# have the density to rasterise glyphs at 2x, which is useful for judging shape,
# but that is a deliberate opt-in and never the default -- matching
# HalDisplay.h's own #ifndef default.
DEFAULT_SCALE = 1


def find_explicit_flag(build_flags):
    """Return an existing -D<MACRO>[=value] from build_flags, or None."""
    for flag in build_flags or []:
        text = str(flag)
        if text == "-D" + MACRO or text.startswith("-D" + MACRO + "="):
            return text
    return None


def resolve_scale(raw):
    """Validate the env var. Returns an int; exits the build on garbage."""
    if not raw.isdigit() or int(raw) < 1:
        print(
            f"ERROR [sim_render_scale.py]: {MACRO}={raw!r} is not a positive "
            "integer. Use 1 (device-exact) or 2 (host pixel density).",
            file=sys.stderr,
        )
        env.Exit(1)  # noqa: F821
    return int(raw)


explicit = find_explicit_flag(env.get("BUILD_FLAGS"))  # noqa: F821
override = os.environ.get(MACRO, "").strip()

if explicit:
    # Someone passed the define by hand. Adding our own would recreate exactly
    # the -Wmacro-redefined spam this script exists to remove.
    if override:
        print(
            f"WARNING [sim_render_scale.py]: both {explicit} and {MACRO}="
            f"{override} were given; the build flag wins.",
            file=sys.stderr,
        )
    print(f"[sim_render_scale.py] {explicit} supplied via build flags; leaving it alone")
elif override:
    scale = resolve_scale(override)
    env.Append(CPPDEFINES=[(MACRO, scale)])  # noqa: F821
    print(f"[sim_render_scale.py] {MACRO}={scale} (from environment)")
else:
    env.Append(CPPDEFINES=[(MACRO, DEFAULT_SCALE)])  # noqa: F821
    print(f"[sim_render_scale.py] {MACRO}={DEFAULT_SCALE} (default)")
