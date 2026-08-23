#pragma once

// Which SD families the reader offers — ONE question, asked by every consumer.
//
// There are two of those: Text Settings (FontSelectionActivity) and the in-book
// cycle on a held side button (EpubReaderActivity::cycleReaderFontFamily). They
// must offer the same set, and on 2026-08-11 they did not: the picker filtered
// writing-only editor faces and the cycle walked the raw registry, so holding a
// side button selected fonts Text Settings would not list. The comment above the
// cycle claimed the two matched; a comment cannot notice when it stops being
// true.
//
// So the rule lives here instead of being spelled out twice. Adding a new reason
// to withhold a family means editing this file, and both consumers get it.
//
// NOTHING IS HIDDEN THAT A READER CHOSE. Every family withheld here is one the
// owner ruled out of the reading set; an unlisted or user-installed font still
// appears, exactly as FontDisplayNames documents.

namespace readingfonts {

// Retired: A-tier'd out of the shipped set by owner ruling. The recipes stay
// buildable in sd-fonts.yaml and the picker labels stay in FontDisplayNames, so
// the face is not deleted from the project — but a card provisioned BEFORE the
// ruling still carries the .cpfont files, and the registry lists whatever is on
// the card. Without this the retirement only reached new cards.
bool isRetired(const char* family);

// The question. False for writing-only editor faces (see notes/EditorFonts.h)
// and for retired families; true for everything else, including families this
// project has never heard of.
bool offeredForReading(const char* family);

// ...and the ORDER, which is the other half of "the same list" and was the half
// still wrong after 2026-08-11 fixed the set. Text Settings sorts reverse
// chronologically by lineage year, ties by display name; the in-book cycle
// walked raw registry order, so a shake or a held side button stepped through
// the same families in a different sequence than the picker shows them. Owner
// 2026-08-23: "make sure font change order always follows Text Settings order."
//
// True when `a` sorts BEFORE `b`, i.e. usable directly as a std::sort
// comparator on family directory names. Both consumers call this rather than
// spelling the rule out, for the reason the file exists.
bool sortsBefore(const char* a, const char* b);

}  // namespace readingfonts
