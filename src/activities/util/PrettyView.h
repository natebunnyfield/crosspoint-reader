#pragma once

#include <cstdint>
#include <string>

// Pretty-mode support for TextViewerActivity (docs/manage-files.md v2 plan).
//
// Non-image kinds generate an in-memory text document that the viewer paginates
// exactly like a file. Generation is a linear scan with no validation pass, so
// malformed input degrades to odd-looking text, never a crash; an empty result
// means "no pretty view" and the viewer stays on raw. Json/Html are capped at
// PRETTY_MAX_SOURCE bytes — larger files simply stay raw-only (the transform
// holds the whole document in RAM, which the 380KB budget cannot fit for
// arbitrary files).
namespace PrettyView {

enum class Kind : uint8_t { None, Json, Html, ImageBmp, ImageDecoded, Epub, CpBin };

// Json/Html transforms hold source + output in RAM; keep both small.
constexpr uint32_t PRETTY_MAX_SOURCE = 32 * 1024;
constexpr size_t PRETTY_MAX_OUTPUT = 96 * 1024;

// entryName: the display name (file name); fullPath: absolute SD path.
Kind detect(const std::string& fullPath, uint32_t fileSize);

// Returns the generated pretty document; empty = unavailable, stay raw.
std::string generate(Kind kind, const std::string& fullPath);

}  // namespace PrettyView
