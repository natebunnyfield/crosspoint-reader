#pragma once
// HttpDownloader.h reaches for HalStorage.h only to have the name `Stream` in
// scope for the fetchUrl overload that writes into one. This suite links no
// transport and no filesystem, so the name is all it needs -- the same trick
// test/font_commit/stubs/HalStorage.h plays at line 35, minus the filesystem.
class Stream;
