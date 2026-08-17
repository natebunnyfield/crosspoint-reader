# Progressive JPEGs, and the scan layout that broke them

Investigated and fixed 2026-08-17, against `ee6fad7e5`. Closes
[T-015](../TODO.md). Everything below was verified on the host with the real
vendored JPEGDEC unless it says otherwise.

## What a progressive JPEG costs us in the first place

JPEGDEC decodes only the FIRST scan of a progressive stream, which carries the
DC coefficients. That is a one-eighth-resolution preview, upscaled back to the
target size — `DecodeJPEG` forces `JPEG_SCALE_EIGHTH` for `ucMode == 0xc2`
(`jpeg.inl:4974`), and both converters mirror that geometry rather than fight
it (`JpegToFramebufferConverter.cpp:455`, `JpegToBmpConverter.cpp:541-549`).
Fine detail is gone; the picture is recognizable. That is unchanged by this
work and is not a defect — decoding all scans needs a full coefficient buffer,
which does not fit in 380 KB.

## The bug

A progressive JPEG may split its DC coefficients into **one scan per
component** — an SOS naming a single component, with `Ss=0 Se=0` — instead of
one interleaved DC scan naming all three. MozJPEG and most web optimizers emit
that layout by default, so it arrives in ripped and scanned material.

`DecodeJPEG`'s MCU loop always reads a Y block (four, when subsampled) and then
the chroma blocks of the same MCU. In a non-interleaved scan **those chroma bits
do not exist**, so the reads consume bits belonging to the following Y blocks.
The bitstream desynchronizes immediately.

It surfaces two ways, and the quiet one is worse:

| | Symptom | What it looks like |
|---|---|---|
| Large image | the next Huffman lookup fails: `rc=0`, `lastError=2` | reads as memory pressure — `JPEG_DECODE_ERROR` is the same code corrupt files return |
| Small image | **no error at all** | a scrambled preview, reported as a successful decode |

Measured on a 96×64 fixture against a Pillow reference, before the fix: mean
|diff| **107.6** at 4:4:4 and **113.5** at 4:2:0, both reporting success. For
scale, a correct DC-only preview of the same image scores 0.76 and 0.94.

## The fix

`scripts/jpegdec_patches/0003-decode-non-interleaved-dc-scans.patch`.

In a non-interleaved scan the data units run in raster order over the
**component's own block grid** — `ceil(width/8) x ceil(height/8)` for luma,
whatever the subsampling — and one data unit counts as one MCU for the restart
interval (T.81 A.2.2). That is exactly JPEGDEC's `0x11` geometry. So the patch
re-derives the traversal as 1:1 for such a scan and skips the chroma reads.

This is worth stating plainly because it is the whole reason the patch is
small: **the subsampled case needs no special handling.** It is tempting to
think a 2x2 MCU holding four Y blocks needs its own transposition — it does
not, because the scan is not MCU-ordered at all. Re-deriving the geometry is
not an approximation of the right answer, it is the traversal the standard
prescribes.

Refused rather than guessed at, both with `JPEG_UNSUPPORTED_FEATURE`:

* **Color output.** Only luma is read from such a scan, so a color caller
  would compose against chroma blocks that were never decoded. Every caller in
  this firmware asks for `EIGHT_BIT_GRAYSCALE`, so this is a guard, not a
  limitation we ship.
* **A single-component first scan that is not the luma DC scan.** Only a
  malformed file gets here.

`lib/JpegToBmpConverter/JpegDecodeError.h` turns JPEGDEC's error integers into
text at both decode-failure sites. `Decode failed (rc=0, lastError=2)` is what
sent the original investigation into the heap; it now names the cause. The idea
and the header shape are `eszter007/matcha-reader`'s (`669d2ac01`); the text is
ours, because our refusal set is different.

## How it was verified

Six-file corpus, `test/progressive_jpeg/fixtures/` (see its README for
provenance and the regeneration commands):

