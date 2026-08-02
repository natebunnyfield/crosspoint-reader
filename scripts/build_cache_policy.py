"""
PlatformIO build script: keep `build_cache_dir` from growing without bound.

Registered as BOTH `pre:` and `post:` in platformio.ini -- the two policies below
need different phases. See the comment in main() for why; the short version is
that PROGNAME is not resolved yet during `pre:`, so naming the link artifact is
impossible there, while pruning has to happen before anything compiles.

The SCons CacheDir behind `build_cache_dir` (platformio.ini `[platformio]`) is
content-addressed and has **no eviction of any kind** -- every distinct
(source, command line, toolchain) hash is stored forever. Measured on this repo
2026-08-01: 11 GB across 19,781 entries, all of it written in the ten days since
the cache was created. Two things drive that:

  * Nine `[env:]` blocks, several built routinely (`default`, `simulator`,
    `simulator_x3`). A C3 object and a native arm64 object for the same
    translation unit are different hashes -- nothing is shared between envs.
  * `[base] build_flags` is a ~40-define block reaching every translation unit
    via `${base.build_flags}`. Flip one define and all ~150 TUs miss, recompile,
    and land under fresh hashes while the previous generation stays on disk.

This script applies two policies:

  1. **Never cache link artifacts.** 63 entries accounted for 3.2 GB of the
     11 GB -- each a 51-57 MB unstripped `.elf` with debug_info, one per link.
     Re-linking is cheap next to a full recompile, so storing these buys almost
     nothing per gigabyte. `env.NoCache()` tags them (SCons 4.8.1,
     SCons/Environment.py:2305); they still build normally, they just are not
     written to or read from the cache.

  2. **Cap total size, evicting least-recently-used first.** APFS updates atime
     on read here (verified), so atime is a true LRU signal: an entry the build
     keeps hitting keeps its recency, and only genuinely cold generations are
     dropped. Override the cap with CROSSPOINT_BUILD_CACHE_MAX_MB; 0 disables
     pruning entirely.

Only the two-hex-character shard directories are ever touched. The cache root
also holds SCons's own `config` ({"prefix_len": 2}) and the `.sconsign*.dblite`
signature databases -- deleting those would corrupt the cache, so they are
skipped explicitly.

Pruning runs before compilation, so no cache write is ever in flight. A failure
here is never allowed to fail the build: worst case the cache stays large.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import json
import os
import re
import sys


DEFAULT_MAX_MB = 3072
SHARD_RE = None  # built once prefix_len is known


def warn(msg):
    print(f'WARNING [build_cache_policy.py]: {msg}', file=sys.stderr)


def info(msg):
    print(f'[build_cache_policy.py] {msg}')


def read_prefix_len(cache_dir):
    """SCons records its shard-name length in <cache>/config."""
    try:
        with open(os.path.join(cache_dir, 'config'), 'r') as fh:
            return int(json.load(fh).get('prefix_len', 2))
    except (OSError, ValueError, TypeError):
        return 2


def collect_entries(cache_dir, prefix_len):
    """(atime, size, path) for every cached object, shard dirs only.

    The cache root's `config` and `.sconsign*.dblite` are SCons metadata and are
    never returned, so they can never be evicted.
    """
    shard_re = re.compile(r'^[0-9A-Fa-f]{%d}$' % prefix_len)
    entries = []
    total = 0
    try:
        shards = os.scandir(cache_dir)
    except OSError as e:
        warn(f'cannot read cache dir {cache_dir}: {e}')
        return [], 0
    with shards:
        for shard in shards:
            if not shard.is_dir(follow_symlinks=False):
                continue
            if not shard_re.match(shard.name):
                continue
            try:
                with os.scandir(shard.path) as items:
                    for item in items:
                        if not item.is_file(follow_symlinks=False):
                            continue
                        try:
                            st = item.stat(follow_symlinks=False)
                        except OSError:
                            continue
                        entries.append((st.st_atime, st.st_size, item.path))
                        total += st.st_size
            except OSError as e:
                warn(f'skipping shard {shard.name}: {e}')
    return entries, total


def prune(cache_dir, max_bytes):
    prefix_len = read_prefix_len(cache_dir)
    entries, total = collect_entries(cache_dir, prefix_len)
    if not entries or total <= max_bytes:
        return
    # Oldest access first: the generations no recent build has touched.
    entries.sort(key=lambda e: e[0])
    freed = 0
    removed = 0
    target = total - max_bytes
    for _atime, size, path in entries:
        if freed >= target:
            break
        try:
            os.remove(path)
        except OSError:
            continue
        freed += size
        removed += 1
    info(
        f'pruned {removed} entries, {freed / 1024 ** 3:.2f} GB '
        f'({total / 1024 ** 3:.2f} GB -> {(total - freed) / 1024 ** 3:.2f} GB, '
        f'cap {max_bytes / 1024 ** 3:.2f} GB)'
    )


def no_cache_link_artifacts():
    """Keep the 50 MB+ unstripped executables out of the cache."""
    targets = []
    platform = env.subst('$PIOPLATFORM')  # noqa: F821
    if platform == 'native':
        # Simulator envs link a bare executable (.pio/build/<env>/program).
        targets.append('$BUILD_DIR/${PROGNAME}')
    else:
        targets.append('$BUILD_DIR/${PROGNAME}.elf')
    for target in targets:
        try:
            env.NoCache(target)  # noqa: F821
        except Exception as e:  # noqa: BLE001 - never fail the build over this
            warn(f'NoCache({target}) failed: {e}')


def main():
    cache_dir = env.subst('$BUILD_CACHE_DIR')  # noqa: F821
    if not cache_dir:
        return  # build_cache_dir not configured; nothing to police
    if not os.path.isabs(cache_dir):
        cache_dir = os.path.join(env.subst('$PROJECT_DIR'), cache_dir)  # noqa: F821

    # This file is registered as BOTH `pre:` and `post:` because the two
    # policies need different phases, and PlatformIO runs both before SCons
    # executes the build:
    #   pre:  PROGNAME is still SCons's default ("program"), so the .elf path
    #         cannot be named yet -- but pruning must happen before compiling,
    #         which is the whole point on a disk that is already full.
    #   post: the platform builder has set PROGNAME (espressif32 -> "firmware"),
    #         so NoCache can finally tag the real node.
    # The marker is per-env so a multi-env `pio run` still prunes once per env.
    # Name-sniffing the phase does not work: native/simulator envs genuinely
    # link a binary called "program".
    marker = '_CROSSPOINT_CACHE_POLICY_%s' % env.subst('$PIOENV')  # noqa: F821
    if os.environ.get(marker):
        no_cache_link_artifacts()  # post: phase
        return
    os.environ[marker] = '1'

    try:
        max_mb = int(os.environ.get('CROSSPOINT_BUILD_CACHE_MAX_MB', DEFAULT_MAX_MB))
    except ValueError:
        warn('CROSSPOINT_BUILD_CACHE_MAX_MB is not an integer; using default')
        max_mb = DEFAULT_MAX_MB
    if max_mb <= 0:
        return  # pruning explicitly disabled
    if not os.path.isdir(cache_dir):
        return  # first build; PlatformIO creates it after this script runs

    prune(cache_dir, max_mb * 1024 * 1024)


try:
    main()
except Exception as e:  # noqa: BLE001 - a cache policy must never break a build
    warn(f'skipped: {e}')
