// A scriptable stand-in for the one HttpDownloader entry point
// fetchUrlToBuffer is built on. See CMakeLists.txt.
#pragma once

#include <cstddef>
#include <string>

namespace fakehttp {

struct Script {
  std::string body;
  // What the server DECLARES as Content-Length. Set to 0 to model a chunked
  // response that declares nothing -- the case fetchUrlToBuffer has to grow
  // into rather than reserve for.
  size_t declaredLength = 0;
  // Hand the body over in pieces this size, the way a 1 KB socket read does.
  size_t chunk = 1024;
  // Stop after this many bytes and report a transport failure (0 = never).
  size_t failAfter = 0;
  // Hand over one zero-length chunk before the body. No real transport does
  // this; the guard it exercises is against a convention, not against a caller.
  bool emitOneEmptyChunk = false;

  // Observed, not scripted.
  size_t sizeAnnouncements = 0;  // times the declared length was offered
  size_t dataCallbacks = 0;      // times a body chunk was handed over
};

Script& script();
void reset();

}  // namespace fakehttp
