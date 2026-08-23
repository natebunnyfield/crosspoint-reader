#!/usr/bin/env python3
"""Generate the ruby (furigana) regression book.

The fixture behind the SECTION_FILE_VERSION 45 change
(docs/book-notes-and-sparse-ruby-2026-08-23.md): the ruby record stopped being
a string per word, and the way that change fails is SILENTLY -- the annotations
simply stop appearing, in books nobody on this project opens between one
refactor and the next.

Two chapters, deliberately:

  1. LATIN ruby. Ruby is not a CJK feature as far as this code is concerned:
     <ruby>base<rt>gloss</rt></ruby> puts the gloss above the base whatever the
     script. This chapter is the one you can SEE on a device carrying only the
     Latin reading faces this repo ships -- a Japanese chapter renders as blank
     boxes there, which looks exactly like a dropped annotation.
  2. JAPANESE ruby, the real thing, including a multi-character base whose
     annotation spans the group (the RUBY_CONTINUE path). It proves the
     multibyte round trip on a card that has a CJK font.

    python3 tools/make_ruby_book.py fs_/books/ruby.epub
"""
import sys, zipfile

out = sys.argv[1] if len(sys.argv) > 1 else 'fs_/books/ruby.epub'

LATIN = [
    ('Ruby above Latin text', [
        'The reading of a word can be printed above it, which is what '
        '<ruby>ruby<rt>rubi</rt></ruby> markup is for, and it is not limited to '
        'Japanese.',
        'A glossary line: <ruby>colonel<rt>KER-nel</rt></ruby> and '
        '<ruby>Worcestershire<rt>WUUS-ter-sheer</rt></ruby> and '
        '<ruby>quay<rt>kee</rt></ruby> all read nothing like they are spelled.',
        'Most words on this line carry no annotation at all, which is exactly '
        'the case the sparse encoding is built for: only '
        '<ruby>three<rt>3</rt></ruby> of them do.',
    ]),
    ('Furigana', [
        '<ruby>日本語<rt>にほんご</rt></ruby>の<ruby>文章<rt>ぶんしょう</rt></ruby>です。',
        '<ruby>漢字<rt>かんじ</rt></ruby>に<ruby>振り仮名<rt>ふりがな</rt></ruby>を'
        '<ruby>付<rt>つ</rt></ruby>けます。',
        'ルビのない<ruby>行<rt>ぎょう</rt></ruby>もあります。',
    ]),
]

CHAPTER = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>{title}</title></head>'
           '<body><h1>{title}</h1>{body}</body></html>')

OPF = ('<?xml version="1.0" encoding="utf-8"?>\n'
       '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
       '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
       '<dc:identifier id="id">ruby-fixture</dc:identifier>'
       '<dc:title>Ruby Fixture</dc:title><dc:language>en</dc:language></metadata>'
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
    for i, (title, paras) in enumerate(LATIN):
        name = f'ch{i}.xhtml'
        body = ''.join(f'<p>{p}</p>' for p in paras)
        z.writestr(f'OEBPS/{name}', CHAPTER.format(title=title, body=body))
        items += f'<item id="c{i}" href="{name}" media-type="application/xhtml+xml"/>'
        refs += f'<itemref idref="c{i}"/>'
        nav += f'<li><a href="{name}">{title}</a></li>'
    z.writestr('OEBPS/nav.xhtml', NAV.format(items=nav))
    z.writestr('OEBPS/content.opf', OPF.format(items=items, refs=refs))

print(f'{out}: {len(LATIN)} chapters')
