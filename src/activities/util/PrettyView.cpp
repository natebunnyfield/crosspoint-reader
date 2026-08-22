#include "PrettyView.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

namespace {

bool endsWithNoCase(const std::string& s, const char* suffix) {
  const size_t n = strlen(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; i++) {
    const char a = s[s.size() - n + i];
    const char b = suffix[i];
    if ((a >= 'A' && a <= 'Z' ? a + 32 : a) != b) return false;
  }
  return true;
}

std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Reads at most `cap` bytes of the file into `out`. Returns false on open/read
// failure or when the file is larger than the cap.
bool readCapped(const std::string& path, const uint32_t cap, std::string& out) {
  HalFile f;
  if (!Storage.openFileForRead("PrettyView", path, f)) return false;
  const uint32_t size = static_cast<uint32_t>(f.fileSize());
  if (size == 0 || size > cap) return false;
  out.resize(size);
  return f.read(&out[0], size) == static_cast<int>(size);
}

// --- JSON: linear re-indenter. No parse, no DOM — any byte sequence flows
// through, so malformed JSON degrades to oddly indented text, never an error.
std::string prettyJson(const std::string& in) {
  std::string out;
  out.reserve(in.size() + in.size() / 2);
  int depth = 0;
  bool inString = false;
  bool escaped = false;

  const auto newlineIndent = [&out, &depth] {
    out += '\n';
    out.append(static_cast<size_t>(depth) * 2, ' ');
  };

  for (size_t i = 0; i < in.size() && out.size() < PrettyView::PRETTY_MAX_OUTPUT; i++) {
    const char c = in[i];
    if (inString) {
      out += c;
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    switch (c) {
      case '"':
        inString = true;
        out += c;
        break;
      case '{':
      case '[': {
        out += c;
        // Keep {} and [] on one line.
        size_t j = i + 1;
        while (j < in.size() && (in[j] == ' ' || in[j] == '\t' || in[j] == '\n' || in[j] == '\r')) j++;
        if (j < in.size() && in[j] == (c == '{' ? '}' : ']')) {
          out += in[j];
          i = j;
          break;
        }
        if (depth < 64) depth++;
        newlineIndent();
        break;
      }
      case '}':
      case ']':
        if (depth > 0) depth--;
        newlineIndent();
        out += c;
        break;
      case ',':
        out += c;
        newlineIndent();
        break;
      case ':':
        out += ": ";
        break;
      case ' ':
      case '\t':
      case '\n':
      case '\r':
        break;  // whitespace outside strings is layout noise
      default:
        out += c;
        break;
    }
  }
  return out;
}

// --- HTML: tag stripper. State machine over bytes; unclosed tags swallow to
// EOF (output may be short or empty — the viewer falls back to raw then).
std::string prettyHtml(const std::string& in) {
  std::string out;
  out.reserve(in.size() / 2);
  bool inTag = false;
  bool inComment = false;
  int skipContent = 0;  // >0 inside <script>/<style>
  std::string tag;
  tag.reserve(16);

  const auto isBlockTag = [](const std::string& t) {
    static const char* kBlocks[] = {"p",       "div",     "br",     "li",     "tr",         "h1", "h2",
                                    "h3",      "h4",      "h5",     "h6",     "ul",         "ol", "table",
                                    "section", "article", "header", "footer", "blockquote", "hr", "title"};
    for (const char* b : kBlocks) {
      if (t == b) return true;
    }
    return false;
  };
  const auto emitNewline = [&out] {
    if (!out.empty() && out.back() != '\n') out += '\n';
  };

  for (size_t i = 0; i < in.size() && out.size() < PrettyView::PRETTY_MAX_OUTPUT; i++) {
    const char c = in[i];
    if (inComment) {
      if (c == '>' && i >= 2 && in[i - 1] == '-' && in[i - 2] == '-') inComment = false;
      continue;
    }
    if (inTag) {
      if (c == '>') {
        inTag = false;
        // Normalize: strip leading '/', trailing attributes already excluded.
        bool closing = !tag.empty() && tag[0] == '/';
        std::string name = closing ? tag.substr(1) : tag;
        if (name == "script" || name == "style") {
          skipContent += closing ? -1 : 1;
          if (skipContent < 0) skipContent = 0;
        }
        if (isBlockTag(name)) emitNewline();
      } else if (tag.size() < 15 && c != ' ') {
        char lc = c >= 'A' && c <= 'Z' ? c + 32 : c;
        tag += lc;
      } else if (c == ' ' && tag.empty()) {
        // "< " is not a tag start; treat literally. Rare; ignore.
      }
      continue;
    }
    if (c == '<') {
      if (in.compare(i, 4, "<!--") == 0) {
        inComment = true;
        i += 3;
        continue;
      }
      inTag = true;
      tag.clear();
      continue;
    }
    if (skipContent > 0) continue;

    // Minimal entity decode.
    if (c == '&') {
      const struct {
        const char* name;
        const char* sub;
      } kEntities[] = {{"&amp;", "&"},   {"&lt;", "<"},   {"&gt;", ">"},
                       {"&quot;", "\""}, {"&apos;", "'"}, {"&nbsp;", " "}};
      bool matched = false;
      for (const auto& e : kEntities) {
        const size_t n = strlen(e.name);
        if (in.compare(i, n, e.name) == 0) {
          out += e.sub;
          i += n - 1;
          matched = true;
          break;
        }
      }
      if (matched) continue;
      out += c;
      continue;
    }
    if (c == '\n' || c == '\r' || c == '\t') {
      if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
      continue;
    }
    if (c == ' ' && !out.empty() && (out.back() == ' ' || out.back() == '\n')) continue;
    out += c;
  }
  return out;
}

// --- EPUB: metadata card via the real Epub loader (reads/creates book.bin).
std::string prettyEpub(const std::string& path) {
  Epub epub(path, "/.crosspoint");
  if (!epub.load(false, true)) return "";
  std::string out;
  out.reserve(256);
  const auto field = [&out](const char* label, const std::string& value) {
    if (value.empty()) return;
    out += label;
    out += ": ";
    out += value;
    out += '\n';
  };
  char buf[48];
  field(tr(STR_META_TITLE), epub.getTitle());
  field(tr(STR_META_AUTHOR), epub.getAuthor());
  field(tr(STR_LANGUAGE), epub.getLanguage());
  snprintf(buf, sizeof(buf), "%s: %d\n", tr(STR_META_CHAPTERS), epub.getTocItemsCount());
  out += buf;
  snprintf(buf, sizeof(buf), "%s: %d\n", tr(STR_META_SPINE_ITEMS), epub.getSpineItemsCount());
  out += buf;
  field(tr(STR_META_COVER), Storage.exists(epub.getCoverBmpPath().c_str()) ? tr(STR_YES) : tr(STR_NO));
  return out;
}

// --- .crosspoint binary summaries. Bounds-checked buffer reads only; any
// truncated/odd file yields a shorter card, never an out-of-range read.
uint32_t rdU32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
uint16_t rdU16(const uint8_t* p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

// Length-prefixed string at `off`; advances `off` past it. Empty on overrun.
std::string readLpString(const std::string& data, size_t& off) {
  if (off + 4 > data.size()) {
    off = data.size();
    return "";
  }
  const uint32_t len = rdU32(reinterpret_cast<const uint8_t*>(data.data()) + off);
  off += 4;
  if (len > 65535 || off + len > data.size()) {
    off = data.size();
    return "";
  }
  std::string s = data.substr(off, len);
  off += len;
  return s;
}

std::string prettyBookBin(const std::string& data) {
  // Layout (docs/file-formats.md v10): u8 version, u32 lutOffset,
  // u16 spineCount, u16 tocCount, then 5 length-prefixed metadata strings.
  if (data.size() < 9) return "";
  const auto* p = reinterpret_cast<const uint8_t*>(data.data());
  char buf[64];
  std::string out;
  out.reserve(256);
  snprintf(buf, sizeof(buf), "book.bin v%u\n", p[0]);
  out += buf;
  snprintf(buf, sizeof(buf), "%s: %u\n", tr(STR_META_SPINE_ITEMS), rdU16(p + 5));
  out += buf;
  snprintf(buf, sizeof(buf), "%s: %u\n", tr(STR_META_CHAPTERS), rdU16(p + 7));
  out += buf;
  size_t off = 9;
  const std::string title = readLpString(data, off);
  const std::string author = readLpString(data, off);
  const std::string language = readLpString(data, off);
  const std::string cover = readLpString(data, off);
  if (!title.empty()) out += std::string(tr(STR_META_TITLE)) + ": " + title + '\n';
  if (!author.empty()) out += std::string(tr(STR_META_AUTHOR)) + ": " + author + '\n';
  if (!language.empty()) out += std::string(tr(STR_LANGUAGE)) + ": " + language + '\n';
  if (!cover.empty()) out += std::string(tr(STR_META_COVER)) + ": " + cover + '\n';
  return out;
}

std::string prettyProgressBin(const std::string& data) {
  // No version byte; LENGTH discriminates (EpubReaderUtils.h): 4 = spine+page,
  // 6 adds chapter page count, 8 adds paragraph index, 12 adds word anchor.
  const auto* p = reinterpret_cast<const uint8_t*>(data.data());
  if (data.size() < 4) return "";
  char buf[48];
  std::string out;
  out.reserve(128);
  snprintf(buf, sizeof(buf), "progress.bin (%u B)\n", static_cast<unsigned>(data.size()));
  out += buf;
  snprintf(buf, sizeof(buf), "spine: %u\n", rdU16(p));
  out += buf;
  snprintf(buf, sizeof(buf), "page: %u\n", rdU16(p + 2));
  out += buf;
  if (data.size() >= 6) {
    snprintf(buf, sizeof(buf), "chapter pages: %u\n", rdU16(p + 4));
    out += buf;
  }
  if (data.size() >= 8) {
    snprintf(buf, sizeof(buf), "paragraph: %u\n", rdU16(p + 6));
    out += buf;
  }
  if (data.size() >= 12) {
    snprintf(buf, sizeof(buf), "word anchor: %u\n", rdU32(p + 8));
    out += buf;
  }
  return out;
}

std::string prettySectionBin(const std::string& data) {
  // Header layout (docs/file-formats.md): u8 version, s32 fontId, f32
  // lineCompression, u8 extraParagraphSpacing, u8 paragraphAlignment,
  // u16 viewportW, u16 viewportH, u8 hyphenation, u8 embeddedStyle,
  // u8 imageRendering, u8 focusReading, u8 lineGrid (v39+), u16 pageCount.
  if (data.size() < 22) return "";
  const auto* p = reinterpret_cast<const uint8_t*>(data.data());
  char buf[48];
  std::string out;
  out.reserve(128);
  snprintf(buf, sizeof(buf), "section.bin v%u\n", p[0]);
  out += buf;
  // v39 grew the header by the lineGrid byte, shifting pageCount from 19 to 20.
  snprintf(buf, sizeof(buf), "pages: %u\n", rdU16(p + (p[0] >= 39 ? 20 : 19)));
  out += buf;
  snprintf(buf, sizeof(buf), "viewport: %ux%u\n", rdU16(p + 11), rdU16(p + 13));
  out += buf;
  int32_t fontId;
  memcpy(&fontId, p + 1, 4);
  snprintf(buf, sizeof(buf), "font id: %d\n", static_cast<int>(fontId));
  out += buf;
  return out;
}

std::string prettyCpBin(const std::string& path) {
  const std::string name = baseName(path);
  std::string data;
  // Progress/headers are small; book.bin metadata sits before the LUTs so a
  // 16KB cap covers real files, and a larger one just reads capped-out empty.
  if (!readCapped(path, name == "book.bin" ? 64 * 1024 : 16 * 1024, data)) {
    // Big book.bin: read just the header + metadata region instead.
    HalFile f;
    if (!Storage.openFileForRead("PrettyView", path, f)) return "";
    data.resize(16 * 1024);
    const int n = f.read(&data[0], data.size());
    if (n <= 0) return "";
    data.resize(n);
  }
  if (name == "book.bin") return prettyBookBin(data);
  if (name == "progress.bin") return prettyProgressBin(data);
  return prettySectionBin(data);
}

}  // namespace

namespace PrettyView {

Kind detect(const std::string& fullPath, const uint32_t fileSize) {
  if (endsWithNoCase(fullPath, ".bmp")) return Kind::ImageBmp;
  if (endsWithNoCase(fullPath, ".png") || endsWithNoCase(fullPath, ".jpg") || endsWithNoCase(fullPath, ".jpeg"))
    return Kind::ImageDecoded;
  if (FsHelpers::hasEpubExtension(fullPath)) return Kind::Epub;
  if (endsWithNoCase(fullPath, ".json")) return fileSize <= PRETTY_MAX_SOURCE ? Kind::Json : Kind::None;
  if (endsWithNoCase(fullPath, ".html") || endsWithNoCase(fullPath, ".htm") || endsWithNoCase(fullPath, ".xhtml"))
    return fileSize <= PRETTY_MAX_SOURCE ? Kind::Html : Kind::None;
  const std::string name = baseName(fullPath);
  if (name == "book.bin" || name == "progress.bin") return Kind::CpBin;
  if (endsWithNoCase(fullPath, ".bin") && fullPath.find("/sections/") != std::string::npos) return Kind::CpBin;
  return Kind::None;
}

std::string generate(const Kind kind, const std::string& fullPath) {
  switch (kind) {
    case Kind::Json: {
      std::string in;
      if (!readCapped(fullPath, PRETTY_MAX_SOURCE, in)) return "";
      return prettyJson(in);
    }
    case Kind::Html: {
      std::string in;
      if (!readCapped(fullPath, PRETTY_MAX_SOURCE, in)) return "";
      return prettyHtml(in);
    }
    case Kind::Epub:
      return prettyEpub(fullPath);
    case Kind::CpBin:
      return prettyCpBin(fullPath);
    case Kind::ImageBmp:
    case Kind::ImageDecoded:
    case Kind::None:
      return "";  // images render directly; None has no pretty view
  }
  return "";
}

}  // namespace PrettyView