| File | Before | After |
|---|---|---|
| `prog-noninter-444.jpg` | rc=1, scrambled (107.6) | rc=1, **byte-identical to its interleaved twin** |
| `prog-noninter-420.jpg` | rc=1, scrambled (113.5) | rc=1, **byte-identical to its interleaved twin** |
| `real-noninter-420.jpg` | rc=0, `JPEG_DECODE_ERROR` | rc=1, mean |diff| 5.3 vs Pillow (DC-only vs full decode) |
| `baseline-420.jpg` | rc=1 | byte-identical to before |
| `prog-inter-444.jpg` | rc=1 | byte-identical to before |
| `prog-inter-420.jpg` | rc=1 | byte-identical to before |
| `prog-gray.jpg` | rc=1 | byte-identical to before |

The twin comparison is the load-bearing one and it needs no golden file: an
interleaved and a non-interleaved encoding of the same source image carry the
same DC coefficients, so their previews must come out byte-for-byte equal. They
do — and the control test asserts that the interleaved 4:4:4 and 4:2:0
encodings already agree, without which "matches its twin" would prove nothing.

`test/progressive_jpeg/` pins all of it: **7 tests, and 4 of them fail against
an unpatched decoder** (checked, by building the same suite against a pristine
clone of the pinned JPEGDEC sha with only patches 0001 and 0002 applied). The
suite compiles the real vendored decoder and FATAL_ERRORs if that copy is
absent or unpatched, because a stub would prove nothing about a bug that lives
in the MCU loop.

Patches 0001, 0002 and 0003 apply in order to a pristine clone of
`86282979224c8a32fd51e091ed5a35b0c699a52b`, and each is idempotent under
`patch_jpegdec.py`'s reverse-check.

## How common is this, really

Scanned every JPEG in every EPUB on the owner's machine — ~1,900 images:

* 1,907 baseline
* 12 progressive, interleaved DC (six images, in two copies of one book)
* **1 progressive, non-interleaved DC** — `illustration-108.jpg` in the
  Standard Ebooks edition of Beatrix Potter's short fiction, 642×800 4:2:0

So: rare in this library, but not hypothetical, and the one specimen is the
subsampled variant — which matters for what follows. Ordinary tools do not
distinguish the layouts (`file`, `identify` and Pillow all say only
"progressive"); the marks are SOF2 plus an SOS with one component and
`Ss=0 Se=0`.

## What the two sibling forks had

T-015 existed because two forks touched this area independently, which was
taken as evidence the bug was real. It was, but only one of them is about it:

* **`eszter007/matcha-reader` `669d2ac01`** — the same bug, found the same way,
  and a patch built on the same reading of the spec. It fixes the 4:4:4 case
  and **refuses the subsampled one** with `JPEG_UNSUPPORTED_FEATURE`, telling
  the owner to re-encode. Since MozJPEG's default is 4:2:0, and since the only
  real specimen found here is 4:2:0, that refusal covers the common case rather
  than the rare one. Our patch decodes it. Worth offering back upstream to
  matcha.
* **`uxjulia/CrossInk` `886a2ae68` / `f39a11fee`, "improve progressive jpeg
  cover support"** — **nothing to take; already present.** It detects SOF2 by
  walking markers, forces `JPEG_SCALE_EIGHTH`, and threads the 1/8 geometry
  through the scaler. This fork already does all three, and by asking JPEGDEC
  (`getJPEGType()`) rather than re-parsing the file: `JpegToFramebufferConverter.cpp:426,455`
  and `JpegToBmpConverter.cpp:541-549`, which additionally smooths the upscaled
  DC preview — something CrossInk does not do. Do not re-propose it.

One adjacent CrossInk change, noted and NOT taken: `c3d04a9dd` raises the max
JPEG width from 2048 to 4096. Our limit lives in `validateImageDimensions` and
is a memory guard on a 380 KB device; raising it is a separate decision with a
RAM cost, not part of this fix.

## Still owed

**Device-unconfirmed.** The whole verification above is host-side. Nothing here
can be felt off-device, but two things should be watched when hardware is next
in hand:

* An EPUB with a non-interleaved progressive cover renders its cover on Home and
  as the sleep screen, rather than falling back to the generated cover.
* Decode time for such a cover is in the same range as any other progressive
  one — the traversal changed, the work per block did not, so it should be.

The simulator cannot verify any of this: `crosspoint-simulator/src/JPEGDEC.h`
substitutes stb_image for the real decoder unless
`CROSSPOINT_SIM_USE_NATIVE_DECODERS` is set, and stb_image handles this layout
correctly on its own. That is exactly the "simulator answers differently from
the device" trap — it would have shown a perfect cover throughout.
