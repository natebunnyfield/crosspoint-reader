"""
PlatformIO pre-build script: reorder SdFat's remove(), truncate() and the
O_TRUNC branch of openCachedEntry() -- the truncation every openFileForWrite
actually performs -- so the directory entry reaches the card BEFORE the
clusters it points at are freed, and the frees are flushed before returning.

Why. Off ARM (the C3 envs and the S3 `sticky` alike) SdFat is built with one
shared sector cache (USE_SEPARATE_FAT_CACHE=0), and all three free the chain
first:
the FAT sectors the free dirtied are evicted to the card when the directory
sector is loaded, and only then is the entry marked deleted (remove) or given
its new size and first cluster (truncate). A reset in that gap -- a panic on
the other task, a brownout, the battery latch -- leaves a LIVE directory entry
whose clusters are marked free. The next allocation hands those clusters to
another file, and from then on deleting either file frees the other's data:
the cross-links, duplicate entries and overwritten directories read off two
cards on 2026-10-04 (docs/sd-card-corruption-2026-10-04.md, B-073).

Reordered, the same reset leaves the OPPOSITE inconsistency: an entry already
gone (or already shortened) with clusters still marked used. That is lost
space until an fsck, and it is harmless to every other file on the card.

How. Exact-text replacement on the libdep working tree, one function at a
time. Idempotency and drift are decided by the text itself:
  * the patched text is present          -> already applied, skip
  * the original text is present         -> apply
  * neither is present                   -> ABORT THE BUILD. SdFat moved;
    someone has to re-read the function and re-derive the reorder, because a
    build that silently ships the old order is the bug this file exists to
    prevent.
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import sys

# (relative path under the SdFat libdep, original text, patched text)
PATCHES = [
    (
        os.path.join("src", "FatLib", "FatFileLFN.cpp"),
        """  // Cant' remove not open for write.
  if (!isWritable()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Free any clusters.
  if (m_firstCluster && !m_vol->freeChain(m_firstCluster)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Cache directory entry.
  dir = cacheDirEntry(FsCache::CACHE_FOR_WRITE);
  if (!dir) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  checksum = lfnChecksum(dir->name);

  // Mark entry deleted.
  dir->name[0] = FAT_NAME_DELETED;

  // Set this file closed.
  m_attributes = FILE_ATTR_CLOSED;
  m_flags = 0;

  // Write entry to device.
  if (!m_vol->cacheSync()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (!isLFN()) {
""",
        """  // Cant' remove not open for write.
  if (!isWritable()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Cache directory entry.
  dir = cacheDirEntry(FsCache::CACHE_FOR_WRITE);
  if (!dir) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  checksum = lfnChecksum(dir->name);

  // Mark entry deleted.
  dir->name[0] = FAT_NAME_DELETED;

  // Set this file closed.
  m_attributes = FILE_ATTR_CLOSED;
  m_flags = 0;

  // Write entry to device.
  if (!m_vol->cacheSync()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // CrossPoint patch (scripts/patch_sdfat.py): free the clusters only now
  // that the entry is gone from the card, then flush, so a true return means
  // the frees are on the card too. A reset in here leaves lost clusters, not
  // a live entry on free ones.
  if (m_firstCluster && !m_vol->freeChain(m_firstCluster)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (!m_vol->cacheSync()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (!isLFN()) {
""",
    ),
    (
        os.path.join("src", "FatLib", "FatFile.cpp"),
        """bool FatFile::truncate() {
  uint32_t toFree;
  // error if not a normal file or read-only
  if (!isWritable()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (m_firstCluster == 0) {
    return true;
  }
  if (m_curCluster) {
    toFree = 0;
    int8_t fg = m_vol->fatGet(m_curCluster, &toFree);
    if (fg < 0) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (fg) {
      // current cluster is end of chain
      if (!m_vol->fatPutEOC(m_curCluster)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
    }
  } else {
    toFree = m_firstCluster;
    m_firstCluster = 0;
  }
  if (toFree) {
    if (!m_vol->freeChain(toFree)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
  m_fileSize = m_curPosition;

  // need to update directory entry
  m_flags |= FILE_FLAG_DIR_DIRTY;
  return sync();
""",
        """bool FatFile::truncate() {
  uint32_t toFree;
  bool cut = false;
  // error if not a normal file or read-only
  if (!isWritable()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (m_firstCluster == 0) {
    return true;
  }
  // CrossPoint patch (scripts/patch_sdfat.py): the directory entry takes its
  // new size (and, at position 0, an empty chain) and reaches the card BEFORE
  // any cluster is freed. A reset between the two leaves clusters still
  // marked used behind a shorter entry -- lost space -- instead of an entry
  // whose chain the next allocation hands to another file. The end-of-chain
  // mark still goes down whenever the current cluster has a successor entry,
  // including a zero one (a corrupted chain), exactly as before.
  if (m_curCluster) {
    toFree = 0;
    int8_t fg = m_vol->fatGet(m_curCluster, &toFree);
    if (fg < 0) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    cut = fg > 0;
  } else {
    toFree = m_firstCluster;
    m_firstCluster = 0;
  }
  m_fileSize = m_curPosition;

  // need to update directory entry
  m_flags |= FILE_FLAG_DIR_DIRTY;
  if (!sync()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (cut && !m_vol->fatPutEOC(m_curCluster)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (toFree && !m_vol->freeChain(toFree)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (!m_vol->cacheSync()) {
    m_error |= WRITE_ERROR;  // as the original's return sync() reported it
    DBG_FAIL_MACRO;
    goto fail;
  }
  return true;
""",
    ),
    (
        os.path.join("src", "FatLib", "FatFile.cpp"),
        """  if (oflag & O_TRUNC) {
    if (firstCluster && !m_vol->freeChain(firstCluster)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    // need to update directory entry
    m_flags |= FILE_FLAG_DIR_DIRTY;
  } else {
""",
        """  if (oflag & O_TRUNC) {
    // CrossPoint patch (scripts/patch_sdfat.py): THIS is the truncation the
    // firmware performs -- every O_TRUNC open lands here, FatFile::truncate()
    // is never called -- and it freed the chain while the entry on the card
    // still pointed at it. The entry goes first: the memset above left size
    // and first cluster at 0, sync() writes them; the chain is freed and
    // flushed after. A reset between leaves lost clusters, not a live entry
    // on free ones.
    m_flags |= FILE_FLAG_DIR_DIRTY;
    if (!sync()) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (firstCluster && !m_vol->freeChain(firstCluster)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (!m_vol->cacheSync()) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  } else {
""",
    ),
]


def _sdfat_dirs(libdeps_dir):
    for env_dir in sorted(os.listdir(libdeps_dir)):
        sdfat = os.path.join(libdeps_dir, env_dir, "SdFat")
        if os.path.isfile(os.path.join(sdfat, "src", "SdFat.h")):
            yield sdfat


def _version(sdfat):
    props = os.path.join(sdfat, "library.properties")
    try:
        with open(props) as f:
            for line in f:
                if line.startswith("version="):
                    return line.strip().split("=", 1)[1]
    except OSError:
        pass
    return "unknown"


def _apply(sdfat, rel, original, patched):
    path = os.path.join(sdfat, rel)
    with open(path) as f:
        text = f.read()
    if patched in text:
        return False
    if original not in text:
        sys.stderr.write(
            "ERROR: scripts/patch_sdfat.py: %s (SdFat %s) no longer contains the "
            "text the reorder expects. SdFat changed; re-read the function and "
            "re-derive the patch before building -- the unpatched order is the "
            "FAT-corruption seed described in the script header.\n"
            % (path, _version(sdfat))
        )
        raise SystemExit(1)
    tmp = "%s.patching.%d" % (path, os.getpid())
    with open(tmp, "w") as f:
        f.write(text.replace(original, patched, 1))
    os.replace(tmp, path)  # never leave a half-written source for the gate to misread
    return True


def _all_applied(sdfat):
    """Every patched block present in this copy, each exactly once."""
    for rel, _original, patched in PATCHES:
        try:
            with open(os.path.join(sdfat, rel)) as f:
                if f.read().count(patched) != 1:
                    return False
        except OSError:
            return False
    return True


def patch_sdfat(env):
    # A clean does not install dependencies first, so there may be nothing to
    # patch and nothing to compile; the gate below would only get in the way.
    if "clean" in COMMAND_LINE_TARGETS:  # noqa: F821 (SCons-injected global)
        return
    libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
    if os.path.isdir(libdeps_dir):
        for sdfat in _sdfat_dirs(libdeps_dir):
            for rel, original, patched in PATCHES:
                if _apply(sdfat, rel, original, patched):
                    print("Applied SdFat patch: %s (%s)" % (rel, os.path.relpath(sdfat, libdeps_dir)))
    # The gate for the env being built. Every ESP32 env reads the card through
    # SdFat (SDCardManager depends on it), so its copy must be here and must
    # carry every block -- not one marker in one file. If it does not, either
    # the libdeps were not installed before this hook ran or another process
    # was rewriting the copy underneath us, and either way the build would
    # compile the UNPATCHED order silently, which is the one outcome this
    # script exists to make impossible. The native simulator has no SdFat.
    if env.subst("$PIOPLATFORM") == "espressif32":
        own = os.path.join(libdeps_dir, env.subst("$PIOENV"), "SdFat")
        if not _all_applied(own):
            sys.stderr.write(
                "ERROR: scripts/patch_sdfat.py: the SdFat copy for env %s at %s is missing or does not "
                "carry every reordered block. Were the lib_deps installed before the pre: scripts ran, "
                "or is another build rewriting it? Re-run the build; if it persists, delete that copy.\n"
                % (env.subst("$PIOENV"), own)
            )
            raise SystemExit(1)


patch_sdfat(env)  # noqa: F821
