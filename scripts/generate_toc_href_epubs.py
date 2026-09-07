#!/usr/bin/env python3
"""
Generate the two test EPUBs for TOC href resolution.

A contents entry reaches a chapter only if its href matches a spine href
character for character (BookMetadataCache::createTocEntry). When it does not,
the entry carries spineIndex -1, Chapter Select's pick is CANCELLED, and the
reader silently repaints the page it was already on -- which is what "every
chapter goes to the same spot" looks like from the outside.

Two real-world layouts break that match, and neither is exotic:

  test_toc_ncx_subdir.epub
      An EPUB 2 whose toc.ncx does NOT sit beside content.opf. NCX `src`
      attributes are relative to the NCX document, so a toc at OEBPS/nav/toc.ncx
      reaches chapters as `../Text/chN.xhtml`. Resolving those against the OPF's
      folder instead produces `Text/chN.xhtml`, and no spine item has that path.
      Every entry in the book fails at once.

  test_toc_dot_paths.epub
      An EPUB 3 whose nav document writes the equivalent-but-not-identical
      `./Text/chN.xhtml`. normalisePath() collapses `..` and used to leave `.`
      alone, so the string never equalled the spine's `Text/chN.xhtml`.

Both books are otherwise ordinary: four chapters, one anchor each, headings the
layout can find. A correct build resolves every entry to its own spine item.
"""

import zipfile
from pathlib import Path

OUTPUT_DIR = Path(__file__).parent.parent / "test" / "epubs"

FILLER = (
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
    "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat."
)

CHAPTERS = [
    ("ch1", "chapter-one", "Chapter One"),
    ("ch2", "chapter-two", "Chapter Two"),
    ("ch3", "chapter-three", "Chapter Three"),
    ("ch4", "chapter-four", "Chapter Four"),
]


def chapter_xhtml(anchor: str, title: str) -> str:
    paras = "\n".join(f"    <p>{FILLER}</p>" for _ in range(6))
    return f"""<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
  <head><title>{title}</title></head>
  <body>
    <h1 id="{anchor}">{title}</h1>
{paras}
  </body>
</html>
"""


def content_opf(*, ncx_href: str | None, nav_href: str | None) -> str:
    manifest = []
    spine = []
    for slug, _, _ in CHAPTERS:
        manifest.append(
            f'    <item id="{slug}" href="Text/{slug}.xhtml" '
            f'media-type="application/xhtml+xml"/>'
        )
        spine.append(f'    <itemref idref="{slug}"/>')
    if ncx_href:
        manifest.append(
            f'    <item id="ncx" href="{ncx_href}" '
            f'media-type="application/x-dtbncx+xml"/>'
        )
    if nav_href:
        manifest.append(
            f'    <item id="nav" href="{nav_href}" properties="nav" '
            f'media-type="application/xhtml+xml"/>'
        )
    spine_attrs = ' toc="ncx"' if ncx_href else ""
    return f"""<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="{'2.0' if ncx_href else '3.0'}" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>TOC href resolution</dc:title>
    <dc:creator>CrossPoint Test Suite</dc:creator>
    <dc:language>en</dc:language>
    <dc:identifier id="bookid">urn:uuid:toc-href-resolution</dc:identifier>
  </metadata>
  <manifest>
{chr(10).join(manifest)}
  </manifest>
  <spine{spine_attrs}>
{chr(10).join(spine)}
  </spine>
</package>
"""


def toc_ncx(href_for) -> str:
    points = []
    for i, (slug, anchor, title) in enumerate(CHAPTERS, start=1):
        points.append(
            f"""    <navPoint id="navpoint-{i}" playOrder="{i}">
      <navLabel><text>{title}</text></navLabel>
      <content src="{href_for(slug)}#{anchor}"/>
    </navPoint>"""
        )
    return f"""<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:uuid:toc-href-resolution"/></head>
  <docTitle><text>TOC href resolution</text></docTitle>
  <navMap>
{chr(10).join(points)}
  </navMap>
</ncx>
"""


def nav_xhtml(href_for) -> str:
    items = "\n".join(
        f'      <li><a href="{href_for(slug)}#{anchor}">{title}</a></li>'
        for slug, anchor, title in CHAPTERS
    )
    return f"""<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
  <head><title>Contents</title></head>
  <body>
    <nav epub:type="toc" id="toc">
      <h1>Contents</h1>
      <ol>
{items}
      </ol>
    </nav>
  </body>
</html>
"""


CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""


def write_epub(path: Path, extra: dict[str, str]) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        # mimetype first and stored, per OCF.
        z.writestr(
            zipfile.ZipInfo("mimetype"), "application/epub+zip",
            compress_type=zipfile.ZIP_STORED,
        )
        z.writestr("META-INF/container.xml", CONTAINER)
        for slug, anchor, title in CHAPTERS:
            z.writestr(f"OEBPS/Text/{slug}.xhtml", chapter_xhtml(anchor, title))
        for name, data in extra.items():
            z.writestr(name, data)
    print(f"wrote {path}")


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # The NCX lives one folder DEEPER than the OPF, so its own hrefs have to
    # climb back out. Resolved against the OPF instead, they land a folder short.
    write_epub(
        OUTPUT_DIR / "test_toc_ncx_subdir.epub",
        {
            "OEBPS/content.opf": content_opf(ncx_href="nav/toc.ncx", nav_href=None),
            "OEBPS/nav/toc.ncx": toc_ncx(lambda slug: f"../Text/{slug}.xhtml"),
        },
    )

    # Same folder, but every href is spelled with a leading "./".
    write_epub(
        OUTPUT_DIR / "test_toc_dot_paths.epub",
        {
            "OEBPS/content.opf": content_opf(ncx_href=None, nav_href="nav.xhtml"),
            "OEBPS/nav.xhtml": nav_xhtml(lambda slug: f"./Text/{slug}.xhtml"),
        },
    )


if __name__ == "__main__":
    main()
