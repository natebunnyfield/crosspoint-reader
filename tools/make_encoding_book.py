#!/usr/bin/env python3
"""Generate the legacy-encoding regression book.

Sweep item #52 (docs/book-notes-and-sparse-ruby-2026-08-23.md): there was no
XML_SetUnknownEncodingHandler anywhere, so expat refused any XHTML whose
declaration named an encoding it does not build in -- and expat builds in only
UTF-8, UTF-16, US-ASCII and ISO-8859-1. Every Windows code page, every legacy
CJK encoding, every KOI8 or Mac Roman chapter simply failed to parse, and the
reader got STR_INDEX_FAILED with no explanation.

The chapters are written as RAW BYTES in each encoding, not as UTF-8 with a
lying declaration, because the bug is in the decode and a lying declaration
would not exercise it.

  ch0  utf-8         the control: this one always worked
  ch1  windows-1252  the common case -- the 0x80..0x9F band (smart quotes, em
                     dash, ellipsis) is exactly what separates it from latin-1,
                     so a build that quietly treats one as the other shows it
                     here and nowhere else
  ch2  iso-8859-1    expat's own built-in, kept as the control for the handler:
                     it must still decode after the handler is installed
  ch3  windows-1251  Cyrillic: a code page whose upper half is nothing like
                     latin-1, so a wrong table is unmissable
  ch4  shift_jis     multi-byte. Whether it decodes is the question the
                     support decision turns on.

    python3 tools/make_encoding_book.py fs_/books/encodings.epub
"""
import sys, zipfile

out = sys.argv[1] if len(sys.argv) > 1 else 'fs_/books/encodings.epub'

# (title, encoding declared AND used, paragraphs)
CHAPTERS = [
    ('Control, UTF-8', 'utf-8', [
        'This chapter is UTF-8 and has always parsed. It is here so a run that '
        'shows nothing at all can be told from a run where only the legacy '
        'chapters are missing.',
        'Accents for comparison: café, naïve, Brontë, æther.',
    ]),
    ('Windows-1252', 'windows-1252', [
        'A publisher’s XHTML in Windows-1252 — the single most common '
        'legacy encoding in EPUB — with the smart punctuation that lives '
        'in the 0x80–9F band: “curly quotes”, an em dash — '
        'like this — and an ellipsis…',
        'Accents from the shared latin-1 half: café, naïve, '
        'Brontë, æther, 25° and £50.',
        'If this paragraph is on screen and its quotes are curly, the '
        'Windows-1252 table is right. If the quotes are missing or wrong, only '
        'the latin-1 half decoded.',
    ]),
    ('ISO-8859-1', 'iso-8859-1', [
        'Expat decodes latin-1 without any help, so this chapter is the '
        'control for the handler itself: installing one must not break it.',
        'Accents: café, naïve, Brontë, æther, ¡Hola! '
        '¿Qué tal?',
    ]),
    ('Windows-1251 (Cyrillic)', 'windows-1251', [
        'В чащах юга жил '
        'бы цитрус? Да, но '
        'фальшивый экземпляр!',
        'Кодовая страница '
        '1251 — это не latin-1.',
    ]),
    ('Shift_JIS', 'shift_jis', [
        'これは Shift_JIS の章です。',
        '多バイト符号化は変換関数'
        'が必要です。',
    ]),
]

CHAPTER = ('<?xml version="1.0" encoding="{enc}"?>\n'
           '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>{title}</title></head>'
           '<body><h1>{title}</h1>{body}</body></html>')

OPF = ('<?xml version="1.0" encoding="utf-8"?>\n'
       '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
       '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
       '<dc:identifier id="id">encoding-fixture</dc:identifier>'
       '<dc:title>Encoding Fixture</dc:title><dc:language>en</dc:language></metadata>'
       '<manifest>{items}<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" '
       'properties="nav"/></manifest><spine>{refs}</spine></package>')

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
    items, refs, nav = '', '', ''
    for i, (title, enc, paras) in enumerate(CHAPTERS):
        name = f'ch{i}.xhtml'
        body = ''.join(f'<p>{p}</p>' for p in paras)
        text = CHAPTER.format(enc=enc, title=title, body=body)
        # RAW BYTES in the declared encoding. Anything the code page cannot
        # carry is a fixture bug, so no error handler here: let it raise.
        z.writestr(f'OEBPS/{name}', text.encode(enc))
        items += f'<item id="c{i}" href="{name}" media-type="application/xhtml+xml"/>'
        refs += f'<itemref idref="c{i}"/>'
        nav += f'<li><a href="{name}">{title}</a></li>'
    z.writestr('OEBPS/nav.xhtml', NAV.format(items=nav))
    z.writestr('OEBPS/content.opf', OPF.format(items=items, refs=refs))

print(f'{out}: {len(CHAPTERS)} chapters, encodings ' +
      ', '.join(c[1] for c in CHAPTERS))
