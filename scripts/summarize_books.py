#!/usr/bin/env python3
"""Generate summary EPUBs for books tagged on-device with Manage Files > Summarize.

The device-side handshake (docs/manage-files.md, "Summarize tag"): toggling
Summarize writes /.crosspoint/epub_<hash>/summarize.tag whose content is the
book's full card path. This script scans those markers, has the claude CLI
produce a researched summary at three reading lengths (3 / 10 / 30 minutes at
~230 wpm), packages them as one EPUB ("<Title> - Summary.epub", chapters per
length plus a colophon), delivers it to /books/, and deletes the marker.

Modes:
  --source DIR    Local card tree (default: fs_). Reads markers and books from
                  DIR, writes the summary EPUB into DIR/books/, deletes the
                  marker locally. Use against the curated card-source tree so
                  the destructive sync-card.sh mirror keeps the summaries.
  --webdav URL    Device in File Transfer mode (e.g. http://crosspoint.local).
                  Scans /.crosspoint over WebDAV, GETs the book, PUTs the
                  summary to /books/, DELETEs the marker. The device server is
                  single-threaded: everything is sequential on purpose.

Requires the `claude` CLI on PATH (uses your Claude Code subscription; the
research pass uses its WebSearch tool). Stdlib only otherwise.
"""

import argparse
import datetime
import html
import io
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from html.parser import HTMLParser
from pathlib import Path

WPM = 230
LENGTHS = [  # (chapter file stem, chapter title, minutes, word budget)
    ("summary_3min", "3-Minute Summary", 3, 3 * WPM),
    ("summary_10min", "10-Minute Summary", 10, 10 * WPM),
    ("summary_30min", "30-Minute Summary", 30, 30 * WPM),
]
WORD_TOLERANCE = (0.55, 1.6)  # accepted fraction of the budget
CLAUDE_TIMEOUT_S = 45 * 60

DC_NS = "{http://purl.org/dc/elements/1.1/}"
OPF_NS = "{http://www.idpf.org/2007/opf}"
CONTAINER_NS = "{urn:oasis:names:tc:opendocument:xmlns:container}"
DAV_NS = "{DAV:}"


# ---------------------------------------------------------------------------
# Card access (local tree or device WebDAV)


class LocalCard:
    def __init__(self, root: Path):
        self.root = root

    def _fs(self, card_path: str) -> Path:
        return self.root / card_path.lstrip("/")

    def find_markers(self):
        """Yield (marker card path, book card path)."""
        for tag in sorted((self.root / ".crosspoint").glob("epub_*/summarize.tag")):
            book = tag.read_text(encoding="utf-8", errors="replace").strip()
            yield "/" + str(tag.relative_to(self.root)), book

    def exists(self, card_path: str) -> bool:
        return self._fs(card_path).exists()

    def read(self, card_path: str) -> bytes:
        return self._fs(card_path).read_bytes()

    def write(self, card_path: str, data: bytes):
        dest = self._fs(card_path)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)

    def delete(self, card_path: str):
        p = self._fs(card_path)
        if p.exists():
            p.unlink()


