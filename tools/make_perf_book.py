#!/usr/bin/env python3
"""Generate a novel-sized EPUB for pagination benchmarking.

The corpus behind docs/performance-indexing-2026-08-23.md. Two shapes:

  python3 tools/make_perf_book.py fs_/books/measure.epub 24 120   # a novel
  python3 tools/make_perf_book.py fs_/books/giant.epub    1 2800  # one spine

Deterministic (seeded) so the same book comes out every run -- a perf
measurement that changes its own input is not a measurement. Words come from
/usr/share/dict/words so hyphenation and kerning see real English rather than
a repeated lorem block, and the markup carries the inline/blockquote variety
that makes the parser take more than one branch.
"""
import random, sys, zipfile, os

out = sys.argv[1]
chapters = int(sys.argv[2]) if len(sys.argv) > 2 else 24
paras_per_ch = int(sys.argv[3]) if len(sys.argv) > 3 else 120

rnd = random.Random(20260823)
words = [w.strip() for w in open('/usr/share/dict/words', encoding='latin-1')
         if 3 <= len(w.strip()) <= 12 and w.strip().isalpha()]
# Zipf-ish: a small common core plus a long tail, like real prose.
core = [w.lower() for w in rnd.sample(words, 400)]
tail = [w.lower() for w in rnd.sample(words, 8000)]

def word():
    return rnd.choice(core) if rnd.random() < 0.75 else rnd.choice(tail)

def sentence():
    n = rnd.randint(6, 24)
    ws = [word() for _ in range(n)]
    ws[0] = ws[0].capitalize()
    s = ' '.join(ws)
    if rnd.random() < 0.12:
        s = '“' + s + ',” he said'
    return s + rnd.choice(['.', '.', '.', '.', '?', '!', '— and so on.'])

def para():
    return ' '.join(sentence() for _ in range(rnd.randint(3, 9)))

CONTAINER = '''<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>'''

CSS = '''body { margin: 0; font-family: serif; }
p { text-indent: 1.2em; margin: 0; }
p.first { text-indent: 0; }
h1 { font-size: 1.6em; margin: 1em 0; text-align: center; }
em { font-style: italic; }
strong { font-weight: bold; }
blockquote { margin: 1em 2em; font-style: italic; }
'''

zf = zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED)
zf.writestr('mimetype', 'application/epub+zip', zipfile.ZIP_STORED)
zf.writestr('META-INF/container.xml', CONTAINER)
zf.writestr('OEBPS/style.css', CSS)

manifest, spine, nav = [], [], []
total = 0
for c in range(1, chapters + 1):
    body = []
    for i in range(paras_per_ch):
        t = para()
        # Some inline markup, some block variety -- exercises the parser's
        # element handling rather than one flat paragraph shape.
        if rnd.random() < 0.18:
            ws = t.split(' ')
            k = rnd.randrange(len(ws))
            ws[k] = '<em>' + ws[k] + '</em>'
            if len(ws) > 6:
                j = rnd.randrange(len(ws))
                ws[j] = '<strong>' + ws[j] + '</strong>'
            t = ' '.join(ws)
        if rnd.random() < 0.04:
            body.append('<blockquote><p>' + t + '</p></blockquote>')
        else:
            body.append('<p%s>%s</p>' % (' class="first"' if i == 0 else '', t))
    html = ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<html xmlns="http://www.w3.org/1999/xhtml"><head>'
            '<title>Chapter %d</title>'
            '<link rel="stylesheet" type="text/css" href="style.css"/></head>'
            '<body><h1 id="ch%d">Chapter %d</h1>\n%s\n</body></html>'
            % (c, c, c, '\n'.join(body)))
    total += len(html)
    name = 'chap%02d.xhtml' % c
    zf.writestr('OEBPS/' + name, html)
    manifest.append('<item id="c%d" href="%s" media-type="application/xhtml+xml"/>' % (c, name))
    spine.append('<itemref idref="c%d"/>' % c)
    nav.append('<navPoint id="n%d" playOrder="%d"><navLabel><text>Chapter %d</text></navLabel>'
               '<content src="%s#ch%d"/></navPoint>' % (c, c, c, name, c))

opf = ('<?xml version="1.0" encoding="utf-8"?>\n'
       '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="bid">'
       '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
       '<dc:title>Measure</dc:title><dc:creator>Perf Harness</dc:creator>'
       '<dc:language>en</dc:language><dc:identifier id="bid">measure-20260823</dc:identifier>'
       '</metadata><manifest>'
       '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
       '<item id="css" href="style.css" media-type="text/css"/>'
       + ''.join(manifest) +
       '</manifest><spine toc="ncx">' + ''.join(spine) + '</spine></package>')
zf.writestr('OEBPS/content.opf', opf)

ncx = ('<?xml version="1.0" encoding="utf-8"?>\n'
       '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
       '<head><meta name="dtb:uid" content="measure-20260823"/></head>'
       '<docTitle><text>Measure</text></docTitle>'
       '<navMap>' + ''.join(nav) + '</navMap></ncx>')
zf.writestr('OEBPS/toc.ncx', ncx)
zf.close()
print('%s: %d chapters, %d bytes of xhtml, %d bytes zipped'
      % (out, chapters, total, os.path.getsize(out)))
