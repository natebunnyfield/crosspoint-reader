#include "Page.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>

#include <new>

namespace {

template <typename Predicate>
void renderFilteredPageElements(const std::vector<std::shared_ptr<PageElement>>& elements, GfxRenderer& renderer,
                                const int fontId, const int xOffset, const int yOffset, Predicate&& predicate) {
  for (const auto& element : elements) {
    if (predicate(*element)) {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

}  // namespace

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(serialization::BufferedFileWriter& out) {
  serialization::writePod(out, xPos);
  serialization::writePod(out, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(out);
}

std::unique_ptr<PageLine> PageLine::deserialize(HalFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto tb = TextBlock::deserialize(file);
  if (!tb) {
    LOG_ERR("PGE", "Deserialization failed: null TextBlock");
    return nullptr;
  }

  auto* line = new (std::nothrow) PageLine(std::move(tb), xPos, yPos);
  if (!line) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageLine");
    return nullptr;
  }
  return std::unique_ptr<PageLine>(line);
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  // Images don't use fontId or text rendering
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

void PageImage::renderPlaceholder(GfxRenderer& renderer, const int xOffset, const int yOffset) const {
  imageBlock->renderPlaceholder(renderer, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(serialization::BufferedFileWriter& out) {
  serialization::writePod(out, xPos);
  serialization::writePod(out, yPos);

  // serialize ImageBlock
  return imageBlock->serialize(out);
}

std::unique_ptr<PageImage> PageImage::deserialize(HalFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto ib = ImageBlock::deserialize(file);
  // nothrow: this runs while deserializing a cached section, so OOM is a real
  // outcome. Every caller of deserialize() already handles nullptr.
  return std::unique_ptr<PageImage>(new (std::nothrow) PageImage(std::move(ib), xPos, yPos));
}

void PageRotatedText::render(GfxRenderer& renderer, const int pageFontId, const int xOffset, const int yOffset) {
  if (text.empty()) return;
  const int useFont = fontId != 0 ? fontId : pageFontId;
  // CCW is the transform that makes a CLOCKWISE-turned page -- see the note on
  // GfxRenderer::drawTextRotated90CCW. The parser has already wrapped this line
  // and chosen its landing spot, so there is nothing to measure here.
  renderer.drawTextRotated90CCW(useFont, xPos + xOffset, yPos + yOffset, text.c_str(), true,
                                bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

bool PageRotatedText::serialize(serialization::BufferedFileWriter& out) {
  serialization::writePod(out, xPos);
  serialization::writePod(out, yPos);
  const uint8_t boldByte = bold ? 1 : 0;
  serialization::writePod(out, boldByte);
  serialization::writePod(out, fontId);
  serialization::writeString(out, text);
  return true;
}

std::unique_ptr<PageRotatedText> PageRotatedText::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint8_t boldByte = 0;
  int32_t fontId = 0;
  std::string text;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readPod(file, boldByte);
  serialization::readPod(file, fontId);
  serialization::readString(file, text);
  if (text.empty()) {
    LOG_ERR("PGE", "Deserialization failed: empty rotated text");
    return nullptr;
  }
  auto* line = new (std::nothrow) PageRotatedText(std::move(text), boldByte != 0, fontId, xPos, yPos);
  if (!line) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageRotatedText");
    return nullptr;
  }
  return std::unique_ptr<PageRotatedText>(line);
}

void PageVerticalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  (void)fontId;
  if (length == 0 || thickness == 0) return;
  renderer.drawLine(xPos + xOffset, yPos + yOffset, xPos + xOffset, yPos + yOffset + length - 1, thickness, true);
}

bool PageVerticalRule::serialize(serialization::BufferedFileWriter& out) {
  serialization::writePod(out, xPos);
  serialization::writePod(out, yPos);
  serialization::writePod(out, length);
  serialization::writePod(out, thickness);
  return true;
}

std::unique_ptr<PageVerticalRule> PageVerticalRule::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint16_t length = 0;
  uint8_t thickness = 0;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readPod(file, length);
  serialization::readPod(file, thickness);
  if (length == 0 || thickness == 0) {
    LOG_ERR("PGE", "Deserialization failed: invalid vertical rule (len=%u thick=%u)", length, thickness);
    return nullptr;
  }
  auto* rule = new (std::nothrow) PageVerticalRule(length, thickness, xPos, yPos);
  if (!rule) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageVerticalRule");
    return nullptr;
  }
  return std::unique_ptr<PageVerticalRule>(rule);
}

void PageHorizontalRule::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  (void)fontId;
  if (width == 0 || thickness == 0) {
    return;
  }

  renderer.drawLine(xPos + xOffset, yPos + yOffset, xPos + xOffset + width - 1, yPos + yOffset, thickness, true);
}

bool PageHorizontalRule::serialize(serialization::BufferedFileWriter& out) {
  serialization::writePod(out, xPos);
  serialization::writePod(out, yPos);
  serialization::writePod(out, width);
  serialization::writePod(out, thickness);
  return true;
}

std::unique_ptr<PageHorizontalRule> PageHorizontalRule::deserialize(HalFile& file) {
  int16_t xPos = 0;
  int16_t yPos = 0;
  uint16_t width = 0;
  uint8_t thickness = 0;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);
  serialization::readPod(file, width);
  serialization::readPod(file, thickness);

  if (width == 0 || thickness == 0) {
    LOG_ERR("PGE", "Deserialization failed: invalid horizontal rule metadata (width=%u thickness=%u)", width,
            thickness);
    return nullptr;
  }

  auto* rule = new (std::nothrow) PageHorizontalRule(width, thickness, xPos, yPos);
  if (!rule) {
    LOG_ERR("PGE", "Deserialization failed: could not allocate PageHorizontalRule");
    return nullptr;
  }
  return std::unique_ptr<PageHorizontalRule>(rule);
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset, [](const PageElement&) { return true; });
}

