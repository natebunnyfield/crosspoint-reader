#!/usr/bin/env bash
# Read-only check across the four CrossPoint trackers.
#
#   scripts/tracker-check.sh          # report
#   scripts/tracker-check.sh --quiet  # only complain; silent when clean
#
# Why this exists. Work in this project has been lost three separate ways, and
# each time the fix was to write it down somewhere that survives the session:
#
#   * todos "carried in chat, where they survive only as long as the session
#     does" -- TODO.md's own header, which is why that file was created.
#   * the 2026-08-06 simulator audit findings, which "sat in a plan file on a
#     branch that was later deleted" -- crosspoint-simulator/TODO.md's header.
#   * six findings that audit left unrecorded entirely, filed later by 2d60b439.
#   * T-017 and T-018, which on 2026-08-15 existed only in a chat transcript
#     until someone asked whether all remaining work was in one file.
#
# Writing it down created a second problem: FOUR trackers across TWO repos, with
# ids assigned by hand. That produced a duplicate T-009 inside one file and a
# T-001 living in both TODO.md and BUGS.md. This script is the mechanical part
# that stops both -- it reports every open item, refuses to let two items share
# an id, and prints the next free id so nobody has to grep for it.
#
# Exit status: 0 when ids are unique, 1 on any collision. Open counts are
# information, never failure.

set -uo pipefail

QUIET=0
[ "${1:-}" = "--quiet" ] && QUIET=1

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sim="$(cd "$here/../crosspoint-simulator" 2>/dev/null && pwd || true)"

# tracker|label|id-prefix
TRACKERS=(
  "$here/TODO.md|crosspoint-reader/TODO.md|T"
  "$here/BUGS.md|crosspoint-reader/BUGS.md|B"
  "${sim:-/nonexistent}/TODO.md|crosspoint-simulator/TODO.md|ST"
  "${sim:-/nonexistent}/BUGS.md|crosspoint-simulator/BUGS.md|S"
)

say() { [ "$QUIET" -eq 1 ] || printf '%s\n' "$*"; }

# Items in the OPEN section only: from '## OPEN' to the next '## ' heading.
open_ids() {
  awk '/^## OPEN/{f=1;next} /^## /{f=0} f' "$1" 2>/dev/null |
    grep -oE '^### \[[A-Z]+-[0-9]+[a-z]*\]' | grep -oE '\[[A-Z]+-[0-9]+[a-z]*\]'
}

# Every id in the file, any section.
all_ids() {
  grep -oE '^### \[[A-Z]+-[0-9]+[a-z]*\]' "$1" 2>/dev/null |
    grep -oE '\[[A-Z]+-[0-9]+[a-z]*\]'
}

status=0
seen_file=$(mktemp)
trap 'rm -f "$seen_file"' EXIT

say ""
say "Trackers"

total_open=0
for entry in "${TRACKERS[@]}"; do
  IFS='|' read -r path label prefix <<<"$entry"

  if [ ! -f "$path" ]; then
    say "  $label -- MISSING"
    continue
  fi

  n_open=$(open_ids "$path" | wc -l | tr -d ' ')
  total_open=$((total_open + n_open))

  # next free number for this prefix, across the WHOLE file (a reused id from a
  # finished item is still a collision when someone greps for it later)
  highest=$(all_ids "$path" | grep -oE "\[$prefix-[0-9]+" | grep -oE '[0-9]+' |
    sed 's/^0*//' | sort -n | tail -1)
  next=$(printf '%s-%03d' "$prefix" "$(( ${highest:-0} + 1 ))")

  say "  $label -- $n_open open, next free id $next"

  # duplicates inside this file
  while read -r dup; do
    [ -z "$dup" ] && continue
    printf '  DUPLICATE %s appears more than once in %s\n' "$dup" "$label"
    status=1
  done < <(all_ids "$path" | sort | uniq -d)

  all_ids "$path" | sed "s|\$|\t$label|" >>"$seen_file"
done

# same id in two different files
while read -r id; do
  [ -z "$id" ] && continue
  where=$(grep -P "^\Q$id\E\t" "$seen_file" 2>/dev/null | cut -f2 | sort -u | tr '\n' ' ')
  [ -z "$where" ] && where=$(awk -F'\t' -v i="$id" '$1==i{print $2}' "$seen_file" | sort -u | tr '\n' ' ')
  printf '  COLLISION %s is used in: %s\n' "$id" "$where"
  status=1
done < <(cut -f1 "$seen_file" | sort | uniq -d)

say ""
say "  $total_open open across all trackers."
say "  Not tracked here: the upstream backlog (docs/fork-sync.md) and the"
say "  sibling-fork candidates (docs/fork-ecosystem.md)."
say ""

if [ "$status" -ne 0 ]; then
  printf '  Ids must be unique across ALL FOUR trackers, not just within a file.\n'
  printf '  A tracker holds only its own prefix: T- in reader TODO, B- in reader\n'
  printf '  BUGS, ST- in simulator TODO, S- in simulator BUGS.\n\n'
fi

exit "$status"
