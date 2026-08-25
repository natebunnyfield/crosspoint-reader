#!/usr/bin/env python3
"""Build the line-breaker sweep corpus out of EPUBs on the test card.

test/line_break_quality's DISABLED_Sweep needs a plain-text corpus, one
paragraph per line. The corpus the 2026-08-25 and 2026-08-26 tables were
measured on is every <p> of 30 or more words from two books on the test card:

    python3 tools/linebreak_corpus.py \
        fs_/books/ai-engineering-from-zero.epub \
        fs_/books/wingspan-the-whole-bird.epub \
        /tmp/corpus.txt

which yields 394 paragraphs / 22,881 words.

THIS SCRIPT EXISTS BECAUSE THE CORPUS DOES NOT LIVE IN THE REPO. It is book
text, so it is not checked in, and the first re-measurement had to reconstruct
it from a sentence in the doc. The extraction rule -- <p> elements only, 30-word
floor, whitespace collapsed, no-break spaces folded to spaces -- is the part
that has to survive, not the file.

The 30-word floor is not arbitrary: the DP optimizes over a whole PARAGRAPH, so
its advantage only appears where paragraphs are long enough to have alternative
break sets. The sweep additionally drops any line under 40 bytes.
"""

import sys
import zipfile
from html.parser import HTMLParser


class ParagraphExtractor(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.paragraphs = []
        self._buf = []
        self._in_p = False
        self._skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style"):
            self._skip += 1
        elif tag == "p":
            self._in_p = True
            self._buf = []
        elif tag == "br" and self._in_p:
            self._buf.append(" ")

    def handle_endtag(self, tag):
        if tag in ("script", "style") and self._skip:
            self._skip -= 1
        elif tag == "p" and self._in_p:
            text = " ".join("".join(self._buf).split())
            if text:
                self.paragraphs.append(text)
            self._in_p = False
            self._buf = []

    def handle_data(self, data):
        if self._in_p and not self._skip:
            self._buf.append(data)


def paragraphs_of(epub_path):
    out = []
    with zipfile.ZipFile(epub_path) as z:
        names = sorted(n for n in z.namelist() if n.lower().endswith((".xhtml", ".html", ".htm")))
        for name in names:
            parser = ParagraphExtractor()
            try:
                parser.feed(z.read(name).decode("utf-8", "replace"))
            except Exception:
                continue
            out.extend(parser.paragraphs)
    return out


def main(argv):
    if len(argv) < 3:
        sys.exit("usage: linebreak_corpus.py <book.epub> [book.epub ...] <corpus.txt>")
    *books, dest = argv[1:]

    kept = []
    for book in books:
        for text in paragraphs_of(book):
            # U+00A0 would otherwise survive into the corpus, where the test's
            # splitWords -- which splits on ASCII space only -- would glue the
            # pair into one very wide token and trip the oversized-word pre-pass.
            text = " ".join(text.replace(" ", " ").split())
            if len(text.split()) >= 30:
                kept.append(text)

    with open(dest, "w") as f:
        for text in kept:
            f.write(text + "\n")
    print(f"{len(kept)} paragraphs, {sum(len(t.split()) for t in kept)} words -> {dest}")


if __name__ == "__main__":
    main(sys.argv)
