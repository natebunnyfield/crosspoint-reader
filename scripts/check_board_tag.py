"""Fail a device build whose firmware.bin does not carry its board tag exactly once.

Owner ruling 2026-11-06, "Add all three", gate 2. Every device image carries
"CROSSPOINT-BOARD-V1:<board>;" once in .rodata (src/network/FirmwareBoardTag.cpp),
and the on-device update paths refuse an image whose tag names another board
(board_tag::Scanner flags any tag that differs). A build that dropped the tag
(gc-sections, a refactor that stops referencing TAG), carried it twice, or
carried another board's tag would still build SUCCESS with a plausible size.

WHERE THE EXPECTED TAG COMES FROM. Not from the env name: the tag is chosen by
the #if/#elif chain in FirmwareBoardTag.cpp from the FREEINK_DEVICE_* build
flags (the X3/X4 C3 envs -> "x4", sticky -> "sticky", picco and
picco-gh_release -> "picco"). This script reads that chain from the source and
evaluates it against this env's CPPDEFINES, first match wins, as the
preprocessor does, so the two cannot drift. A chain it cannot read in full
fails the build rather than being guessed at.

Post-build, on $BUILD_DIR/${PROGNAME}.bin, after stamp_app_desc.py (which
touches only the app descriptor). Listed in [base] extra_scripts only, so the
simulator envs, which do not extend [base] and produce no firmware.bin, never
run it. On failure it deletes firmware.bin, so the next build relinks and is
checked again instead of finding a stale bin up to date.

The helpers import outside SCons (test/check_board_tag/).
"""
import os
import re
import sys

MAGIC = b"CROSSPOINT-BOARD-V1:"
TAG_SOURCE = os.path.join("src", "network", "FirmwareBoardTag.cpp")

_COND = re.compile(r"^#\s*(if|elif)\s+(.+?)\s*$")
_FLAG = re.compile(r"^FREEINK_DEVICE_[A-Z0-9_]+$")
_DEFINE = re.compile(r'^#\s*define\s+CROSSPOINT_BOARD_NAME\s+"([a-z0-9_]+)"\s*$')


def board_chain(source_text):
    """[(flags, name), ...] in source order, from the FREEINK_DEVICE_* chain.

    Accepts only `#if A || B` / `#elif C` branches of FREEINK_DEVICE_* names,
    each followed by one `#define CROSSPOINT_BOARD_NAME "name"`, closed by
    `#else` or `#endif`. Raises ValueError on anything else inside the chain.
    """
    chain = []
    flags = None
    inside = False
    for raw in source_text.splitlines():
        line = raw.strip()
        cond = _COND.match(line)
        if not inside:
            if cond and cond.group(1) == "if" and "FREEINK_DEVICE_" in cond.group(2):
                inside = True
            else:
                continue
        if cond:
            if flags is not None:
                raise ValueError("branch with no CROSSPOINT_BOARD_NAME before: %s" % line)
            terms = [t.strip() for t in cond.group(2).split("||")]
            if not all(_FLAG.match(t) for t in terms):
                raise ValueError("unreadable board condition: %s" % line)
            flags = tuple(terms)
            continue
        define = _DEFINE.match(line)
        if define:
            if flags is None:
                raise ValueError("CROSSPOINT_BOARD_NAME outside a branch: %s" % line)
            chain.append((flags, define.group(1)))
            flags = None
            continue
        if re.match(r"^#\s*(else|endif)\b", line):
            if flags is not None:
                raise ValueError("branch with no CROSSPOINT_BOARD_NAME before: %s" % line)
            break
        if line and not line.startswith("//"):
            raise ValueError("unexpected line inside the board chain: %s" % line)
    if not chain:
        raise ValueError("no FREEINK_DEVICE_* board chain found")
    return chain


def true_flags(cppdefines):
    """Names defined to a nonzero value. #if on an absent or 0 macro is false."""
    out = set()
    for item in cppdefines or []:
        if isinstance(item, (list, tuple)):
            name = item[0]
            value = item[1] if len(item) > 1 else 1
        else:
            name, value = item, 1
        text = str(value).strip()
        if text not in ("0", ""):
            out.add(str(name))
    return out


def expected_tag(chain, flags):
    """The board name the chain picks for these flags, or None (the C++ #error)."""
    for names, board in chain:
        if any(n in flags for n in names):
            return board
    return None


def tag_problems(image, board):
    """Empty when the image holds the magic exactly once, as <board>;."""
    want = MAGIC + board.encode() + b";"
    found = []
    start = 0
    while True:
        at = image.find(MAGIC, start)
        if at < 0:
            break
        end = image.find(b";", at + len(MAGIC), at + len(MAGIC) + 33)
        found.append(bytes(image[at:end + 1]) if end >= 0 else bytes(image[at:at + len(MAGIC) + 16]) + b"...")
        start = at + 1
    if found == [want]:
        return []
    if not found:
        return ["no %r in the image" % want.decode()]
    exact = found.count(want)
    others = [f for f in found if f != want]
    problems = []
    if exact != 1:
        problems.append("%r occurs %d times, not once" % (want.decode(), exact))
    if others:
        problems.append("other board tags present: %s" % ", ".join(repr(o.decode(errors="replace")) for o in others))
    return problems


def _scons_main(env):
    tag_source = os.path.join(env.subst("$PROJECT_DIR"), TAG_SOURCE)
    pioenv = env.subst("$PIOENV")

    def _check(source, target, env):
        path = str(target[0])
        if not path.endswith(".bin"):
            return 0
        try:
            with open(tag_source) as fp:
                chain = board_chain(fp.read())
        except (OSError, ValueError) as err:
            problems, board = ["cannot read the board chain in %s: %s" % (tag_source, err)], None
        else:
            board = expected_tag(chain, true_flags(env.get("CPPDEFINES", [])))
            if board is None:
                problems = ["no FREEINK_DEVICE_* flag of this env matches the chain in %s" % tag_source]
            else:
                with open(path, "rb") as fh:
                    problems = tag_problems(fh.read(), board)
        if problems:
            sys.stderr.write(
                "ERROR: scripts/check_board_tag.py: %s (env %s): %s. Deleted it so the next build relinks.\n"
                % (path, pioenv, "; ".join(problems))
            )
            try:
                os.remove(path)
            except OSError:
                pass
            return 1
        print("check_board_tag: %s carries CROSSPOINT-BOARD-V1:%s; exactly once" % (path, board))
        return 0

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _check)


try:
    Import("env")  # noqa: F821  (injected by SCons)
except NameError:  # imported outside SCons (host check of the helpers above)
    env = None

if env is not None:
    _scons_main(env)
