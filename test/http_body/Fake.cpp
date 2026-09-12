#include "Fake.h"

#include "HttpDownloader.h"

namespace fakehttp {

Script& script() {
  static Script s;
  return s;
}

void reset() { script() = Script{}; }

}  // namespace fakehttp

// The real one lives in HttpDownloader.cpp, which is not compiled here. This
// definition satisfies the link and drives fetchUrlToBuffer exactly the way the
// wolfSSL path does: announce the declared length once, then deliver the body
// in fixed-size pieces, stopping when a callback returns false.
HttpDownloader::DownloadError HttpDownloader::fetchUrlWithHeaders(const std::string&, const HeaderList&,
                                                                  const DataCallback& onData,
                                                                  const SizeCallback& onSize) {
  auto& s = fakehttp::script();

  if (onSize) {
    s.sizeAnnouncements++;
    if (!onSize(s.declaredLength)) return FILE_ERROR;
  }

  if (s.emitOneEmptyChunk) {
    s.dataCallbacks++;
    if (!onData(reinterpret_cast<const uint8_t*>(""), 0)) return FILE_ERROR;
  }

  size_t sent = 0;
  while (sent < s.body.size()) {
    if (s.failAfter && sent >= s.failAfter) return HTTP_ERROR;
    const size_t len = std::min(s.chunk, s.body.size() - sent);
    s.dataCallbacks++;
    if (!onData(reinterpret_cast<const uint8_t*>(s.body.data() + sent), len)) return FILE_ERROR;
    sent += len;
  }
  return OK;
}
