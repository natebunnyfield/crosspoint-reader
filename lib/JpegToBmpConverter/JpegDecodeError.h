#pragma once

#include <JPEGDEC.h>

// JPEGDEC reports every failure as a small integer, and the ones that reach this firmware mean
// very different things. `Decode failed (rc=0, lastError=2)` reads like memory pressure, which is
// what sent an earlier investigation into the heap for a file that was simply an unusual --
// and, until the JPEGDEC patches, unsupported -- progressive layout.
//
// Lives here rather than beside either caller because both need it: JpegToFramebufferConverter in
// lib/Epub/Epub/converters (lib/Epub already includes this lib) and JpegToBmpConverter itself.
//
// Every return is a string literal, so the text is flash-resident, costs no allocation, and is
// safe to hand straight to LOG_ERR.
inline const char* jpegDecodeErrorText(const int err) {
  switch (err) {
    case JPEG_SUCCESS:
      return "success";
    case JPEG_INVALID_PARAMETER:
      return "invalid parameter";
    case JPEG_DECODE_ERROR:
      // The catch-all: corrupt data, a truncated file, or a bitstream the decoder walked out of
      // step with. NOT an out-of-memory condition, which is what the bare number suggested.
      return "corrupt or truncated JPEG data";
    case JPEG_UNSUPPORTED_FEATURE:
      // Reachable from the 0003 patch. It decodes a progressive JPEG whose DC coefficients are
      // split into one scan per component, at any subsampling, but only for luma output -- the
      // chroma blocks of such a scan are never read. Colour output, or a first scan that is not
      // the luma DC scan, is refused here rather than rendered as noise.
      return "unsupported JPEG variant (progressive, split DC scan, non-luma output)";
    case JPEG_INVALID_FILE:
      return "not a JPEG file";
    default:
      return "unknown error";
  }
}
