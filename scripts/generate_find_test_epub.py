#!/usr/bin/env python3
"""
Generate test/epubs/test_find.epub, the fixture for test/find_in_book.

Whole-book Find (docs/find-in-book-options-2026-09-28.md section 9) is tested on
a real .epub laid out by the real Section build, so the fixture only has to
guarantee WHICH words exist and WHERE; where the layout breaks lines and pages
is measured by the test, not assumed here.

Five chapters of deterministic filler drawn from short words, plus plants that
occur nowhere else in the book:

  ch1  "marmalade lantern"            only here: the wrap-around target
  ch3  "keeper" twice in one sentence  a second match on the same page
  ch4  "Zanzibar harbour"              a hit in a later chapter
  all  LONG_WORDS, one each            candidates for a line-break hyphen; the
                                       test finds whichever the layout split
  ch2  "super&#173;cali&#173;fragilistic"  soft hyphens in the source
  ch5  "well-known"                    an explicit hyphen

The filler vocabulary is short words only, so no filler word can collide with
a plant.
"""

import os
import random
import zipfile
from pathlib import Path

OUTPUT_PATH = Path(__file__).parent.parent / "test" / "epubs" / "test_find.epub"

VOCAB = (
    "the a of and to in was he she it that on as with his her they at by "
    "this had not are but from or have an were one all we when there can "
    "sea boat rope tide gull wind salt shore rock mist cold grey long slow "
    "old net oar deck mast bell hull pier sand wave dawn dusk night moon"
).split()

# One each in the whole book. Long enough that a narrow page splits some of them.
LONG_WORDS = [
    "incomprehensibility", "institutionalization", "counterrevolutionary",
    "electroencephalograph", "uncharacteristically", "disproportionately",
    "interdisciplinary", "indistinguishable", "misunderstandings",
    "photosynthesizing", "telecommunications", "overcompensation",
    "unconstitutionally", "compartmentalization", "internationalization",
    "responsibilities", "extraordinarily", "characterization",
    "entrepreneurship", "infrastructure", "reconsideration", "individualistic",
    "meteorologically", "circumnavigation", "hypersensitivity",
    "notwithstanding", "instrumentalities", "industrialization",
    "unquestionably", "transcontinental",
]

rng = random.Random(20260928)


def sentence(words=12):
    w = [rng.choice(VOCAB) for _ in range(words)]
    w[0] = w[0].capitalize()
    return " ".join(w) + "."


def paragraph(sentences=4):
    return " ".join(sentence(rng.randint(8, 16)) for _ in range(sentences))


def chapter(title, plants):
    """plants: {paragraph_index: text inserted mid-paragraph}"""
    paras = []
    for i in range(18):
        p = paragraph()
        if i in plants:
            cut = p.index(". ") + 2 if ". " in p else len(p)
            p = p[:cut] + plants[i] + " " + p[cut:]
        paras.append(f"<p>{p}</p>")
    body = "\n".join(paras)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>{title}</title><link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<h2>{title}</h2>
{body}
</body>
</html>"""


def long_word_plants(words):
    return " ".join(f"The {w} of it." for w in words)


CSS = "p { margin: 0; text-indent: 1em; text-align: justify; }\n"

chunks = [LONG_WORDS[i::5] for i in range(5)]
CHAPTERS = [
    ("Chapter One", {2: "She lit the marmalade lantern at dusk.", 5: long_word_plants(chunks[0])}),
    ("Chapter Two", {3: "It was super&#173;cali&#173;fragilistic, they said.", 8: long_word_plants(chunks[1])}),
    ("Chapter Three", {4: "The keeper counted gulls, and then the keeper slept.", 11: long_word_plants(chunks[2])}),
    ("Chapter Four", {6: "They sailed for Zanzibar harbour at night.", 9: long_word_plants(chunks[3])}),
    ("Chapter Five", {1: "A well-known bell rang.", 7: long_word_plants(chunks[4])}),
]


def build(path):
    os.makedirs(path.parent, exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", """<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>""")
        z.writestr("OEBPS/style.css", CSS)
        manifest, spine, nav = [], [], []
        for i, (title, plants) in enumerate(CHAPTERS, 1):
            name = f"ch{i}.xhtml"
            z.writestr(f"OEBPS/{name}", chapter(title, plants))
            manifest.append(f'<item id="ch{i}" href="{name}" media-type="application/xhtml+xml"/>')
            spine.append(f'<itemref idref="ch{i}"/>')
            nav.append(f'<li><a href="{name}">{title}</a></li>')
        manifest.append('<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>')
        manifest.append('<item id="css" href="style.css" media-type="text/css"/>')
        z.writestr("OEBPS/content.opf", f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">test-epub-find</dc:identifier>
    <dc:title>Test: Find in Book</dc:title>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>{"".join(manifest)}</manifest>
  <spine>{"".join(spine)}</spine>
</package>""")
        z.writestr("OEBPS/nav.xhtml", f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Contents</title></head>
<body><nav epub:type="toc"><ol>{"".join(nav)}</ol></nav></body>
</html>""")
    print(f"Generated: {path}")


if __name__ == "__main__":
    build(OUTPUT_PATH)
