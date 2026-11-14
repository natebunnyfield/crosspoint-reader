#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <SDCardManager.h>
#if FREEINK_CAP_USB_MSC
#include <UsbMassStorage.h>
#endif

#include <cassert>
#include <new>

#define SDCard SDCardManager::getInstance()

#if FREEINK_CAP_USB_MSC
#if !FREEINK_SD_SDMMC
#error "USB Drive requires an SDMMC-backed storage profile"
#endif
namespace {
freeink::UsbMassStorage usbMassStorage;
}  // namespace
#endif

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  // Recursive so the same task can re-enter StorageLock without self-deadlock.
  // openFileForRead/Write take the lock and then assign to a HalFile&
  // out-param; if that out-param already held an Impl, its destructor takes
  // the lock again to close the prior FsFile under serialization (see
  // HalFile::Impl::~Impl below). Priority inheritance still applies to
  // recursive mutexes.
  storageMutex = xSemaphoreCreateRecursiveMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex); }
};

#if FREEINK_DEVICE_PICCO || FREEINK_CAP_USB_MSC
namespace {
// Write SdFat's cached FAT and directory sectors to the card. Needed before
// anything that ends the volume: FsVolume::end() (SdFat 2.3.1,
// FsLib/FsVolume.h:120-125) only nulls the volume pointers, and the
// partition-level end() is a cacheClear() (FatLib/FatPartition.h:92-95,
// ExFatLib/ExFatPartition.h:85-88), so a dirty cached sector is dropped rather
// than written: a FAT behind its directory entries, the lost-allocation shape
// of B-073 (docs/sd-card-corruption-2026-10-04.md). The SDK's
// SDCardManager::shutdown() says "FsVolume::end() flushes"; it does not.
// FatFile::sync() and ExFatFile::sync() end in the volume's cacheSync()
// (FatLib/FatFile.cpp:1301, ExFatLib/ExFatFileWrite.cpp:376), which writes the
// data and FAT caches and syncs the device, and is a no-op when nothing is
// dirty (common/FsCache.cpp:56-71). The root directory is always there to sync
// through. Caller holds StorageLock.
bool syncSdFatCaches() {
  if (!SDCard.ready()) return true;  // nothing mounted, so nothing cached
  FsFile root = SDCard.open("/", O_RDONLY);
  if (!root) return false;
  const bool ok = root.sync();
  root.close();
  return ok;
}
}  // namespace
#endif

#if FREEINK_DEVICE_PICCO
namespace {
// Write SdFat's cached FAT and directory sectors to the card. Needed before
// anything that ends the volume: FsVolume::end() (SdFat 2.3.1,
// FsLib/FsVolume.h:120-125) only nulls the volume pointers, and the
// partition-level end() is a cacheClear() (FatLib/FatPartition.h:92-95,
// ExFatLib/ExFatPartition.h:85-88), so a dirty cached sector is dropped rather
// than written: a FAT behind its directory entries, the lost-allocation shape
// of B-073 (docs/sd-card-corruption-2026-10-04.md). The SDK's
// SDCardManager::shutdown() says "FsVolume::end() flushes"; it does not.
// FatFile::sync() and ExFatFile::sync() end in the volume's cacheSync()
// (FatLib/FatFile.cpp:1301, ExFatLib/ExFatFileWrite.cpp:376), which writes the
// data and FAT caches and syncs the device, and is a no-op when nothing is
// dirty (common/FsCache.cpp:56-71). The root directory is always there to sync
// through. Caller holds StorageLock.
bool syncSdFatCaches() {
  if (!SDCard.ready()) return true;  // nothing mounted, so nothing cached
  FsFile root = SDCard.open("/", O_RDONLY);
  if (!root) return false;
  const bool ok = root.sync();
  root.close();
  return ok;
}
}  // namespace
#endif

#if FREEINK_DEVICE_PICCO
void HalStorage::prepareForDeepSleep() {
  StorageLock lock;
  // Logged, never blocking: the card loses power either way, and refusing to
  // sleep would only drain the battery.
  if (!syncSdFatCaches()) LOG_ERR("SD", "Sleep: could not flush SdFat's cached sectors before power-down");
  SDCard.shutdown();
}
#endif

#if FREEINK_CAP_USB_MSC
bool HalStorage::refuseWhileUsbDriveOwnsCard(const char* op) const {
  if (!usbDriveOwnsCardFlag.load()) return false;
  LOG_ERR("SD", "Refused %s: the USB Drive host owns the SD card", op);
  return true;
}