class WebdavCard:
    """Minimal WebDAV client for the device's single-threaded server."""

    def __init__(self, base_url: str):
        self.base = base_url.rstrip("/")

    def _url(self, card_path: str) -> str:
        return self.base + urllib.parse.quote(card_path)

    def _request(self, method: str, card_path: str, data=None, headers=None):
        req = urllib.request.Request(self._url(card_path), data=data, method=method)
        for k, v in (headers or {}).items():
            req.add_header(k, v)
        return urllib.request.urlopen(req, timeout=60)

    def _propfind_names(self, card_path: str):
        body = (
            b'<?xml version="1.0" encoding="utf-8"?>'
            b'<propfind xmlns="DAV:"><prop><resourcetype/></prop></propfind>'
        )
        with self._request(
            "PROPFIND", card_path, data=body, headers={"Depth": "1", "Content-Type": "application/xml"}
        ) as resp:
            tree = ET.fromstring(resp.read())
        names = []
        for response in tree.iter(DAV_NS + "response"):
            href = response.findtext(DAV_NS + "href", "")
            name = posixpath.basename(urllib.parse.unquote(href).rstrip("/"))
            if name:
                names.append(name)
        return names

    def find_markers(self):
        for entry in self._propfind_names("/.crosspoint"):
            if not entry.startswith("epub_"):
                continue
            marker = f"/.crosspoint/{entry}/summarize.tag"
            try:
                book = self.read(marker).decode("utf-8", errors="replace").strip()
            except urllib.error.HTTPError as e:
                if e.code == 404:
                    continue
                raise
            yield marker, book

    def exists(self, card_path: str) -> bool:
        try:
            with self._request("HEAD", card_path):
                return True
        except urllib.error.HTTPError:
            return False

    def read(self, card_path: str) -> bytes:
        with self._request("GET", card_path) as resp:
            return resp.read()

    def write(self, card_path: str, data: bytes):
        with self._request("PUT", card_path, data=data, headers={"Content-Type": "application/octet-stream"}):
            pass

    def delete(self, card_path: str):
        try:
            with self._request("DELETE", card_path):
                pass
        except urllib.error.HTTPError as e:
            if e.code != 404:
                raise


# ---------------------------------------------------------------------------
# EPUB text extraction (stdlib only)


