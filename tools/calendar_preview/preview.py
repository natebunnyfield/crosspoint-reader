# Superseded by render_harness.cpp — see README.md.
#
# This file previously reimplemented the calendar layout in Python. That
# approach could not catch the real bugs (drawText baseline semantics,
# ink-vs-ascender centring, cross-year header overflow) because it did not
# use the firmware's own renderer. Build ./build.sh and run ./render_harness
# instead; it links the actual GfxRenderer and fonts.
raise SystemExit(__doc__ or "superseded by render_harness.cpp; see README.md")
