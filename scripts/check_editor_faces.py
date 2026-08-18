"""Refuse to build a release that silently drops the commercial editor faces.

B-029: PragmataPro and NittiTypewriter are commercial, so their glyph tables are
gitignored and regenerated locally by convert-builtin-fonts.sh. main.cpp gates
each family behind __has_include, which is what lets a clone with no licensed
TTFs compile at all -- and that is the right behavior for a clone.

It is the wrong behavior for a RELEASE. A gh_release built in a fresh worktree
came out 430,674 bytes smaller than the same commit built in the working
checkout, because the headers were not there and nothing said so. The only
visible sign was a flash figure of 68.8% where 75.4% belonged. A published
firmware would have quietly lost two editor typefaces.

So: release environments FAIL here, every other environment warns loudly and
carries on. That keeps the no-TTF clone building while making the silent case
impossible to publish by accident.

Set CROSSPOINT_ALLOW_MISSING_EDITOR_FACES=1 to downgrade the failure to a
warning -- for deliberately shipping a build without the licensed faces. It
prints what it is allowing.
"""

import os
import sys

Import("env")  # noqa: F821  (SCons injects this)

FAMILIES = ("pragmatapro", "nittitypewriter")
SIZES = ("12", "14")
STYLES = ("regular", "bold", "italic", "bolditalic")

# Environments whose output is published. A missing face here is a shipped
# defect, not a local inconvenience.
RELEASE_ENVS = ("gh_release", "gh_release_rc")

FIX = "  lib/EpdFont/scripts/convert-builtin-fonts.sh   (needs lib/EpdFont/local_fonts/)"


def headers(suffix=""):
    return [
        "%s_%s_%s%s.h" % (family, size, style, suffix)
        for family in FAMILIES
        for size in SIZES
        for style in STYLES
    ]


def missing_from(directory, names):
    return [n for n in names if not os.path.isfile(os.path.join(directory, n))]


def main():
    pioenv = env["PIOENV"]  # noqa: F821
    fontdir = os.path.join(env.subst("$PROJECT_DIR"), "lib", "EpdFont", "builtinFonts")  # noqa: F821

    base = headers()
    gone = missing_from(fontdir, base)
    if not gone:
        return

    have = len(base) - len(gone)
    allowed = os.environ.get("CROSSPOINT_ALLOW_MISSING_EDITOR_FACES") == "1"
    # A tree with SOME of them is the worse case: main.cpp gates on the largest
    # size, so a partial set either fails to compile or -- if the gate header is
    # the missing one -- drops faces whose siblings are sitting right there.
    partial = have > 0

    print("")
    print("*" * 78)
    print("  COMMERCIAL EDITOR FACES MISSING -- %d of %d headers absent" % (len(gone), len(base)))
    print("  env: %s" % pioenv)
    print("  dir: %s" % fontdir)
    print("")
    for name in gone:
        print("    missing  %s" % name)
    print("")
    if partial:
        print("  This tree has %d of them, so it is a PARTIAL set -- most likely" % have)
        print("  regenerated before a size was added. main.cpp gates on the largest")
        print("  size, so this drops or breaks a face whose siblings are present.")
        print("")
    print("  Regenerate with:")
    print(FIX)
    print("")

    if pioenv in RELEASE_ENVS and not allowed:
        print("  REFUSING TO BUILD. This is a release environment, and a release")
        print("  built without these faces loses PragmataPro and NittiTypewriter")
        print("  from the device with no error and a smaller binary (B-029).")
        print("  Override with CROSSPOINT_ALLOW_MISSING_EDITOR_FACES=1 if that is")
        print("  genuinely what you want.")
        print("*" * 78)
        print("")
        sys.exit(1)

    if pioenv in RELEASE_ENVS and allowed:
        print("  ALLOWED by CROSSPOINT_ALLOW_MISSING_EDITOR_FACES=1 -- this release")
        print("  WILL ship without those faces.")
    else:
        print("  Building anyway: the faces degrade to a built-in mono, which is")
        print("  the intended behavior for a tree without the licensed TTFs.")
    print("*" * 78)
    print("")


main()
