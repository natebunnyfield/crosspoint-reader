"""
PlatformIO pre-build script: emit the simulator's supersampling CEILING as
EXACTLY ONE -DCROSSPOINT_RENDER_SCALE on the compiler command line, plus the
-DCROSSPOINT_RENDER_SCALE_RUNTIME switch that lets the factor actually rendered
at be chosen when the binary runs.

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

CROSSPOINT_RENDER_SCALE is read at BUILD time and fixes the ceiling. Above a
ceiling of 1 this script also defines CROSSPOINT_RENDER_SCALE_RUNTIME, which
makes cp::renderScale() (lib/GfxRenderer/RenderScale.h) a variable rather than a
constant -- so one binary can render at any factor up to its ceiling, chosen at
launch by the RUN-time env var CROSSPOINT_SIM_RENDER_SCALE (the iOS app reads
Settings > Page Sharpness instead). Unset, it renders at the ceiling, which is
byte-for-byte the behaviour from before the switch existed.

    CROSSPOINT_RENDER_SCALE=3 pio run -e simulator      # ceiling 3, renders 3x
    CROSSPOINT_SIM_RENDER_SCALE=2 .pio/build/simulator/program   # same binary, 2x

At a ceiling of 1 the switch is deliberately NOT defined: there is nothing to
choose, and leaving cp::renderScale() constexpr keeps the device-exact desktop
build folding the scale arithmetic away exactly as the device build does. Toggling it does not change platformio.ini, so the
build directory survives -- and that used to be a trap rather than a feature.
The scale changes a STRUCT LAYOUT, not just code paths: FontCacheManager takes
an extra constructor parameter under RENDER_SCALE > 1. Recompiling incrementally
across a scale change therefore mixed objects built at different scales, and the
lucky outcome was the undefined-symbol link error that caught it on 2026-08-15.
The unlucky one is a binary that links and misbehaves. The marker below makes a
scale change wipe the build directory, so a build is all one scale or it is not
a build.

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
    _ceiling = 0  # unknown -- parsed below only to decide the runtime switch
    if "=" in explicit:
        try:
            _ceiling = int(explicit.split("=", 1)[1])
        except ValueError:
            _ceiling = 0
elif override:
    scale = resolve_scale(override)
    env.Append(CPPDEFINES=[(MACRO, scale)])  # noqa: F821
    print(f"[sim_render_scale.py] {MACRO}={scale} (from environment)")
    _ceiling = scale
else:
    env.Append(CPPDEFINES=[(MACRO, DEFAULT_SCALE)])  # noqa: F821
    print(f"[sim_render_scale.py] {MACRO}={DEFAULT_SCALE} (default)")
    _ceiling = DEFAULT_SCALE

# The runtime switch, above a ceiling of 1. See the module docstring.
#
# CROSSPOINT_RENDER_SCALE_RUNTIME=0 in the environment forces it off, which
# builds a binary whose scale is a compile-time constant again -- the shape this
# repo had before the setting existed. That is not a nostalgia switch: it is how
# you produce a reference binary to diff a runtime build against, and proving
# "runtime at N is byte-identical to fixed at N" is the only way to know the
# ~40 arithmetic sites were converted without one of them silently reading the
# ceiling.
_runtime_env = os.environ.get(MACRO + "_RUNTIME", "").strip()
_runtime_on = False
if _runtime_env == "0":
    print(f"[sim_render_scale.py] {MACRO}_RUNTIME disabled by environment; "
          f"scale is a compile-time constant {_ceiling}")
elif _ceiling > 1:
    env.Append(CPPDEFINES=[(MACRO + "_RUNTIME", 1)])  # noqa: F821
    _runtime_on = True
    print(
        f"[sim_render_scale.py] {MACRO}_RUNTIME=1 "
        f"(CROSSPOINT_SIM_RENDER_SCALE picks 1..{_ceiling} at launch)"
    )



# --- Scale changes invalidate the whole build directory ----------------------
#
# PlatformIO's project checksum covers platformio.ini and the source tree, not
# the environment, so nothing here would otherwise force a rebuild when the
# scale changes. Stamp the value and clear the directory when it moves.
#
# Deliberately a full wipe rather than a targeted one: the affected set is
# "every TU that sees a scale-dependent declaration", which is most of them via
# GfxRenderer.h and FontCacheManager.h, and getting that list subtly wrong
# reintroduces exactly the mixed-object bug this prevents.
# The RUNTIME switch is part of the stamp, not just the scale. It changes
# whether cp::renderScale() is a constexpr function or an extern variable --
# which is an ODR-relevant difference in every header that calls it, and it
# changes DirectPixelWriter's layout (a member at runtime, nothing at
# compile time). Stamping only the scale would let a RUNTIME=0 reference build
# link against runtime objects, which is the mixed-object bug this check was
# written for, wearing a different hat.
_build_dir = env.subst("$BUILD_DIR")  # noqa: F821
_effective = explicit if explicit else (override or str(DEFAULT_SCALE))
_effective = f"{_effective}/runtime={1 if _runtime_on else 0}"
_marker = os.path.join(_build_dir, ".render_scale")

if os.path.isdir(_build_dir):
    _previous = None
    try:
        with open(_marker) as fh:
            _previous = fh.read().strip()
    except OSError:
        # No marker: a directory from before this check existed. Treat it as
        # unknown rather than as a match -- it may hold objects from any scale.
        _previous = None
    if _previous != str(_effective):
        import shutil

        print(
            f"[sim_render_scale.py] scale changed ({_previous or 'unknown'} -> "
            f"{_effective}); clearing {_build_dir} so no objects survive from "
            "the previous scale",
            file=sys.stderr,
        )
        shutil.rmtree(_build_dir, ignore_errors=True)

os.makedirs(_build_dir, exist_ok=True)
with open(_marker, "w") as fh:
    fh.write(str(_effective))