bool HalStorage::beginUsbDrive() {
  StorageLock lock;
  if (usbDriveOwnsCardFlag.load()) {
    LOG_ERR("USB", "USB Drive already owns the SD card");
    return false;
  }
  if (!SDCard.ready()) {
    LOG_ERR("USB", "USB Drive requires a mounted SDMMC filesystem");
    return false;
  }
  // Flush BEFORE the detach: detachFilesystemForRawAccess() ends the volume
  // with FsVolume::end(), which drops a dirty cached sector (syncSdFatCaches()
  // above), and the host would mount a FAT that disagrees with its directory
  // entries. A failed flush refuses the handoff with the volume still mounted.
  if (!syncSdFatCaches()) {
    LOG_ERR("USB", "USB Drive refused: could not flush the SD card's cached sectors");
    return false;
  }

  auto* const blockDevice = SDCard.detachFilesystemForRawAccess();
  if (!blockDevice) {
    LOG_ERR("USB", "USB Drive requires a mounted SDMMC filesystem");
    return false;
  }
  // From here the volume is gone, so nothing may reach SdFat until the reboot.
  usbDriveOwnsCardFlag.store(true);

  if (!usbMassStorage.begin(blockDevice)) {
    LOG_ERR("USB", "USB Drive MSC initialization failed");
    // No host ever saw the card (MSC or USB.begin() failed), so the card is
    // exactly as SdFat left it a moment ago and remounting it is safe. This is
    // the only remount-in-place on this path; every exit after a host could
    // have the card is a reboot.
    if (SDCard.begin()) {
      usbDriveOwnsCardFlag.store(false);
    } else {
      LOG_ERR("USB", "Unable to remount SD card after USB Drive startup failure");
    }
    return false;
  }
  return true;
}

bool HalStorage::disconnectUsbDriveHost() {
  StorageLock lock;
  return usbMassStorage.disconnectHost();
}

void HalStorage::endUsbDrive() {
  StorageLock lock;
  // The card stays refused (usbDriveOwnsCardFlag) until the reboot that must
  // follow: the host may have rewritten any sector SdFat knew about.
  usbMassStorage.end();
}

UsbDriveState HalStorage::usbDriveState() const {
  StorageLock lock;
  switch (usbMassStorage.state()) {
    case freeink::UsbMassStorageState::WaitingForHost:
      return UsbDriveState::WaitingForHost;
    case freeink::UsbMassStorageState::Connected:
    case freeink::UsbMassStorageState::Accessed:
      return UsbDriveState::Connected;
    case freeink::UsbMassStorageState::Ejected:
      return UsbDriveState::Ejected;
    case freeink::UsbMassStorageState::Disconnected:
      return UsbDriveState::Disconnected;
    case freeink::UsbMassStorageState::IoError:
      return UsbDriveState::IoError;
    case freeink::UsbMassStorageState::Idle:
      break;
  }
  return UsbDriveState::Unsupported;
}

