#!/usr/bin/env python3
"""scripts/check_board_tag.py -- the post-build board-tag gate.

Every case is one way the gate could be wrong in either direction:

  * passing an image with no tag, two tags, or another board's tag -> a bin
    the on-device update paths refuse (or a cross-board flash) ships;
  * picking the wrong expected tag for an env's flags (chain order, a flag
    defined to 0, the X3+X4 pair) -> every build of that env fails, or the
    gate checks for the wrong board;
  * guessing at a chain it cannot read instead of failing.

The chain cases read the REAL src/network/FirmwareBoardTag.cpp, so a new board
or a reordered chain is tested as the build will see it.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
SCRIPT = os.path.join(ROOT, "scripts", "check_board_tag.py")

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print(f"  FAIL: {what}")


def load_script():
    spec = importlib.util.spec_from_file_location("check_board_tag", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)  # outside SCons: Import() raises NameError, env stays None
    return mod


def image(*tags, pad=4096):
    """Bytes shaped like .rodata: filler, then each tag NUL-terminated."""
    out = bytearray(b"\xe9" + b"\x00" * pad)
    for t in tags:
        out += t + b"\x00" + b"\x55" * 64
    return bytes(out)


def main():
    mod = load_script()
    with open(os.path.join(ROOT, mod.TAG_SOURCE)) as fp:
        chain = mod.board_chain(fp.read())

    def tag_for(defines):
        return mod.expected_tag(chain, mod.true_flags(defines))

    # --- Expected tag per env, from the real chain and real flag shapes. ---
    c3 = [("FREEINK_DEVICE_X4", "1"), ("FREEINK_DEVICE_X3", "1"), ("CROSSPOINT_VERSION", '\\"1.0\\"')]
    check(tag_for(c3) == "x4", "the C3 envs (X4=1, X3=1) expect x4")
    check(tag_for([("FREEINK_DEVICE_X3", "1")]) == "x4", "X3 alone expects x4")
    check(tag_for([("FREEINK_DEVICE_STICKY", "1")]) == "sticky", "sticky expects sticky")
    check(tag_for([("FREEINK_DEVICE_PICCO", "1"), "BOARD_HAS_PSRAM"]) == "picco", "picco expects picco")
    check(tag_for(["FREEINK_DEVICE_PICCO"]) == "picco", "a bare -DFREEINK_DEVICE_PICCO counts as 1")
    check(tag_for([("FREEINK_DEVICE_PICCO", 1)]) == "picco", "an int value counts")
    check(tag_for([("FREEINK_DEVICE_X4PRO", "1"), ("FREEINK_DEVICE_X4", "1")]) == "x4pro",
          "first match wins: X4PRO before X4, as the preprocessor does")
    check(tag_for([("FREEINK_DEVICE_PICCO", "0"), ("FREEINK_DEVICE_STICKY", "1")]) == "sticky",
          "a flag defined to 0 is false")
    check(tag_for([("FREEINK_DEVICE_PICCO", "0")]) is None, "no true flag: no tag (the C++ #error case)")
    check(tag_for([]) is None, "no flags at all: no tag")

    # --- A chain it cannot read fails rather than guesses. ---
    def raises(text):
        try:
            mod.board_chain(text)
        except ValueError:
            return True
        return False

    good = '#if FREEINK_DEVICE_A\n#define CROSSPOINT_BOARD_NAME "a"\n#else\n#error x\n#endif\n'
    check(mod.board_chain(good) == [(("FREEINK_DEVICE_A",), "a")], "a minimal chain parses")
    check(raises('#if FREEINK_DEVICE_A && FREEINK_DEVICE_B\n#define CROSSPOINT_BOARD_NAME "a"\n#endif\n'),
          "an && condition is refused")
    check(raises('#if FREEINK_DEVICE_A\n#elif FREEINK_DEVICE_B\n#define CROSSPOINT_BOARD_NAME "b"\n#endif\n'),
          "a branch with no name is refused")
    check(raises('#if FREEINK_DEVICE_A\n#define CROSSPOINT_BOARD_NAME BOARD\n#endif\n'),
          "a name that is not a literal is refused")
    check(raises("int x;\n"), "no chain at all is refused")

    # --- The image check. ---
    x4 = b"CROSSPOINT-BOARD-V1:x4;"
    picco = b"CROSSPOINT-BOARD-V1:picco;"
    check(mod.tag_problems(image(x4), "x4") == [], "exactly one x4 tag passes")
    check(mod.tag_problems(image(picco), "picco") == [], "exactly one picco tag passes")
    check(mod.tag_problems(image(), "x4") != [], "a tagless image fails")
    check(mod.tag_problems(image(x4, x4), "x4") != [], "the tag twice fails")
    check(mod.tag_problems(image(picco), "x4") != [], "another board's tag fails")
    check(mod.tag_problems(image(x4, picco), "x4") != [], "the right tag plus a foreign one fails")
    check(mod.tag_problems(image(b"CROSSPOINT-BOARD-V1:x4"), "x4") != [], "an unterminated tag fails")
    check(mod.tag_problems(image(b"CROSSPOINT-BOARD-V1:x4pro;"), "x4") != [], "a prefix-sharing tag fails")
    check(mod.tag_problems(image(x4)[:-1] + b"CROSSPOINT-BOARD-V1:", "x4") != [], "a magic cut off at EOF fails")

    if failures:
        print(f"check_board_tag_test: {len(failures)} FAILURES")
        return 1
    print("check_board_tag_test: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
