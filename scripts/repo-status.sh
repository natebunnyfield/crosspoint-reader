#!/usr/bin/env bash
# Read-only status across the CrossPoint working copies.
#
#   scripts/repo-status.sh              # fetch, then report
#   scripts/repo-status.sh --no-fetch   # report from what is already local
#
# Deliberately reports and never merges. This fork has removed whole subsystems
# that upstream still develops (see docs/fork-sync.md), so `git merge
# upstream/develop` resurrects deleted code and must stay a human decision.
# Exit status is 0 unless a repo is missing; "behind upstream" is information,
# not failure.
set -uo pipefail

FETCH=1
[ "${1:-}" = "--no-fetch" ] && FETCH=0

# Paths this fork has deleted on purpose. An upstream commit touching ONLY
# these is Not Applicable here — see docs/fork-sync.md for the removal commits.
REMOVED_PREFIXES="lib/KOReaderSync/ src/util/BookmarkFile src/BookmarkEntry.h \
src/activities/reader/EpubReaderBookmarksActivity src/activities/reader/KOReaderSyncActivity \
src/activities/settings/TextSettingsPreview lib/Calibre/ src/activities/reader/ReaderMenu"

REPOS="$HOME/src/crosspoint-reader $HOME/src/crosspoint-simulator"

# Is every path in this commit under a removed prefix?
commit_is_na() {
  local repo=$1 sha=$2 path
  local files
  files=$(git -C "$repo" show --pretty=format: --name-only "$sha" | grep -v '^$')
  [ -z "$files" ] && return 1
  while IFS= read -r path; do
    local hit=0
    for pre in $REMOVED_PREFIXES; do
      case "$path" in "$pre"*) hit=1; break;; esac
    done
    [ "$hit" -eq 0 ] && return 1
  done <<< "$files"
  return 0
}

rc=0
for repo in $REPOS; do
  name=$(basename "$repo")
  if [ ! -d "$repo/.git" ]; then
    printf '%s\n  MISSING (no git repo at %s)\n\n' "$name" "$repo"
    rc=1
    continue
  fi

  [ "$FETCH" -eq 1 ] && git -C "$repo" fetch --all --quiet 2>/dev/null

  branch=$(git -C "$repo" branch --show-current)
  dirty=$(git -C "$repo" status --porcelain | wc -l | tr -d ' ')
  printf '%s  [%s]\n' "$name" "$branch"

  if [ "$dirty" -gt 0 ]; then
    printf '  UNCOMMITTED: %s file(s) -- commit or stash before consolidating anything\n' "$dirty"
    git -C "$repo" status --porcelain | sed 's/^/      /'
  fi

  # Local branches that exist nowhere on a remote: the shape that lost work.
  unpushed=""
  while IFS= read -r b; do
    [ -z "$b" ] && continue
    git -C "$repo" branch -r --contains "$b" 2>/dev/null | grep -q . || unpushed="$unpushed $b"
  done <<< "$(git -C "$repo" for-each-ref --format='%(refname:short)' refs/heads/)"
  [ -n "$unpushed" ] && printf '  NOT ON ANY REMOTE:%s\n' "$unpushed"

  for remote in origin upstream; do
    git -C "$repo" remote get-url "$remote" >/dev/null 2>&1 || continue
    head=$(git -C "$repo" symbolic-ref -q "refs/remotes/$remote/HEAD" 2>/dev/null | sed "s@refs/remotes/@@")
    [ -z "$head" ] && head="$remote/$branch"
    git -C "$repo" rev-parse --verify --quiet "$head" >/dev/null || continue
    ahead=$(git -C "$repo" rev-list --count "$head..$branch" 2>/dev/null || echo ?)
    behind=$(git -C "$repo" rev-list --count "$branch..$head" 2>/dev/null || echo ?)
    printf '  vs %-8s %s ahead, %s behind\n' "$remote" "$ahead" "$behind"

    # Classify what we are behind upstream by, so "behind" is actionable.
    if [ "$remote" = upstream ] && [ "$behind" != 0 ] && [ "$behind" != "?" ]; then
      while IFS= read -r sha; do
        [ -z "$sha" ] && continue
        subj=$(git -C "$repo" log -1 --format='%h %s' "$sha")
        if commit_is_na "$repo" "$sha"; then
          printf '      N/A  %s\n' "$subj"
        else
          printf '      REVIEW %s\n' "$subj"
        fi
      done <<< "$(git -C "$repo" rev-list "$branch..$head")"
      printf '      (N/A = touches only subsystems this fork removed; see docs/fork-sync.md)\n'
    fi
  done

  # A submodule left uninitialised builds against the wrong SDK silently.
  if [ -f "$repo/.gitmodules" ]; then
    git -C "$repo" submodule status | while IFS= read -r line; do
      case "$line" in
        -*) printf '  SUBMODULE UNINITIALISED: %s -- run: git -C %s submodule update --init --recursive\n' "$line" "$repo" ;;
        +*) printf '  SUBMODULE DRIFTED from the pinned commit: %s\n' "$line" ;;
         *) printf '  submodule ok: %s\n' "$(echo "$line" | awk '{print $2" "$3}')" ;;
      esac
    done
  fi
  echo
done

# What is still owed, across all four trackers in both repos. Reported here
# because this is the script people actually run, and work that lives only in a
# chat transcript is work that gets lost (see tracker-check.sh for the three
# times that happened). A duplicate id fails; open counts never do.
if [ -x "$(dirname "$0")/tracker-check.sh" ]; then
  "$(dirname "$0")/tracker-check.sh" || rc=1
fi

exit "$rc"