// Every wrapped call below takes the lock and then refuses while the host owns
// the card, returning the method's value-initialized result (false, 0, an empty
// String or vector, a closed file).
#define HAL_STORAGE_REFUSED(method, ...) \
  if (HalStorage::getInstance().refuseWhileUsbDriveOwnsCard(#method)) return decltype(SDCard.method(__VA_ARGS__))();
#define HAL_FILE_REFUSED(method, ...) \
  if (HalStorage::getInstance().refuseWhileUsbDriveOwnsCard(#method)) return decltype(impl->file.method(__VA_ARGS__))();
#else
#define HAL_STORAGE_REFUSED(method, ...)
#define HAL_FILE_REFUSED(method, ...)
#endif

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  HAL_STORAGE_REFUSED(method, __VA_ARGS__)    \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
  // SdFat is not thread-safe; FsFile::close() touches SD/SPI and must run
  // under StorageLock or it races SdSpiCard::m_spiActive across tasks and
  // trips FreeRTOS's xTaskPriorityDisinherit assert. The FsFile member
  // destructor (DESTRUCTOR_CLOSES_FILE=1) will close() again after the lock
  // releases, but close() on an already-closed FsFile is a no-op. See SdFat
  // issue #518 and the HAL note in CLAUDE.md.
  ~Impl() {
    HalStorage::StorageLock lock;
    file.close();
  }
  FsFile file;
};

// Wrap an SdFat handle without the abort-on-OOM of a bare `new`. Every caller
// holds StorageLock, so when the wrapper cannot be allocated the FsFile is
// closed right here by its destructor (DESTRUCTOR_CLOSES_FILE), still under the
// lock, and the caller gets a closed HalFile. An abort() at this point was a
// reboot with every other open write handle unsynced: clusters allocated in
// the FAT, directory entries still claiming the old sizes.
std::unique_ptr<HalFile::Impl> HalFile::wrap(FsFile&& fsFile) {
  std::unique_ptr<Impl> impl(new (std::nothrow) Impl(std::move(fsFile)));
  if (!impl) LOG_ERR("HAL", "OOM wrapping a file handle");
  return impl;
}

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
#if FREEINK_CAP_USB_MSC
  if (refuseWhileUsbDriveOwnsCard("open")) return HalFile();
#endif
  return HalFile(HalFile::wrap(SDCard.open(path, oflag)));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  // Close whatever the out-param still holds BEFORE opening. Assigning after
  // the open closed the old handle second: reuse a HalFile still open for
  // write on the same path and the O_TRUNC freed the chain, then the old
  // handle's close wrote its stale first cluster back into the live entry --
  // an entry on free clusters, the cross-link pattern. No caller does that
  // today; this makes it impossible rather than merely absent.
  file = HalFile();
#if FREEINK_CAP_USB_MSC
  if (refuseWhileUsbDriveOwnsCard("openFileForRead")) return false;
#endif
  FsFile fsFile;
  bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
  file = HalFile(HalFile::wrap(std::move(fsFile)));
  return ok && file.isOpen();
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  // Close whatever the out-param still holds BEFORE opening. Assigning after
  // the open closed the old handle second: reuse a HalFile still open for
  // write on the same path and the O_TRUNC freed the chain, then the old
  // handle's close wrote its stale first cluster back into the live entry --
  // an entry on free clusters, the cross-link pattern. No caller does that
  // today; this makes it impossible rather than merely absent.
  file = HalFile();
#if FREEINK_CAP_USB_MSC
  if (refuseWhileUsbDriveOwnsCard("openFileForWrite")) return false;
#endif
  FsFile fsFile;
  bool ok = SDCard.openFileForWrite(moduleName, path, fsFile);
  file = HalFile(HalFile::wrap(std::move(fsFile)));
  return ok && file.isOpen();
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  HAL_FILE_REFUSED(method, __VA_ARGS__)    \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }
// NOT a plain pass-through, deliberately. SdFat's getName() returns 0 and may
// leave the buffer UNTOUCHED when it cannot read the name -- a broken long-name
// chain, a stale directory entry, a buffer too small. Sixteen of the seventeen
// call sites in this tree ignore the return value, and every one of them is a
// loop over directory entries reusing one buffer, so "untouched" means the
// PREVIOUS ENTRY'S NAME is what gets used. A listing then shows a duplicate
// where a file should be, which is indistinguishable from the filesystem itself
// being corrupt. Reported by the owner 2026-09-08 as "deleting a file seems to
// corrupt the filenames across the filesystem" (B-054).
//
// Emptying the buffer on failure turns that silent wrong answer into an obvious
// one at every call site at once, without touching sixteen of them. Callers that
// want to distinguish "failed" from "empty name" still have the return value.
size_t HalFile::getName(char* name, size_t len) {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
#if FREEINK_CAP_USB_MSC
  if (HalStorage::getInstance().refuseWhileUsbDriveOwnsCard("getName")) {
    if (name != nullptr && len > 0) name[0] = '\0';
    return 0;
  }
#endif
  const size_t written = impl->file.getName(name, len);
  if (written == 0 && name != nullptr && len > 0) name[0] = '\0';
  return written;
}
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe, no need to wrap
bool HalFile::getCreateDateTime(uint16_t* pdate, uint16_t* ptime) {
  HAL_FILE_WRAPPED_CALL(getCreateDateTime, pdate, ptime);
}
bool HalFile::getModifyDateTime(uint16_t* pdate, uint16_t* ptime) {
  HAL_FILE_WRAPPED_CALL(getModifyDateTime, pdate, ptime);
}
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const uint8_t* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, b); }
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, ); }
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
#if FREEINK_CAP_USB_MSC
  if (HalStorage::getInstance().refuseWhileUsbDriveOwnsCard("openNextFile")) return HalFile();
#endif
  return HalFile(HalFile::wrap(impl->file.openNextFile()));
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
