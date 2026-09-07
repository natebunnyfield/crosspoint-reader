// SdCardFontSystem stand-in: FontUpdater::finishRun() calls markRegistryDirty()
// and nothing else on it. The real facade owns the registry, the manager and a
// GfxRenderer, none of which this suite has or needs.
#pragma once

class SdCardFontSystem {
 public:
  void markRegistryDirty() { dirtyCount++; }
  int dirtyCount = 0;
};

extern SdCardFontSystem sdFontSystem;
