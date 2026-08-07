# Open work

Things to do that are not defects. Defects live in [BUGS.md](BUGS.md); this file
is for work that was asked for and has not landed.

It exists for the same reason `BUGS.md` does: todos were being carried in chat,
where they survive only as long as the session does.

Format: `### [T-NNN] Title` then what it is, why, and what "done" looks like.
An item leaves this file when it ships or when it is ruled out — not when it is
started.

---

## OPEN

### [T-001] Switch all icons to the Lucide set
**scope: UI · asked 2026-08-07 · RULED 2026-08-07, not yet implemented**

### The rulings

| UIIcon | Chosen |
|---|---|
| Transfer | `arrow-right-left` |
| Folder | `folder` |
| Book | `book` |
| Recent | `clock` |
| Settings | `settings-2` |
| ManageFiles | `folder-cog` |
| CreateNote | `square-pen` |
| Library | `library-big` |
| Wifi | `wifi` |
| Hotspot | `radio` |
| ClaudeMark | authored `claude-mark.svg`, `bot` as the fallback |
| Text | `file-text` |
| Image | `image` |
| File | `file` |

Chooser page (current vs candidates, both from the real assets):
https://claude.ai/code/artifact/17ed9181-f110-4766-814a-0f595836535b

### The constraint that governs the implementation

**Every converted icon must be stored PRE-ROTATED 90° CCW.** The renderer turns
content 90° CCW into the landscape framebuffer for Portrait, so the existing
headers are authored already turned — decode one literally and you get the panel
image rotated 90° CCW. Confirmed by decoding `wifi.h`: as stored it is sideways,
rotated 90° CW it is a correct wifi glyph.

Convert a Lucide SVG straight to a bitmap and it ships sideways. That is exactly
the `MoonIcon.h` bug the simulator's notes already record, and it is the single
easiest way to get this migration wrong.

### The Claude mark

`claude.h` is the Anthropic starburst, and at 32×32 it decodes to a near-solid
blob — the rays merge. `src/components/icons/claude-mark.svg` is a stroke-based
version (10 rays, round caps, Lucide's 24×24 / stroke-width 2 geometry) that
survives 1-bit at panel size. Lucide `bot` stays the fallback if the brand mark
is ever unwanted.

### Also in scope

The duplicate size-variant headers — `book`/`book24`, `folder`/`folder24`,
`file24`, `image24`, `text24`, 5 of the 18 — collapse as part of this. Three
rows legitimately resolve to the same or adjacent Lucide glyphs (`Text`, `File`
both near `file-text`), which is a consequence of that de-duplication rather
than a mistake.

### Done looks like

Every `UIIcon` resolves to its chosen glyph, pre-rotated; the size-variant
headers are gone or generated; and Home / Manage Files / Settings are
screenshotted before and after, so it is judged on the panel rather than in the
source.

---

## Carried over, still owner decisions

These were raised in the audit and have not been ruled on. They are not defects,
so they are not in `BUGS.md`.

### [T-002] `src/notes/` versus `SCOPE.md`
`SCOPE.md:56-57` bans "notepads" and "typed notes, journals, or editors" by
name; `ROADMAP.md:75-76` repeats it. The note editor and Claude chat ship in
every device binary — `build_src_filter` appears only on the simulator envs, so
PlatformIO's `+<*>` default compiles `src/notes/` into `default`, `gh_release`,
`gh_release_rc`, `slim` and `sticky`.

Not a removal proposal: this is capability that was deliberately built. But the
docs currently give three different answers about the bar (`ROADMAP.md:77` bans
connectivity outright, `SCOPE.md:35` calls it a temporary freeze, `SCOPE.md:58`
bans only browsers and feed readers), which is how it got in without anyone
deciding. Keep it and amend the docs, gate it out of device builds, or retire
it — but the docs should stop contradicting the binary either way.

### [T-003] The published SSID and the API key
`ClaudeChat.cpp:29` hardcodes a home SSID in a public repo, and
`claude-key.txt` sits at the SD root where the file-transfer server lists and
serves it — `isProtectedItemName` covers only dot-prefixed names plus
`{"System Volume Information", "XTCache"}`, and only on rename/delete/move.

The code fix is small and unambiguous. **Scrubbing the SSID from published
history is the part that needs a decision** — it means a force-push against a
public fork.

Note the exposure widened on 2026-08-07: before the network split, WebDAV was
not compiled into iOS, so the key was not servable from the phone. Now it is.

### [T-004] Make the simulator stop lying about the device
`S-001` in the simulator's `BUGS.md` lists six places where it reports the
opposite of the hardware. The 1 MB free-heap constant is the one that matters:
every graceful-degradation path on a 380 KB device is unreachable in the only
pre-device gate the project has.

Scoped as its own piece of work, not a cleanup. A budgeted fake heap would reach
most of the dead branches.
