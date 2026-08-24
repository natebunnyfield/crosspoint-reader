#!/usr/bin/env python3
"""Generate the CSS length-unit regression book.

The fixture behind the SECTION_FILE_VERSION 47 change
(docs/book-notes-and-sparse-ruby-2026-08-23.md, sweep item #23): every unit the
CSS parser did not recognize fell through to PIXELS, so `margin: 1cm` was one
pixel and the page had no margin at all. The way that fails is silent -- the
text is complete, the render succeeds, the log says nothing -- so the only
proof is a measured page.

Four chapters, each one a thing that can be measured off a screenshot:

  1. cm/mm/in/pc/pt margins, one paragraph per unit, all specified to be the
     SAME physical width (1cm = 10mm = 0.3937in = 2.362pc = 28.35pt). Every one
     of those five paragraphs must land on the same left edge. Before the fix
     they landed on five DIFFERENT edges, all of them within a few pixels of no
     margin at all.
  2. A control: the same inset written in px and em, which never changed.
  3. Units with no honest conversion (ex, ch, vw, vh). These must be DROPPED --
     the paragraph sits at the reader's own margin, not at 5 px -- and the book
     must carry the note that says so.
  4. Text this reader must NOT print: a <script>, a <style> and an SVG
     <title>/<desc>, next to an SVG <image> that must still render. Sweep item
     #69, and the reason `svg` itself is not in SKIP_TAGS.

    python3 tools/make_css_units_book.py fs_/books/cssunits.epub
"""
import sys
import struct
import zlib
import zipfile

out = sys.argv[1] if len(sys.argv) > 1 else 'fs_/books/cssunits.epub'

CSS = """
body { margin: 0; padding: 0; }
p { margin-top: 0; margin-bottom: 0; text-indent: 0; }

/* One centimeter, written five ways. Every one of these is the same distance:
   1cm = 10mm = 0.3937in = 2.3622pc = 28.3465pt. Five different left edges on
   the page means the conversion is wrong; five different edges all near zero
   means the units were read as pixels. */
p.cm { margin-left: 1cm; }
p.mm { margin-left: 10mm; }
p.inch { margin-left: 0.3937in; }
p.pc { margin-left: 2.3622pc; }
p.pt { margin-left: 28.3465pt; }

/* The control. These two never changed and must not move. */
p.px { margin-left: 59px; }
p.em { margin-left: 1.31em; }

/* No honest conversion exists for any of these. The paragraph must sit at the
   reader's own margin, NOT at 5 px, and the book must say so. */
p.ex { margin-left: 5ex; }
p.ch { margin-left: 12ch; }
p.vw { margin-left: 13vw; }
p.vh { margin-top: 10vh; }
"""

# A 32x32 solid PNG, so the SVG-wrapped image case has something real to draw.
def _png(width, height, value):
    raw = b''.join(b'\x00' + bytes([value]) * (width * 3) for _ in range(height))

    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))

    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw))
            + chunk(b'IEND', b''))


CHAPTERS = [
    ('Absolute units', """
<h1>Absolute units</h1>
<p class="cm">CM one centimeter of left margin.</p>
<p class="mm">MM ten millimeters, the same distance.</p>
<p class="inch">IN 0.3937 inches, the same distance.</p>
<p class="pc">PC 2.3622 picas, the same distance.</p>
<p class="pt">PT 28.3465 points, the same distance.</p>
<p>NONE no margin at all, for comparison.</p>
"""),
    ('The control', """
<h1>The control</h1>
<p class="px">PX fifty-nine pixels, which is what a centimeter comes to.</p>
<p class="em">EM 1.31 em, which is about the same on this page.</p>
<p>NONE no margin at all, for comparison.</p>
"""),
    ('Units with no conversion', """
<h1>No conversion</h1>
<p class="ex">EX five ex. This paragraph must sit flush, not five pixels in.</p>
<p class="ch">CH twelve ch. Flush as well.</p>
<p class="vw">VW thirteen viewport widths. Flush as well.</p>
<p>NONE no margin at all, for comparison.</p>
"""),
    ('Not prose', """
<h1>Not prose</h1>
<script type="text/javascript">var LEAKED_SCRIPT = "this must not appear";</script>
<style type="text/css">.LEAKED_STYLE { color: red; }</style>
<p>Between the script above and the picture below there must be nothing but
this sentence.</p>
<div><svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"
  width="32" height="32" viewBox="0 0 32 32"><title>LEAKED_SVG_TITLE</title>
  <desc>LEAKED_SVG_DESC</desc>
  <image width="32" height="32" xlink:href="sq.png"/></svg></div>
<p>The square above proves an SVG-wrapped image still renders, which is why
svg itself is not skipped.</p>
"""),
]

CHAPTER = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>{title}</title>'
           '<link rel="stylesheet" type="text/css" href="style.css"/></head>'
           '<body>{body}</body></html>')

OPF = ('<?xml version="1.0" encoding="utf-8"?>\n'
       '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
       '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
       '<dc:identifier id="id">cssunits-fixture</dc:identifier>'
       '<dc:title>CSS Units Fixture</dc:title><dc:language>en</dc:language></metadata>'
       '<manifest>{items}'
       '<item id="css" href="style.css" media-type="text/css"/>'
       '<item id="sq" href="sq.png" media-type="image/png"/>'
       '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
       '</manifest><spine>{refs}</spine></package>')

NAV = ('<?xml version="1.0" encoding="utf-8"?>\n'
       '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">'
       '<head><title>Contents</title></head><body><nav epub:type="toc"><ol>{items}</ol></nav>'
       '</body></html>')

with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('mimetype', 'application/epub+zip', zipfile.ZIP_STORED)
    z.writestr('META-INF/container.xml',
               '<?xml version="1.0"?><container version="1.0" '
               'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>'
               '<rootfile full-path="OEBPS/content.opf" '
               'media-type="application/oebps-package+xml"/></rootfiles></container>')
    z.writestr('OEBPS/style.css', CSS)
    z.writestr('OEBPS/sq.png', _png(32, 32, 0))
    items, refs, nav = '', '', ''
    for i, (title, body) in enumerate(CHAPTERS):
        name = f'ch{i}.xhtml'
        z.writestr(f'OEBPS/{name}', CHAPTER.format(title=title, body=body))
        items += f'<item id="c{i}" href="{name}" media-type="application/xhtml+xml"/>'
        refs += f'<itemref idref="c{i}"/>'
        nav += f'<li><a href="{name}">{title}</a></li>'
    z.writestr('OEBPS/nav.xhtml', NAV.format(items=nav))
    z.writestr('OEBPS/content.opf', OPF.format(items=items, refs=refs))

print(f'{out}: {len(CHAPTERS)} chapters')
