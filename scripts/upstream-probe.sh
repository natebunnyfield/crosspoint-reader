#!/usr/bin/env bash
# Probe which upstream commits still apply to this fork. Read docs/fork-sync.md first.
#
#   git worktree add --detach /tmp/probe origin/main
#   scripts/upstream-probe.sh /tmp/probe <last-upstream-sha-already-triaged> upstream/develop out.tsv
#   git worktree remove --force /tmp/probe
#
# It COMMITS probe picks in the worktree you give it, so that later commits see
# earlier ones. Give it a throwaway detached worktree, never a working copy.
# It takes nothing: the result is a map, and "clean" means "applies", not "builds".
#
# Per non-merge commit in upstream order, one TSV row:
#   sha  date  patch-id(present|absent)  result  files  missing  other-lang  sdk  subject  live-conflict-files  missing-paths
# result:
#   clean            applies as is
#   clean-live-part  conflicted only in paths this fork does not have; the rest applies
#   dead-only        touches only paths this fork does not have
#   empty            nothing left to apply (already here)
#   empty-sdkonly    only moves the freeink-sdk pointer (the SDK is a fork branch here)
#   conflict         conflicts in files this fork has (listed in column 10)
# "missing" counts modified files absent at the fork head; "other-lang" counts
# translation files other than english.yaml and spanish.yaml.
# Never probe with -X ours (docs/fork-sync.md): it reports conflicts as clean.
set -u
WT=$1; FROM=$2; TIP=$3; OUT=$4
cd "$WT" || exit 1
FORK=$(git rev-parse HEAD)
CH=$(mktemp); git cherry "$FORK" "$TIP" > "$CH"
: > "$OUT"
for sha in $(git rev-list --reverse --no-merges "$FROM..$TIP"); do
  s=$(git log -1 --format='%h' "$sha"); d=$(git log -1 --format='%ad' --date=short "$sha"); subj=$(git log -1 --format='%s' "$sha" | tr '\t' ' ')
  pid=$(grep -q "^- $sha" "$CH" && echo present || echo absent)
  files=$(git show --pretty=format: --name-status "$sha" | grep -v '^$')
  nfiles=$(printf '%s\n' "$files" | grep -c .)
  sdk=$(printf '%s\n' "$files" | grep -c 'freeink-sdk$')
  # modified/deleted files that do not exist in the fork
  missing=0; i18n=0; missing_list=""
  while IFS=$'\t' read -r st p p2; do
    [ -z "$st" ] && continue
    case "$p" in freeink-sdk) continue;; esac
    case "$p" in lib/I18n/translations/*) case "$p" in */english.yaml|*/spanish.yaml) ;; *) i18n=$((i18n+1)); continue;; esac;; esac
    case "$st" in A*) continue;; esac
    if ! git cat-file -e "$FORK:$p" 2>/dev/null; then missing=$((missing+1)); missing_list="$missing_list $p"; fi
  done <<< "$files"
  res=""; cf=""
  if git cherry-pick -x --no-commit "$sha" >/dev/null 2>&1; then
    git reset -q HEAD -- freeink-sdk 2>/dev/null
    if git diff --cached --quiet; then res=empty; git reset -q --hard HEAD
    else git commit -q --no-verify -m "probe $s" >/dev/null 2>&1; res=clean; fi
  else
    cfl=$(git diff --name-only --diff-filter=U | grep -v '^freeink-sdk$')
    if [ -z "$cfl" ]; then
      # only the submodule pointer conflicted: keep ours
      git reset -q HEAD -- freeink-sdk 2>/dev/null; git checkout -q HEAD -- freeink-sdk 2>/dev/null
      if git diff --cached --quiet; then res=empty-sdkonly; git cherry-pick --abort 2>/dev/null; git reset -q --hard HEAD
      else git commit -q --no-verify -m "probe $s" >/dev/null 2>&1 && res=clean-sdkdropped || { res=conflict; git cherry-pick --abort 2>/dev/null; git reset -q --hard HEAD; }
      fi
    else
      # drop conflicted paths the fork does not have (deleted subsystems, other languages)
      live=""
      while IFS= read -r p; do
        [ -z "$p" ] && continue
        if git cat-file -e "HEAD:$p" 2>/dev/null; then live="$live $p"; else git rm -q -f --cached -- "$p" 2>/dev/null; rm -f -- "$p"; fi
      done <<< "$cfl"
      git reset -q HEAD -- freeink-sdk 2>/dev/null
      if [ -z "$live" ]; then
        if git diff --cached --quiet; then res=dead-only; git cherry-pick --abort 2>/dev/null; git reset -q --hard HEAD
        else git commit -q --no-verify -m "probe $s" >/dev/null 2>&1 && res=clean-live-part || { res=conflict; git cherry-pick --abort 2>/dev/null; git reset -q --hard HEAD; }
        fi
      else
        res=conflict; cf="$live"
        git cherry-pick --abort 2>/dev/null; git reset -q --hard HEAD
      fi
    fi
  fi
  git clean -fdq 2>/dev/null
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$s" "$d" "$pid" "$res" "$nfiles" "$missing" "$i18n" "$sdk" "$subj" "$cf" "$missing_list" >> "$OUT"
done
rm -f "$CH"