void Page::renderImages(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  renderFilteredPageElements(elements, renderer, fontId, xOffset, yOffset,
                             [](const PageElement& element) { return element.getTag() == TAG_PageImage; });
}

void Page::renderWithImagePlaceholders(GfxRenderer& renderer, const int fontId, const int xOffset,
                                       const int yOffset) const {
  for (const auto& element : elements) {
    if (element->getTag() == TAG_PageImage) {
      static_cast<const PageImage&>(*element).renderPlaceholder(renderer, xOffset, yOffset);
    } else {
      element->render(renderer, fontId, xOffset, yOffset);
    }
  }
}

bool Page::serialize(serialization::BufferedFileWriter& out) const {
  const uint16_t count = elements.size();
  serialization::writePod(out, count);

  for (const auto& el : elements) {
    // Use getTag() method to determine type
    serialization::writePod(out, static_cast<uint8_t>(el->getTag()));

    if (!el->serialize(out)) {
      return false;
    }
  }

  // Serialize footnotes (clamp to MAX_FOOTNOTES_PER_PAGE to match addFootnote/deserialize limits)
  const uint16_t fnCount = std::min<uint16_t>(footnotes.size(), MAX_FOOTNOTES_PER_PAGE);
  serialization::writePod(out, fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    const auto& fn = footnotes[i];
    out.write(fn.number, sizeof(fn.number));
    out.write(fn.href, sizeof(fn.href));
  }

  // The writer reports a short write through flush(), which the caller checks
  // once per build rather than per field -- that is the point of buffering.
  return true;
}

std::unique_ptr<Page> Page::deserialize(HalFile& file) {
  auto page = std::unique_ptr<Page>(new (std::nothrow) Page());
  if (!page) {
    LOG_ERR("PGE", "OOM: Page while deserializing a cached section");
    return nullptr;
  }

  uint16_t count;
  serialization::readPod(file, count);

  // Reserve up front so a page load costs one allocation for the element vector
  // instead of a grow-copy-free cycle every doubling. `count` is untrusted (it
  // comes straight off the SD cache), so clamp it: a real page holds a few dozen
  // elements, while a corrupt header could ask for 65535 * sizeof(shared_ptr) and
  // abort() on the failed allocation (vector's operator new is throwing, and this
  // firmware builds with -fno-exceptions). Under-reserving is harmless -- the
  // push_back path below still grows normally.
  static constexpr uint16_t RESERVE_CAP = 256;
  page->elements.reserve(std::min(count, RESERVE_CAP));

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      if (!pl) {
        return nullptr;
      }
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      if (!pi) {
        return nullptr;
      }
      page->elements.push_back(std::move(pi));
    } else if (tag == TAG_PageVerticalRule) {
      auto rule = PageVerticalRule::deserialize(file);
      if (!rule) {
        return nullptr;
      }
      page->elements.push_back(std::move(rule));
    } else if (tag == TAG_PageRotatedText) {
      auto rot = PageRotatedText::deserialize(file);
      if (!rot) {
        return nullptr;
      }
      page->elements.push_back(std::move(rot));
    } else if (tag == TAG_PageHorizontalRule) {
      auto rule = PageHorizontalRule::deserialize(file);
      if (!rule) {
        return nullptr;
      }
      page->elements.push_back(std::move(rule));
    } else {
      LOG_ERR("PGE", "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  // Deserialize footnotes
  uint16_t fnCount;
  serialization::readPod(file, fnCount);
  if (fnCount > MAX_FOOTNOTES_PER_PAGE) {
    LOG_ERR("PGE", "Invalid footnote count %u", fnCount);
    return nullptr;
  }
  page->footnotes.resize(fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    auto& entry = page->footnotes[i];
    if (file.read(entry.number, sizeof(entry.number)) != sizeof(entry.number) ||
        file.read(entry.href, sizeof(entry.href)) != sizeof(entry.href)) {
      LOG_ERR("PGE", "Failed to read footnote %u", i);
      return nullptr;
    }
    entry.number[sizeof(entry.number) - 1] = '\0';
    entry.href[sizeof(entry.href) - 1] = '\0';
  }

  return page;
}
