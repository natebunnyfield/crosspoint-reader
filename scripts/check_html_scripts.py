#!/usr/bin/env python3
"""Syntax-check every inline <script> AS IT SHIPS, after build_html.py's minifier.

The minifier is the step that can break a page without touching its source:
from 2026-08-03 to 2026-11-18 it folded FilesPage.html's script onto one line,
the first trailing `// comment` swallowed the rest, and the File Manager sat on
its loading spinner forever. Every source file was valid JavaScript the whole
time, so only a check of the MINIFIED output can catch this.

Runs `node --check` on each inline script, before and after minification, and
exits non-zero if any script is valid in the source but not once minified (or
invalid in both). Needs node on PATH; exits 2 (skip) without it.

Usage: python3 scripts/check_html_scripts.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_SCRIPT = os.path.join(REPO, "scripts", "build_html.py")
# build_html.py is a PlatformIO pre-script with its generation loop at module
# level (SCons runs it with no __main__ and no __file__), so load only the part
# above the loop. Fail loudly if the marker moves rather than check nothing.
LOOP_MARKER = "for root, _, files in os.walk(SRC_DIR):"
INLINE_SCRIPT = re.compile(r"<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)</script>", re.IGNORECASE)


def load_minifier():
    with open(BUILD_SCRIPT, encoding="utf-8") as f:
        source = f.read()
    if LOOP_MARKER not in source:
        sys.exit(f"check_html_scripts: cannot find the generation loop in {BUILD_SCRIPT}; update LOOP_MARKER")
    ns = {}
    exec(compile(source.split(LOOP_MARKER, 1)[0], BUILD_SCRIPT, "exec"), ns)
    return ns["minify_html"]


def node_check(js, workdir, name):
    path = os.path.join(workdir, name)
    with open(path, "w", encoding="utf-8") as f:
        f.write(js)
    r = subprocess.run(["node", "--check", path], capture_output=True, text=True)
    if r.returncode == 0:
        return True, []
    # node prints the source line, a caret, then "SyntaxError: ..." and its
    # own version banner last; the error line is the useful one.
    errors = [l for l in r.stderr.splitlines() if re.match(r"\w*Error:", l)]
    # On a script folded onto one line node echoes ~64 KB of it and stops
    # before the error line, which is itself the diagnosis.
    return False, errors[:1] or ["node printed no error line (is the script on one line?)"]


def main():
    if shutil.which("node") is None:
        print("check_html_scripts: SKIP, node not on PATH")
        return 2
    minify_html = load_minifier()
    pages = []
    for root, _, files in os.walk(os.path.join(REPO, "src")):
        pages += [os.path.join(root, f) for f in files if f.endswith(".html")]
    failures = 0
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        for page in sorted(pages):
            rel = os.path.relpath(page, REPO)
            with open(page, encoding="utf-8") as f:
                html = f.read()
            raw = INLINE_SCRIPT.findall(html)
            shipped = INLINE_SCRIPT.findall(minify_html(html))
            if len(raw) != len(shipped):
                print(f"FAIL {rel}: {len(raw)} inline scripts in source, {len(shipped)} after minification")
                failures += 1
                continue
            for i, (src_js, min_js) in enumerate(zip(raw, shipped)):
                checked += 1
                src_ok, src_err = node_check(src_js, tmp, f"src{i}.js")
                min_ok, min_err = node_check(min_js, tmp, f"min{i}.js")
                if src_ok and min_ok:
                    continue
                failures += 1
                if not src_ok:
                    print(f"FAIL {rel} script {i}: invalid in the SOURCE: {' '.join(src_err)}")
                else:
                    print(f"FAIL {rel} script {i}: valid in source, BROKEN BY MINIFICATION: {' '.join(min_err)}")
    print(f"check_html_scripts: {checked} inline scripts in {len(pages)} pages, {failures} failing")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
