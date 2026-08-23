#pragma once

#include <expat.h>

// Teach an expat parser the legacy encodings a book can declare.
//
// Expat builds in FOUR: UTF-8, UTF-16, US-ASCII and ISO-8859-1. Anything else
// -- and "anything else" includes windows-1252, which is the most common
// non-UTF-8 encoding in EPUB by a wide margin -- fails the parse outright with
// XML_ERROR_UNKNOWN_ENCODING unless the host installs a handler. Before this
// existed the reader's symptom was not a blank page or a garbled paragraph: the
// chapter simply never built, and page-forward sat on the same failing chapter
// for as long as the reader kept pressing (sweep item #52).
//
// Call it once per parser, straight after XML_ParserCreate, at EVERY site. A
// parser without it is not a slower path, it is a chapter that cannot open, and
// an EPUB is free to write its OPF in one encoding and its chapters in another.
void installXmlEncodingSupport(XML_Parser parser);