class _TextExtractor(HTMLParser):
    BLOCK_TAGS = {"p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6", "li", "tr", "blockquote", "section"}
    SKIP_TAGS = {"script", "style", "head"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
        self._skip_depth = 0

    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP_TAGS:
            self._skip_depth += 1
        elif tag in self.BLOCK_TAGS:
            self.parts.append("\n")

    def handle_endtag(self, tag):
        if tag in self.SKIP_TAGS and self._skip_depth:
            self._skip_depth -= 1
        elif tag in self.BLOCK_TAGS:
            self.parts.append("\n")

    def handle_data(self, data):
        if not self._skip_depth:
            self.parts.append(data)

    def text(self) -> str:
        raw = "".join(self.parts)
        raw = re.sub(r"[ \t]+", " ", raw)
        return re.sub(r"\n{3,}", "\n\n", raw).strip()


def extract_epub(data: bytes):
    """Return (title, author, language, full plain text)."""
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        container = ET.fromstring(zf.read("META-INF/container.xml"))
        rootfile = container.find(f".//{CONTAINER_NS}rootfile")
        opf_path = rootfile.get("full-path")
        opf_dir = posixpath.dirname(opf_path)
        opf = ET.fromstring(zf.read(opf_path))

        title = opf.findtext(f".//{DC_NS}title", "Unknown Title").strip()
        author = opf.findtext(f".//{DC_NS}creator", "").strip()
        language = opf.findtext(f".//{DC_NS}language", "en").strip() or "en"

        hrefs = {
            item.get("id"): item.get("href")
            for item in opf.iter(OPF_NS + "item")
            if item.get("media-type") == "application/xhtml+xml"
        }
        chunks = []
        for itemref in opf.iter(OPF_NS + "itemref"):
            href = hrefs.get(itemref.get("idref"))
            if not href:
                continue
            path = posixpath.normpath(posixpath.join(opf_dir, urllib.parse.unquote(href)))
            try:
                doc = zf.read(path).decode("utf-8", errors="replace")
            except KeyError:
                continue
            parser = _TextExtractor()
            parser.feed(doc)
            text = parser.text()
            if text:
                chunks.append(text)
        return title, author, language, "\n\n* * *\n\n".join(chunks)


# ---------------------------------------------------------------------------
# claude CLI invocation


PROMPT_TEMPLATE = """You are producing a researched book summary for an e-reader.

The full text of the book is in book.txt in the current directory (metadata in
metadata.json). Work in this order:

1. Read book.txt completely (in chunks if large). This is the primary source.
2. Research the book on the web: the author's background, the book's critical
   reception, its themes, and (for fiction) its historical/literary context or
   (for non-fiction) how its claims have held up. Weave what you learn into the
   summaries where it genuinely adds understanding; keep the book itself
   central.
3. Write FOUR files into the current directory. Each must be an XHTML FRAGMENT
   (no <html>/<head>/<body> wrapper) using only these tags: h2, h3, p, em,
   strong, blockquote. All entities XML-escaped (&amp;, &lt;) — named HTML
   entities like &mdash; or &nbsp; are NOT valid here, use the literal
   character. Each summary is fully self-contained — a reader picks exactly
   one, so do not reference the other lengths and do not assume anything was
   already read. Do NOT open a file with a heading: the chapter title is added
   automatically, and a heading forces a page break on this device, so a
   leading one wastes a whole page. Start with a paragraph; use h2/h3 only for
   internal sections after the first paragraph.

   - summary_3min.xhtml  — about {w3} words. The essence: what the book is,
     what happens / what it argues, why it matters.
   - summary_10min.xhtml — about {w10} words. Structured overview: main arc or
     argument chapter-group by chapter-group, key characters/concepts, 2-3
     short quotations, a paragraph of context from your research.
   - summary_30min.xhtml — about {w30} words. A thorough condensation someone
     could read instead of the book: full arc/argument with all significant
     developments, quotations where the prose matters, plus woven-in research
     (reception, author context, influence).
   - colophon.xhtml — under 150 words: one paragraph noting this is an
     AI-generated summary of "{title}"{author_clause}, generated {date}, based
     on the full text plus web research, and listing the 2-4 most useful
     sources you consulted (title + site, plain text, no links).

Word counts matter: stay within ~15% of each target. Do not stop until all
four files are written."""


def child_env() -> dict:
    """Environment for the child claude CLI.

    Built from a whitelist, not by scrubbing: when this script runs inside a
    Claude Code session the inherited session vars (base URL, host
    auth-refresh flags, OAuth endpoint overrides) make the child CLI fail with
    "OAuth session expired and could not be refreshed".
    """
    # `USER` is deliberately NOT forwarded: with it set (and only it — bisected
    # against every other var here) the CLI fails auth. LOGNAME carries the
    # same value for anything that needs the username.
    keep = ("HOME", "PATH", "LOGNAME", "SHELL", "LANG", "LC_ALL", "TMPDIR", "SSH_AUTH_SOCK")
    env = {k: os.environ[k] for k in keep if k in os.environ}
    env.setdefault("TERM", "dumb")
    return env


def run_claude(claude_bin: str, model: str, workdir: Path, title: str, author: str, date: str) -> bool:
    prompt = PROMPT_TEMPLATE.format(
        w3=LENGTHS[0][3],
        w10=LENGTHS[1][3],
        w30=LENGTHS[2][3],
        title=title,
        author_clause=f" by {author}" if author else "",
        date=date,
    )
    cmd = [
        claude_bin,
        "-p",
        prompt,
        "--model",
        model,
        "--allowedTools",
        "Read,Write,Edit,WebSearch,WebFetch",
    ]
    print(f"  running claude ({model}; research + {LENGTHS[2][3]} words — this takes a while)...")
    result = subprocess.run(
        cmd, cwd=workdir, capture_output=True, text=True, timeout=CLAUDE_TIMEOUT_S, env=child_env()
    )
    if result.returncode != 0:
        # The CLI reports auth/config errors on stdout, not stderr.
        detail = (result.stderr.strip() or result.stdout.strip())[:500]
        print(f"  claude exited {result.returncode}: {detail}", file=sys.stderr)
        return False
    return True


def _word_count(fragment: str) -> int:
    return len(re.sub(r"<[^>]+>", " ", fragment).split())


def normalize_entities(fragment: str) -> str:
    """Turn HTML named entities into literal characters.

    Models reach for &ldquo;/&rsquo;/&mdash; by habit, but an EPUB is parsed as
    XML — only the five XML entities are defined, and the firmware builds with
    XML_GE=0 — so anything else is a hard parse error. Numeric references are
    valid XML and pass through untouched.
    """
    keep = {"amp", "lt", "gt", "quot", "apos"}

    def repl(m: re.Match) -> str:
        name = m.group(1)
        if name in keep:
            return m.group(0)
        char = html.entities.html5.get(name + ";")
        return char if char else m.group(0)

    return re.sub(r"&([A-Za-z][A-Za-z0-9]*);", repl, fragment)


def repair_fragment(path: Path) -> str:
    """Normalize entities and strip a leading heading; rewrite in place."""
    original = path.read_text(encoding="utf-8")
    fixed = normalize_entities(original).strip()
    fixed = re.sub(r"^<h[1-3]\b[^>]*>.*?</h[1-3]>\s*", "", fixed, count=1, flags=re.DOTALL)
    if fixed != original:
        path.write_text(fixed, encoding="utf-8")
    return fixed


def validate_outputs(workdir: Path) -> str:
    """Check every output, repairing what can be repaired in place.

    Named entities are rewritten to literals and a leading heading is dropped
    (the builder emits the chapter title itself, and section.bin v35 breaks the
    page before h1-h3, so a leading heading would waste a page). Returns "" when
    everything is present, well-formed and within budget.
    """
    for stem, _, _, budget in LENGTHS:
        path = workdir / f"{stem}.xhtml"
        if not path.exists():
            return f"missing {path.name}"
        fragment = repair_fragment(path)
        try:
            ET.fromstring(f"<div>{fragment}</div>")
        except ET.ParseError as e:
            return f"{path.name} not well-formed XML: {e}"
        words = _word_count(fragment)
        if not budget * WORD_TOLERANCE[0] <= words <= budget * WORD_TOLERANCE[1]:
            return f"{path.name} has {words} words (budget {budget})"
    colophon = workdir / "colophon.xhtml"
    if not colophon.exists():
        return "missing colophon.xhtml"
    try:
        ET.fromstring(f"<div>{repair_fragment(colophon)}</div>")
    except ET.ParseError as e:
        return f"colophon.xhtml not well-formed XML: {e}"
    return ""


# ---------------------------------------------------------------------------
# Summary EPUB assembly (mirrors the repo's stdlib test-epub generators)


def _chapter_doc(title: str, fragment: str, language: str) -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="{language}">
<head><title>{html.escape(title)}</title></head>
<body>
<h1>{html.escape(title)}</h1>
{fragment}
</body>
</html>"""


def build_summary_epub(workdir: Path, title: str, author: str, language: str) -> bytes:
    summary_title = f"{title} - Summary"
    chapters = [(stem, chapter_title) for stem, chapter_title, _, _ in LENGTHS] + [
        ("colophon", "About This Summary")
    ]

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as epub:
        epub.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        epub.writestr(
            "META-INF/container.xml",
            """<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>""",
        )

        manifest, spine, nav = [], [], []
        for stem, chapter_title in chapters:
            fragment = (workdir / f"{stem}.xhtml").read_text(encoding="utf-8")
            epub.writestr(f"OEBPS/{stem}.xhtml", _chapter_doc(chapter_title, fragment, language))
            manifest.append(f'    <item id="{stem}" href="{stem}.xhtml" media-type="application/xhtml+xml"/>')
            spine.append(f'    <itemref idref="{stem}"/>')
            nav.append(f'      <li><a href="{stem}.xhtml">{html.escape(chapter_title)}</a></li>')
        manifest.append('    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>')

        author_meta = f"\n    <dc:creator>{html.escape(author)}</dc:creator>" if author else ""
        epub.writestr(
            "OEBPS/content.opf",
            f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">crosspoint-summary-{re.sub(r"[^a-z0-9]+", "-", title.lower())}</dc:identifier>
    <dc:title>{html.escape(summary_title)}</dc:title>{author_meta}
    <dc:language>{language}</dc:language>
  </metadata>
  <manifest>
{chr(10).join(manifest)}
  </manifest>
  <spine>
{chr(10).join(spine)}
  </spine>
</package>""",
        )

        epub.writestr(
            "OEBPS/nav.xhtml",
            f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Table of Contents</title></head>
<body>
  <nav epub:type="toc">
    <ol>
{chr(10).join(nav)}
    </ol>
  </nav>
</body>
</html>""",
        )
    return buf.getvalue()


def sanitize_filename(name: str) -> str:
    name = re.sub(r'[/\\:*?"<>|\x00-\x1f]', " ", name)
    return re.sub(r" +", " ", name).strip()


# ---------------------------------------------------------------------------


def process_book(card, marker: str, book_path: str, args) -> bool:
    print(f"\n== {book_path}")
    if not card.exists(book_path):
        print(f"  book missing; deleting stale marker {marker}")
        card.delete(marker)
        return False

    workdir = Path(tempfile.mkdtemp(prefix="cp-summary-"))
    try:
        title, author, language, text = extract_epub(card.read(book_path))
        words = len(text.split())
        print(f"  '{title}'{' by ' + author if author else ''} — {words} words extracted")
        if words < 500:
            print("  too little text to summarize; skipping (marker kept)")
            return False

        (workdir / "book.txt").write_text(text, encoding="utf-8")
        (workdir / "metadata.json").write_text(
            json.dumps({"title": title, "author": author, "language": language, "words": words}),
            encoding="utf-8",
        )

        date = datetime.date.today().isoformat()
        for attempt in (1, 2):
            if not run_claude(args.claude_bin, args.model, workdir, title, author, date):
                continue
            problem = validate_outputs(workdir)
            if not problem:
                break
            print(f"  attempt {attempt}: {problem}")
        else:
            print("  giving up on this book (marker kept for a re-run)", file=sys.stderr)
            return False

        epub_bytes = build_summary_epub(workdir, title, author, language)
        dest = f"/books/{sanitize_filename(title)} - Summary.epub"
        card.write(dest, epub_bytes)
        card.delete(marker)
        print(f"  wrote {dest} ({len(epub_bytes) // 1024} KB); marker cleared")
        return True
    finally:
        if args.keep_work:
            print(f"  work dir kept: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--source", default="fs_", help="local card tree (default: fs_)")
    mode.add_argument("--webdav", help="device WebDAV base URL (File Transfer mode), e.g. http://crosspoint.local")
    ap.add_argument("--claude-bin", default="claude", help="claude CLI binary (default: claude)")
    ap.add_argument(
        "--model",
        default="opus",
        help="model for the claude CLI (default: opus). Passed straight through, so any alias the CLI accepts works.",
    )
    ap.add_argument("--list", action="store_true", help="only list tagged books, do nothing")
    ap.add_argument("--keep-work", action="store_true", help="keep the per-book scratch dir for inspection")
    args = ap.parse_args()

    if args.webdav:
        card = WebdavCard(args.webdav)
    else:
        root = Path(args.source)
        if not root.is_dir():
            sys.exit(f"not a directory: {root}")
        card = LocalCard(root)

    markers = list(card.find_markers())
    if not markers:
        print("No books tagged for summarization.")
        return
    print(f"{len(markers)} tagged book(s):")
    for marker, book in markers:
        print(f"  {book}  ({marker})")
    if args.list:
        return

    ok = sum(process_book(card, marker, book, args) for marker, book in markers)
    print(f"\nDone: {ok}/{len(markers)} summaries generated.")


if __name__ == "__main__":
    main()
