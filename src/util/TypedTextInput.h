#pragma once
#include <HalGPIO.h>

#include <string>

// Splits one drained chunk of host-typed text (HalGPIO::consumeTypedText) into
// printable runs and editing keys, and hands each to the caller's field.
//
// The chunk is a byte stream: UTF-8 for anything printable, with \b \n \x1b
// standing in for Backspace, OK and Cancel -- the three keys a physical
// keyboard has that the on-screen grid spells as keys. It is NOT one character
// per call: a fast typist, a paired Bluetooth keyboard's repeat, or an iPhone
// keyboard's autocorrect replacing a whole word all arrive as several bytes in
// one frame, and a paste is a whole line.
//
// Runs are inserted whole rather than character by character so a multi-byte
// code point cannot be split across two insert() calls, and so a field that
// enforces a maximum length sees the same string it would have seen from the
// grid.
//
// Lives here, called by both KeyboardEntryActivity and DaisyEntryActivity,
// because the split is the same for every text field and the two activities
// disagree only about how they store the text.
namespace TypedTextInput {

enum class Outcome : uint8_t {
  None,       // nothing in the chunk changed the field
  Changed,    // text was inserted or erased; repaint
  Committed,  // the host pressed Return: finish with the current text
  Cancelled,  // the host pressed Escape: abandon the entry
};

// insert(const std::string&) appends one printable run at the cursor.
// backspace() erases one character before the cursor and returns whether it
// did. Both are called in the order the bytes arrived, so "ab\bc" leaves "ac".
//
// Commit and cancel stop the walk immediately and are reported through the
// return value rather than a callback: they finish the activity, and an
// activity that has called finish() must not keep editing the bytes behind it.
template <typename InsertFn, typename BackspaceFn>
Outcome apply(const std::string& chunk, InsertFn insert, BackspaceFn backspace) {
  Outcome outcome = Outcome::None;
  std::string run;

  auto flush = [&]() {
    if (run.empty()) return;
    insert(run);
    run.clear();
    outcome = Outcome::Changed;
  };

  for (const char c : chunk) {
    switch (c) {
      case HalGPIO::TYPED_BACKSPACE:
        flush();
        if (backspace()) outcome = Outcome::Changed;
        break;
      case HalGPIO::TYPED_COMMIT:
        flush();
        return Outcome::Committed;
      case HalGPIO::TYPED_CANCEL:
        flush();
        return Outcome::Cancelled;
      default:
        run.push_back(c);
        break;
    }
  }
  flush();
  return outcome;
}

}  // namespace TypedTextInput
