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
**scope: UI · asked 2026-08-07**

Move the icon set over to Lucide. **Where more than one Lucide icon fits a
concept, present the options rather than picking one** — that is the explicit
ask, and it is the part that needs owner input rather than a commit.

Where it stands today: 14 semantic values in `UIIcon`
(`src/components/themes/BaseTheme.h:114`) — `Folder, Text, Image, Book, File,
Recent, Settings, Transfer, Library, Wifi, Hotspot, ManageFiles, CreateNote,
ClaudeMark` — backed by 18 headers in `src/components/icons/`, several of which
are the same glyph at a second size (`book`/`book24`, `folder`/`folder24`,
`file24`, `image24`, `text24`).

Lucide is already vendored: `freeink-sdk/libs/assets/Icons/lucide`, **3,470
icons**, so this needs no new dependency.

Concepts where Lucide plainly offers several defensible choices, i.e. the ones
that need a ruling rather than a lookup:

| UIIcon | Lucide candidates |
|---|---|
| `Book` | `book`, `book-open`, `book-marked`, `library` |
| `Recent` | `history`, `clock`, `rotate-ccw` |
| `Settings` | `settings`, `settings-2`, `sliders-horizontal` |
| `Transfer` | `send`, `share-2`, `upload-cloud`, `arrow-right-left` |
| `ManageFiles` | `folder-cog`, `folder-tree`, `files` |
| `CreateNote` | `file-plus`, `notebook-pen`, `square-pen` |
| `Library` | `library`, `library-big`, `books` |
| `Hotspot` | `radio`, `wifi`, `router` |

**Do not send a link for this.** Per the standing rule about visual decisions,
the choices have to arrive inside the question — rendered at the real 1-bit size
against the panel palette, since a vector preview in a browser says nothing
about how a glyph reads dithered on e-ink at 24px.

**Done looks like:** every `UIIcon` resolves to a Lucide glyph, the duplicate
size-variant headers are gone or generated, the multi-candidate rows above have
a recorded ruling, and Home / Manage Files / Settings are screenshotted before
and after so the change is judged on the panel rather than in the source.

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
