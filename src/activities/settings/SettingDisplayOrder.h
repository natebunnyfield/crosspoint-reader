#pragma once

// Display order for a settings picker, kept separate from the value it stores.
//
// An ENUM setting persists the row INDEX, so its value list is append-only: a
// new choice goes on the end whatever it means, because moving one would
// re-point every saved settings.json at something else. Three pickers ended up
// ordered by when each choice was added rather than by anything a reader would
// recognize -- "0 ms" after "1000 ms" in Typing Redraw Delay, "None" wedged
// between "Cover Custom" and "Quick Resume" in Sleep Screen, and the two editor
// fonts that need no SD card sitting below three that do nothing until one is
// installed.
//
// A display order is a permutation: display position -> stored index. The
// stored index never moves, so the on-screen order can be fixed at any time
// with no migration.
//
// Free functions in their own header, with no Arduino, renderer, I18n or
// settings-singleton dependency, so the logic is directly testable -- see
// test/settings_display_order/. SettingInfo wraps these; nothing else should
// need them.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace settingorder {

// Every index in 0..count-1 exactly once. Rejects duplicates and out-of-range
// entries, either of which would make some choice unreachable in the picker.
inline bool isPermutation(const std::vector<uint8_t>& order, size_t count) {
  if (order.size() != count) return false;
  std::vector<bool> seen(count, false);
  for (const uint8_t v : order) {
    if (v >= count || seen[v]) return false;
    seen[v] = true;
  }
  return true;
}

// Distinct, in-range entries, any length from 1 to count. What a DELIBERATE
// withdrawal of choices produces -- the sleep-screen trim of 2026-08-21 lists
// 9 of 12 values on purpose. Never inferred: a subset order is only honored
// where the row passed subset=true, so an accidentally short table elsewhere
// still degrades to identity instead of silently hiding a choice.
inline bool isSubsetPermutation(const std::vector<uint8_t>& order, size_t count) {
  if (order.empty() || order.size() > count) return false;
  std::vector<bool> seen(count, false);
  for (const uint8_t v : order) {
    if (v >= count || seen[v]) return false;
    seen[v] = true;
  }
  return true;
}

// `order` when it is usable, identity otherwise.
//
// A stale table -- someone appends a choice and does not extend the order --
// degrades to identity rather than dropping the new choice off the end of the
// picker. Losing the ordering is cosmetic; losing a choice means an owner
// cannot select a value the firmware supports, which is far worse. Rows that
// WANT to lose choices say so explicitly with subset=true.
inline std::vector<uint8_t> resolve(const std::vector<uint8_t>& order, size_t count, bool subset = false) {
  if (subset ? isSubsetPermutation(order, count) : isPermutation(order, count)) return order;
  std::vector<uint8_t> identity(count);
  for (size_t i = 0; i < count; i++) identity[i] = static_cast<uint8_t>(i);
  return identity;
}

// Display position showing `stored`, or 0 when it is not listed. A stored value
// CAN be past the end of the list (a font uninstalled, an enum shortened), the
// same case the row renderer bounds-checks for.
inline size_t positionOf(const std::vector<uint8_t>& order, size_t count, uint8_t stored, bool subset = false) {
  const std::vector<uint8_t> resolved = resolve(order, count, subset);
  for (size_t i = 0; i < resolved.size(); i++) {
    if (resolved[i] == stored) return i;
  }
  return 0;
}

// `values` rearranged into display order.
template <typename T>
std::vector<T> reorder(const std::vector<uint8_t>& order, const std::vector<T>& values, bool subset = false) {
  const std::vector<uint8_t> resolved = resolve(order, values.size(), subset);
  std::vector<T> out;
  out.reserve(resolved.size());
  for (const uint8_t stored : resolved) out.push_back(values[stored]);
  return out;
}

}  // namespace settingorder
