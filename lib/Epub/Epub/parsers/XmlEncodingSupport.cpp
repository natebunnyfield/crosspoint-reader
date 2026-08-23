#include "XmlEncodingSupport.h"

#include <Logging.h>

#include "Epub/BookNotes.h"
#include "XmlEncodingTables.h"

namespace {

// Expat's contract (expat.h, XML_UnknownEncodingHandler): fill map[256] with the
// Unicode scalar each byte stands for, and return XML_STATUS_OK. A negative
// entry means a multi-byte lead and obliges a `convert` function; every table
// here is single-byte, so none is ever written and `convert`/`release` stay
// null.
int XMLCALL unknownEncoding(void* /*handlerData*/, const XML_Char* name, XML_Encoding* info) {
  const uint16_t* highHalf = xmlencodings::highHalfFor(name);
  if (highHalf == nullptr) {
    // Refuse, and SAY SO where the reader can find it. This is the multi-byte
    // case (Shift_JIS, EUC-JP, Big5, GB18030) and the merely obscure one, and
    // the difference between them does not matter to someone holding a book
    // that will not open: what matters is the name, which the note carries.
    booknotes::current().raiseUnsupportedEncoding(name);
    LOG_ERR("XEN", "No table for declared encoding '%s'; this chapter cannot be decoded", name ? name : "(null)");
    return XML_STATUS_ERROR;
  }

  // Below 0x80 every code page this firmware carries is ASCII, and expat
  // REQUIRES that -- XmlInitUnknownEncoding rejects a table whose low half is
  // not the identity for any byte it classifies, so there is nothing to store
  // and nothing to get wrong.
  for (int i = 0; i < 0x80; ++i) {
    info->map[i] = i;
  }
  for (int i = 0; i < 0x80; ++i) {
    info->map[0x80 + i] = highHalf[i];
  }
  info->data = nullptr;
  info->convert = nullptr;
  info->release = nullptr;
  LOG_DBG("XEN", "Decoding as '%s'", name ? name : "(null)");
  return XML_STATUS_OK;
}

}  // namespace

void installXmlEncodingSupport(XML_Parser parser) {
  if (!parser) return;
  // No handler data: the tables are in flash and the note singleton finds
  // itself, so there is nothing per-parser to carry and nothing to keep alive
  // past the parse.
  XML_SetUnknownEncodingHandler(parser, unknownEncoding, nullptr);
}
