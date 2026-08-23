#!/usr/bin/env python3
"""Generate the single-byte XML encoding tables for expat's unknown-encoding
handler.

Sweep item #52 (docs/book-notes-and-sparse-ruby-2026-08-23.md). Expat builds in
exactly four encodings -- UTF-8, UTF-16, US-ASCII and ISO-8859-1 -- and refuses
every other declaration with XML_ERROR_UNKNOWN_ENCODING unless the host installs
an XML_SetUnknownEncodingHandler. This writes the tables that handler answers
with.

Generated, not hand-typed, for the reason derive_phosphors.py is: a code page is
128 numbers and a person transcribing them makes exactly the kind of mistake
nobody reviews. Python's own codec tables are the source, which is also what
makes the round-trip test in test/xml_encodings meaningful -- it re-derives a
sample from the same codecs and compares.

    python3 tools/gen_xml_encodings.py

Writes lib/Epub/Epub/parsers/XmlEncodingTables.cpp (data + lookup) against the
hand-written XmlEncodingTables.h.
"""
import codecs
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / 'lib/Epub/Epub/parsers/XmlEncodingTables.cpp'

# The byte a code page leaves undefined. U+FFFD, not -1 (expat's "illegal
# byte"): a single stray byte must not fail the whole chapter, and the
# replacement character is the honest thing to put where a byte meant nothing.
UNDEFINED = 0xFFFD

# (identifier, python codec, aliases as they appear in the wild).
#
# The aliases are matched after NORMALISATION -- lowercased, every character
# that is not a letter or a digit dropped -- so "Windows-1252", "windows_1252"
# and "WINDOWS1252" are one entry and only genuinely different spellings need
# listing.
#
# The set is the whole single-byte space a legacy EPUB can plausibly declare.
# Choosing among them would cost more argument than it saves flash: one table is
# 256 bytes, and the entire set below is under 8 KB against 1.5 MB free.
ENCODINGS = [
    ('cp1250', 'cp1250', ['windows-1250', 'cp1250', 'x-cp1250', 'ms-ee']),
    ('cp1251', 'cp1251', ['windows-1251', 'cp1251', 'x-cp1251', 'ms-cyrl']),
    ('cp1252', 'cp1252', ['windows-1252', 'cp1252', 'x-cp1252', 'ms-ansi', 'ansi']),
    ('cp1253', 'cp1253', ['windows-1253', 'cp1253', 'ms-greek']),
    ('cp1254', 'cp1254', ['windows-1254', 'cp1254', 'ms-turk']),
    ('cp1255', 'cp1255', ['windows-1255', 'cp1255', 'ms-hebr']),
    ('cp1256', 'cp1256', ['windows-1256', 'cp1256', 'ms-arab']),
    ('cp1257', 'cp1257', ['windows-1257', 'cp1257', 'winbaltrim']),
    ('cp1258', 'cp1258', ['windows-1258', 'cp1258']),
    ('cp874', 'cp874', ['windows-874', 'cp874', 'iso-8859-11', 'tis-620', 'tis620']),
    ('iso8859_2', 'iso8859_2', ['iso-8859-2', 'iso8859-2', 'latin2', 'l2']),
    ('iso8859_3', 'iso8859_3', ['iso-8859-3', 'iso8859-3', 'latin3', 'l3']),
    ('iso8859_4', 'iso8859_4', ['iso-8859-4', 'iso8859-4', 'latin4', 'l4']),
    ('iso8859_5', 'iso8859_5', ['iso-8859-5', 'iso8859-5', 'cyrillic']),
    ('iso8859_6', 'iso8859_6', ['iso-8859-6', 'iso8859-6', 'arabic', 'asmo-708']),
    ('iso8859_7', 'iso8859_7', ['iso-8859-7', 'iso8859-7', 'greek', 'greek8', 'ecma-118']),
    ('iso8859_8', 'iso8859_8', ['iso-8859-8', 'iso8859-8', 'hebrew']),
    ('iso8859_9', 'iso8859_9', ['iso-8859-9', 'iso8859-9', 'latin5', 'l5']),
    ('iso8859_10', 'iso8859_10', ['iso-8859-10', 'iso8859-10', 'latin6', 'l6']),
    ('iso8859_13', 'iso8859_13', ['iso-8859-13', 'iso8859-13', 'latin7', 'l7']),
    ('iso8859_14', 'iso8859_14', ['iso-8859-14', 'iso8859-14', 'latin8', 'l8']),
    ('iso8859_15', 'iso8859_15', ['iso-8859-15', 'iso8859-15', 'latin9', 'l9', 'latin-9']),
    ('iso8859_16', 'iso8859_16', ['iso-8859-16', 'iso8859-16', 'latin10', 'l10']),
    ('koi8_r', 'koi8_r', ['koi8-r', 'koi8r', 'cskoi8r']),
    ('koi8_u', 'koi8_u', ['koi8-u', 'koi8u']),
    ('mac_roman', 'mac_roman', ['macintosh', 'mac-roman', 'x-mac-roman', 'macroman']),
    ('mac_cyrillic', 'mac_cyrillic', ['x-mac-cyrillic', 'mac-cyrillic', 'maccyrillic']),
    ('mac_greek', 'mac_greek', ['x-mac-greek', 'mac-greek', 'macgreek']),
    ('mac_latin2', 'mac_latin2', ['x-mac-ce', 'x-mac-centraleurroman', 'mac-centraleurope', 'maccentraleurope']),
]


def normalise(name):
    return re.sub(r'[^a-z0-9]', '', name.lower())


def high_half(codec):
    """Bytes 0x80..0xFF as Unicode scalars, and a check that 0x00..0x7F is
    plain ASCII -- expat REFUSES a table whose low half is not the identity for
    any byte it classifies (XmlInitUnknownEncoding, xmltok.c), so a code page
    that reassigns one is not installable at all and must not be offered."""
    for i in range(0x80):
        try:
            if bytes([i]).decode(codec) != chr(i):
                raise SystemExit(f'{codec}: byte {i:#04x} is not ASCII-identical; expat cannot take this table')
        except UnicodeDecodeError:
            raise SystemExit(f'{codec}: byte {i:#04x} is undefined in the low half')
    out = []
    for i in range(0x80, 0x100):
        try:
            cp = ord(bytes([i]).decode(codec))
        except UnicodeDecodeError:
            cp = UNDEFINED
        if cp > 0xFFFF:
            raise SystemExit(f'{codec}: byte {i:#04x} is outside the BMP; expat cannot take this table')
        out.append(cp)
    return out


def main():
    seen = {}
    for ident, codec, aliases in ENCODINGS:
        codecs.lookup(codec)  # raises if this Python has no such codec
        for a in aliases:
            n = normalise(a)
            if seen.get(n, ident) != ident:
                raise SystemExit(f'alias {a!r} claimed by both {seen[n]} and {ident}')
            seen[n] = ident

    lines = []
    w = lines.append
    w('// GENERATED by tools/gen_xml_encodings.py -- do not edit.')
    w('//')
    w('// The high half of every single-byte code page this firmware can decode, as')
    w('// Unicode scalars. 0xFFFD marks a byte the code page leaves undefined: a stray')
    w('// byte must cost one character, never the chapter.')
    w('//')
    w('// The low half is not stored. Every code page here is ASCII-identical below')
    w('// 0x80 -- the generator refuses one that is not, because expat refuses one that')
    w('// is not (XmlInitUnknownEncoding in xmltok.c checks it byte by byte).')
    w('#include "XmlEncodingTables.h"')
    w('')
    w('#include <cstring>')
    w('')
    w('namespace xmlencodings {')
    w('namespace {')
    w('')
    for ident, codec, _ in ENCODINGS:
        table = high_half(codec)
        w(f'// {codec}')
        w(f'const uint16_t k_{ident}[128] = {{')
        for row in range(0, 128, 8):
            cells = ', '.join(f'0x{c:04X}' for c in table[row:row + 8])
            w(f'    {cells},')
        w('};')
        w('')
    w('struct Alias {')
    w('  const char* normalised;  // lowercase, letters and digits only')
    w('  const uint16_t* table;')
    w('};')
    w('')
    w('// Sorted by nothing in particular: the list is short and a chapter declares an')
    w('// encoding once, so a linear walk costs less than the code to avoid it.')
    w('const Alias kAliases[] = {')
    emitted = set()
    alias_rows = 0
    for ident, _, aliases in ENCODINGS:
        for a in aliases:
            n = normalise(a)
            if n in emitted:
                continue  # a spelling that only differed by punctuation
            emitted.add(n)
            alias_rows += 1
            w(f'    {{"{n}", k_{ident}}},')
    w('};')
    w('')
    w('}  // namespace')
    w('')
    w('void normaliseEncodingName(const char* name, char* out, const size_t outSize) {')
    w('  size_t n = 0;')
    w('  if (outSize == 0) return;')
    w('  for (size_t i = 0; name != nullptr && name[i] != \'\\0\' && n + 1 < outSize; ++i) {')
    w('    const char c = name[i];')
    w('    if (c >= \'a\' && c <= \'z\') {')
    w('      out[n++] = c;')
    w('    } else if (c >= \'A\' && c <= \'Z\') {')
    w('      out[n++] = static_cast<char>(c - \'A\' + \'a\');')
    w('    } else if (c >= \'0\' && c <= \'9\') {')
    w('      out[n++] = c;')
    w('    }')
    w('  }')
    w('  out[n] = \'\\0\';')
    w('}')
    w('')
    w('const uint16_t* highHalfFor(const char* declaredName) {')
    w('  char key[48];')
    w('  normaliseEncodingName(declaredName, key, sizeof(key));')
    w('  if (key[0] == \'\\0\') return nullptr;')
    w('  for (const auto& alias : kAliases) {')
    w('    if (std::strcmp(alias.normalised, key) == 0) return alias.table;')
    w('  }')
    w('  return nullptr;')
    w('}')
    w('')
    # `extern`: a namespace-scope const has INTERNAL linkage in C++, so the
    # header's extern declaration would find nothing to link against.
    w(f'extern const size_t kSingleByteEncodingCount = {len(ENCODINGS)};')
    w(f'extern const size_t kEncodingAliasCount = {alias_rows};')
    w('')
    w('}  // namespace xmlencodings')
    OUT.write_text('\n'.join(lines) + '\n')
    total = len(ENCODINGS) * 128 * 2
    print(f'{OUT.relative_to(REPO)}: {len(ENCODINGS)} code pages, '
          f'{alias_rows} aliases, {total} bytes of table')


if __name__ == '__main__':
    main()
