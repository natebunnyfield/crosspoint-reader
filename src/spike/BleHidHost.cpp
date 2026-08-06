// SPIKE — throwaway. See BleHidHost.h.
#include "BleHidHost.h"

#include <Arduino.h>
#include <Logging.h>
#include <string.h>

#include <atomic>

#include "HidKeymap.h"

#ifdef SIMULATOR
// The simulator has no BLE radio. HalGPIO owns all SDL polling there (adding
// text input would mean editing the simulator repo), so typed text arrives
// through a file instead: append to fs_/ble-input.txt and it is consumed as
// keystrokes. Same ring, same popChar() contract, so the editor is unchanged
// and the whole flow becomes scriptable.
#include <HalStorage.h>

namespace blespike {
namespace {
constexpr const char* TAG = "BLESPIKE";
constexpr const char* INPUT_PATH = "/ble-input.txt";
State gState = State::Off;
bool gStarted = false;
char gPeerName[32] = "simulated keyboard";
std::string gPending;    // characters read but not yet popped
uint32_t gConsumed = 0;  // bytes of INPUT_PATH already ingested
uint32_t gNotifies = 0;
uint32_t gLastTs = 0;
unsigned long gLastPoll = 0;

// Poll the input file for newly appended bytes. Cheap: one open + seek.
void pollInput() {
  if (millis() - gLastPoll < 200) return;
  gLastPoll = millis();
  HalFile f;
  if (!Storage.openFileForRead(TAG, INPUT_PATH, f)) return;
  const uint32_t size = static_cast<uint32_t>(f.size());
  if (size <= gConsumed) return;
  if (!f.seek(gConsumed)) return;
  char buf[256];
  const int n = f.read(buf, sizeof(buf));
  if (n <= 0) return;
  gPending.append(buf, n);
  gConsumed += n;
  gNotifies++;
  gLastTs = millis();
  LOG_DBG(TAG, "sim input: +%d chars", n);
}
}  // namespace

bool canStart() { return true; }  // no radio to bring up

bool begin() {
  gStarted = true;
  gState = State::Ready;
  LOG_INF(TAG, "SIMULATOR: keystrokes read from %s (append to type)", INPUT_PATH);
  return true;
}

void end() {
  gStarted = false;
  gState = State::Off;
}

State state() { return gState; }
const char* peerName() { return gPeerName; }
uint32_t lastKeyMillis() { return gLastTs; }
uint32_t notifyCount() { return gNotifies; }
uint32_t droppedCount() { return 0; }

int popChar() {
  if (!gStarted) return -1;
  if (gPending.empty()) pollInput();
  if (gPending.empty()) return -1;
  const unsigned char c = static_cast<unsigned char>(gPending.front());
  gPending.erase(gPending.begin());
  return c;
}

const char* stateName() {
  switch (gState) {
    case State::Off:
      return "off";
    case State::Ready:
      return "ready (sim)";
    default:
      return "sim";
  }
}

}  // namespace blespike

#else  // real device below

#include <esp_task_wdt.h>

extern "C" {
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

// store/config/include is not on the framework's exported include path even
// though the symbol is in libbt.a, so declare it rather than include it.
void ble_store_config_init(void);

// THE load-bearing symbol for this whole spike. initArduino() calls
// esp_bt_controller_mem_release(ESP_BT_MODE_BTDM) at boot unless btInUse() is
// true (esp32-hal-misc.c:345); the core's own definition is __attribute__((weak))
// and returns false, and `lib_ignore = BLE` means nothing else overrides it.
// Without this strong definition the controller's RAM is donated to the heap at
// boot and btdm_controller_init() later executes into it: "Guru Meditation
// Error: Core 0 panic'ed (Instruction access fault)", MEPC 0x00000000, RA in
// r_lld_env_init. Observed on the X4, 2026-08-05.
//
// The release is IRREVERSIBLE until reboot, so this is a boot-time decision,
// not a runtime one: a build that can ever start BLE pays the controller's RAM
// for the whole boot session, whether or not the editor is opened.
bool btInUse(void) { return true; }
}

namespace blespike {
namespace {

constexpr const char* TAG = "BLESPIKE";

constexpr uint16_t UUID_HID_SERVICE = 0x1812;
constexpr uint16_t UUID_REPORT = 0x2A4D;
constexpr uint16_t UUID_BOOT_KBD_INPUT = 0x2A22;
constexpr uint16_t UUID_PROTOCOL_MODE = 0x2A4E;
constexpr uint16_t UUID_CCCD = 0x2902;

// The test keyboard (Geonix rev.2) exposes 11 characteristics in 0x0019..0x0044:
// Protocol Mode, six notifiable 0x2A4D reports, one writable 0x2A4D output
// report, Report Map, Boot Keyboard Input, Boot Keyboard Output and Boot Mouse
// Input. MAX_REPORTS=6 silently truncated that set.
constexpr size_t MAX_CHRS = 24;
constexpr size_t MAX_REPORTS = 10;
constexpr size_t RING_SIZE = 128;  // power of two

struct ChrInfo {
  uint16_t defHandle;
  uint16_t valHandle;
  uint16_t uuid16;
  uint8_t properties;
};

struct ReportInfo {
  uint16_t valHandle;
  uint16_t cccdHandle;
  uint16_t endHandle;
};

State gState = State::Off;
bool gStarted = false;
char gPeerName[32] = {0};

uint8_t gOwnAddrType = 0;
ble_addr_t gPeerAddr = {};
bool gHavePeer = false;
uint16_t gConnHandle = BLE_HS_CONN_HANDLE_NONE;

uint16_t gSvcStart = 0;
uint16_t gSvcEnd = 0;
bool gSvcFound = false;

ChrInfo gChrs[MAX_CHRS];
size_t gChrCount = 0;
ReportInfo gReports[MAX_REPORTS];
size_t gReportCount = 0;
size_t gSubIndex = 0;  // which report we are subscribing
bool gDiscoveryRunning = false;

// Single-producer (host task) / single-consumer (main task) ring.
int gRing[RING_SIZE];
uint32_t gRingTs[RING_SIZE];
std::atomic<uint16_t> gHead{0};
std::atomic<uint16_t> gTail{0};
std::atomic<uint32_t> gNotifies{0};
std::atomic<uint32_t> gDropped{0};
uint32_t gLastPoppedTs = 0;

// Last report, so we emit only 0->1 key transitions (HOGP has no key-repeat;
// the host is expected to synthesise it, and we do not want one per report).
uint8_t gPrevKeys[6] = {0};

void setState(State s) {
  gState = s;
  LOG_INF(TAG, "state -> %s", stateName());
}

void ringPush(int c, uint32_t ts) {
  const uint16_t head = gHead.load(std::memory_order_relaxed);
  const uint16_t next = (head + 1) % RING_SIZE;
  if (next == gTail.load(std::memory_order_acquire)) {
    gDropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  gRing[head] = c;
  gRingTs[head] = ts;
  gHead.store(next, std::memory_order_release);
}

void startScan();

// --- GATT: subscribe ---------------------------------------------------------

int onCccdWrite(uint16_t connHandle, const ble_gatt_error* error, ble_gatt_attr* /*attr*/, void* /*arg*/);

void subscribeNext() {
  while (gSubIndex < gReportCount && gReports[gSubIndex].cccdHandle == 0) {
    LOG_DBG(TAG, "report val=0x%04x has no CCCD, skipping", gReports[gSubIndex].valHandle);
    ++gSubIndex;
  }
  if (gSubIndex >= gReportCount) {
    LOG_INF(TAG, "subscribed to %u input report(s)", (unsigned)gReportCount);
    gDiscoveryRunning = false;
    setState(State::Ready);
    return;
  }
  static const uint8_t enableNotify[2] = {0x01, 0x00};
  const int rc = ble_gattc_write_flat(gConnHandle, gReports[gSubIndex].cccdHandle, enableNotify, sizeof(enableNotify),
                                      onCccdWrite, nullptr);
  if (rc != 0) {
    LOG_ERR(TAG, "CCCD write start failed rc=%d", rc);
    ++gSubIndex;
    subscribeNext();
  }
}

int onCccdWrite(uint16_t /*connHandle*/, const ble_gatt_error* error, ble_gatt_attr* /*attr*/, void* /*arg*/) {
  if (error != nullptr && error->status != 0) {
    LOG_ERR(TAG, "CCCD write failed for val=0x%04x status=%d", gReports[gSubIndex].valHandle, error->status);
  } else {
    LOG_INF(TAG, "subscribed: report val=0x%04x cccd=0x%04x", gReports[gSubIndex].valHandle,
            gReports[gSubIndex].cccdHandle);
  }
  ++gSubIndex;
  subscribeNext();
  return 0;
}

// --- GATT: descriptors -------------------------------------------------------

// Descriptor discovery runs ONCE over the whole HID service range, and each
// CCCD is attributed to the report whose value handle most closely precedes it
// — which is exactly the GATT layout rule (a characteristic's descriptors sit
// between its value handle and the next declaration).
//
// The previous version ran one procedure per report over a computed
// [valHandle+1, endHandle] window and got BLE_HS_EINVAL (rc=3) on the first
// call; the remaining reports then silently fell out through the start>end
// guard, so NO CCCD was ever written. Keystrokes still arrived, but only
// because the test keyboard had a CCCD persisted from an earlier bond — a
// never-bonded keyboard would have connected, discovered, and delivered
// nothing. One pass removes the per-report window arithmetic entirely.
int onDsc(uint16_t /*connHandle*/, const ble_gatt_error* error, uint16_t /*chrValHandle*/, const ble_gatt_dsc* dsc,
          void* /*arg*/) {
  if (error != nullptr && error->status == BLE_HS_EDONE) {
    size_t withCccd = 0;
    for (size_t i = 0; i < gReportCount; ++i) {
      if (gReports[i].cccdHandle != 0) ++withCccd;
    }
    LOG_INF(TAG, "descriptors done: %u/%u report(s) have a CCCD", (unsigned)withCccd, (unsigned)gReportCount);
    gSubIndex = 0;
    subscribeNext();
    return 0;
  }
  if (error != nullptr && error->status != 0) {
    LOG_ERR(TAG, "descriptor discovery status=%d — subscribing with what we have", error->status);
    gSubIndex = 0;
    subscribeNext();
    return 0;
  }
  if (dsc == nullptr) return 0;
  if (ble_uuid_u16(&dsc->uuid.u) != UUID_CCCD) return 0;

  // Attach to the nearest preceding report value handle.
  size_t best = gReportCount;
  uint16_t bestHandle = 0;
  for (size_t i = 0; i < gReportCount; ++i) {
    const uint16_t vh = gReports[i].valHandle;
    if (vh < dsc->handle && vh >= bestHandle) {
      bestHandle = vh;
      best = i;
    }
  }
  if (best < gReportCount && gReports[best].cccdHandle == 0) {
    gReports[best].cccdHandle = dsc->handle;
    LOG_DBG(TAG, "CCCD 0x%04x -> report val=0x%04x", dsc->handle, gReports[best].valHandle);
  }
  return 0;
}

void discoverDescriptorsNext() {
  const int rc = ble_gattc_disc_all_dscs(gConnHandle, gSvcStart, gSvcEnd, onDsc, nullptr);
  if (rc != 0) {
    LOG_ERR(TAG, "disc_all_dscs(0x%04x..0x%04x) rc=%d", gSvcStart, gSvcEnd, rc);
    gSubIndex = 0;
    subscribeNext();
  }
}

// --- GATT: characteristics ---------------------------------------------------

void onCharsDone() {
  // Report characteristic ranges end just before the next characteristic
  // declaration (or at the end of the HID service for the last one).
  gReportCount = 0;
  for (size_t i = 0; i < gChrCount && gReportCount < MAX_REPORTS; ++i) {
    const bool isReport = gChrs[i].uuid16 == UUID_REPORT || gChrs[i].uuid16 == UUID_BOOT_KBD_INPUT;
    if (!isReport) continue;
    if ((gChrs[i].properties & BLE_GATT_CHR_PROP_NOTIFY) == 0) continue;
    uint16_t end = gSvcEnd;
    for (size_t j = 0; j < gChrCount; ++j) {
      if (gChrs[j].defHandle > gChrs[i].defHandle && gChrs[j].defHandle - 1 < end) {
        end = gChrs[j].defHandle - 1;
      }
    }
    gReports[gReportCount++] = ReportInfo{gChrs[i].valHandle, 0, end};
  }
  LOG_INF(TAG, "HID service: %u chrs, %u notifiable input report(s)", (unsigned)gChrCount, (unsigned)gReportCount);
  if (gReportCount == 0) {
    LOG_ERR(TAG, "no notifiable input reports — not a keyboard we can read");
    gDiscoveryRunning = false;
    setState(State::Failed);
    return;
  }
  discoverDescriptorsNext();
}

int onChr(uint16_t /*connHandle*/, const ble_gatt_error* error, const ble_gatt_chr* chr, void* /*arg*/) {
  if (error != nullptr && error->status == BLE_HS_EDONE) {
    onCharsDone();
    return 0;
  }
  if (error != nullptr && error->status != 0) {
    LOG_ERR(TAG, "chr discovery status=%d", error->status);
    gDiscoveryRunning = false;
    setState(State::Failed);
    return 0;
  }
  if (chr == nullptr || gChrCount >= MAX_CHRS) return 0;
  const uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);
  gChrs[gChrCount++] = ChrInfo{chr->def_handle, chr->val_handle, uuid16, chr->properties};
  LOG_DBG(TAG, "chr uuid=0x%04x val=0x%04x props=0x%02x", uuid16, chr->val_handle, chr->properties);
  return 0;
}

int onSvc(uint16_t /*connHandle*/, const ble_gatt_error* error, const ble_gatt_svc* svc, void* /*arg*/) {
  if (error != nullptr && error->status == BLE_HS_EDONE) {
    if (!gSvcFound) {
      LOG_ERR(TAG, "peer has no HID service (0x1812)");
      gDiscoveryRunning = false;
      setState(State::Failed);
      return 0;
    }
    gChrCount = 0;
    const int rc = ble_gattc_disc_all_chrs(gConnHandle, gSvcStart, gSvcEnd, onChr, nullptr);
    if (rc != 0) {
      LOG_ERR(TAG, "disc_all_chrs rc=%d", rc);
      gDiscoveryRunning = false;
      setState(State::Failed);
    }
    return 0;
  }
  if (error != nullptr && error->status != 0) {
    LOG_ERR(TAG, "svc discovery status=%d", error->status);
    gDiscoveryRunning = false;
    setState(State::Failed);
    return 0;
  }
  if (svc == nullptr || gSvcFound) return 0;
  gSvcFound = true;
  gSvcStart = svc->start_handle;
  gSvcEnd = svc->end_handle;
  LOG_INF(TAG, "HID service handles 0x%04x..0x%04x", gSvcStart, gSvcEnd);
  return 0;
}

void startDiscovery() {
  // Re-entry guard. security_initiate returning BLE_HS_EALREADY used to drop us
  // straight into discovery, and then ENC_CHANGE started a SECOND one over the
  // same globals: the service was discovered twice, the characteristic
  // callbacks interleaved, and the peer answered the overlapping procedure with
  // "chr discovery status=10". Observed on the X4, 2026-08-05.
  if (gDiscoveryRunning) {
    LOG_DBG(TAG, "discovery already running, ignoring restart");
    return;
  }
  gDiscoveryRunning = true;
  setState(State::Discovering);
  gSvcFound = false;
  gChrCount = 0;
  gReportCount = 0;
  const ble_uuid16_t hidUuid = BLE_UUID16_INIT(UUID_HID_SERVICE);
  const int rc = ble_gattc_disc_svc_by_uuid(gConnHandle, &hidUuid.u, onSvc, nullptr);
  if (rc != 0) {
    LOG_ERR(TAG, "disc_svc_by_uuid rc=%d", rc);
    gDiscoveryRunning = false;
    setState(State::Failed);
  }
}

// --- keystroke decode --------------------------------------------------------

void decodeReport(const uint8_t* rep, size_t len) {
  // Boot-protocol layout: [modifiers][reserved][6 keycodes]. Report-protocol
  // keyboards use the same shape; a 9-byte report carries a leading report ID,
  // so decode from the last 8 bytes. The decode itself lives in HidKeymap.h so
  // it can be unit-tested on the host without a radio (test/ble_keymap).
  if (len < 8) return;
  const uint8_t* report = rep + (len - 8);
  int decoded[6];
  const size_t n = hidkeymap::decodeReport(report, gPrevKeys, decoded, 6);
  const uint32_t now = millis();
  for (size_t i = 0; i < n; ++i) ringPush(decoded[i], now);
}

// --- GAP ---------------------------------------------------------------------

bool advIsHidKeyboard(const ble_hs_adv_fields& fields, const char* name) {
  for (uint8_t i = 0; i < fields.num_uuids16; ++i) {
    if (ble_uuid_u16(&fields.uuids16[i].u) == UUID_HID_SERVICE) return true;
  }
  // Solicited-services list: some peripherals ask the host for HID this way.
  for (uint8_t i = 0; i < fields.sol_num_uuids16; ++i) {
    if (ble_uuid_u16(&fields.sol_uuids16[i].u) == UUID_HID_SERVICE) return true;
  }
  // Some keyboards omit the service UUID and advertise only the appearance.
  if (fields.appearance_is_present && (fields.appearance == 0x03C1 || fields.appearance == 0x03C0)) return true;
  // Last resort for this spike: match the test keyboard by name. A keyboard
  // that exposes HID only in its GATT table (not in the advertisement) is
  // legal HOGP and would otherwise be invisible to a UUID-only scan.
  if (name != nullptr && strstr(name, "eonix") != nullptr) return true;
  return false;
}

// One full payload dump per address, so a keyboard that never matches can still
// be identified from what it actually broadcasts.
constexpr size_t MAX_SEEN = 48;
uint8_t gSeen[MAX_SEEN][6];
size_t gSeenCount = 0;

bool markSeen(const uint8_t* addr) {
  for (size_t i = 0; i < gSeenCount; ++i) {
    if (memcmp(gSeen[i], addr, 6) == 0) return false;
  }
  if (gSeenCount < MAX_SEEN) {
    memcpy(gSeen[gSeenCount++], addr, 6);
  }
  return true;
}

int onGapEvent(ble_gap_event* event, void* /*arg*/) {
  switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
      ble_hs_adv_fields fields;
      if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) return 0;

      char name[32] = {0};
      if (fields.name != nullptr && fields.name_len > 0) {
        const size_t n = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
        memcpy(name, fields.name, n);
      }
      const bool isHid = advIsHidKeyboard(fields, name);
      if (markSeen(event->disc.addr.val)) {
        // First sighting of this address: dump everything about it once.
        char hex[3 * 31 + 1] = {0};
        const uint8_t n = event->disc.length_data > 31 ? 31 : event->disc.length_data;
        for (uint8_t i = 0; i < n; ++i) snprintf(hex + i * 3, 4, "%02x ", event->disc.data[i]);
        char uuids[64] = {0};
        int off = 0;
        for (uint8_t i = 0; i < fields.num_uuids16 && off < (int)sizeof(uuids) - 8; ++i) {
          off += snprintf(uuids + off, sizeof(uuids) - off, "%04x ", ble_uuid_u16(&fields.uuids16[i].u));
        }
        LOG_INF(TAG, "NEW %02x:%02x:%02x:%02x:%02x:%02x type=%u rssi=%d hid=%d name='%s' uuids16=[%s] appear=%04x/%u",
                event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3], event->disc.addr.val[2],
                event->disc.addr.val[1], event->disc.addr.val[0], event->disc.addr.type, event->disc.rssi,
                isHid ? 1 : 0, name, uuids, fields.appearance, fields.appearance_is_present ? 1u : 0u);
        LOG_INF(TAG, "    raw[%u]: %s", (unsigned)event->disc.length_data, hex);
      }
      if (!isHid) return 0;

      LOG_INF(TAG, "HID peripheral found: '%s' rssi=%d", name, event->disc.rssi);
      strncpy(gPeerName, name[0] != 0 ? name : "(unnamed)", sizeof(gPeerName) - 1);
      gPeerAddr = event->disc.addr;
      gHavePeer = true;
      ble_gap_disc_cancel();

      setState(State::Connecting);
      const int rc = ble_gap_connect(gOwnAddrType, &gPeerAddr, 10000, nullptr, onGapEvent, nullptr);
      if (rc != 0) {
        LOG_ERR(TAG, "ble_gap_connect rc=%d", rc);
        startScan();
      }
      return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
      LOG_INF(TAG, "scan complete reason=%d", event->disc_complete.reason);
      if (gState == State::Scanning) startScan();
      return 0;

    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status != 0) {
        LOG_ERR(TAG, "connect failed status=%d", event->connect.status);
        startScan();
        return 0;
      }
      gConnHandle = event->connect.conn_handle;
      LOG_INF(TAG, "connected, handle=%u — starting security", gConnHandle);
      setState(State::Securing);
      {
        const int rc = ble_gap_security_initiate(gConnHandle);
        // BLE_HS_EALREADY means a bond exists / encryption is already under way,
        // which is the normal path on a re-connect. Wait for ENC_CHANGE either
        // way; starting discovery here races the one ENC_CHANGE will start.
        if (rc != 0 && rc != BLE_HS_EALREADY) {
          LOG_ERR(TAG, "security_initiate rc=%d — trying discovery unencrypted", rc);
          startDiscovery();
        } else if (rc == BLE_HS_EALREADY) {
          LOG_INF(TAG, "security already in progress (bonded) — waiting for ENC_CHANGE");
        }
      }
      return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
      LOG_INF(TAG, "encryption change status=%d", event->enc_change.status);
      if (event->enc_change.status == 0) {
        startDiscovery();
      } else {
        LOG_ERR(TAG, "bonding failed — HOGP requires encryption");
        setState(State::Failed);
      }
      return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      // Stale bond on one side. Drop ours and let pairing run again.
      ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
        ble_store_util_delete_peer(&desc.peer_id_addr);
      }
      LOG_INF(TAG, "repeat pairing — old bond deleted");
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_DISCONNECT:
      LOG_INF(TAG, "disconnected reason=%d", event->disconnect.reason);
      gConnHandle = BLE_HS_CONN_HANDLE_NONE;
      gDiscoveryRunning = false;
      memset(gPrevKeys, 0, sizeof(gPrevKeys));
      startScan();
      return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
      uint8_t buf[24];
      uint16_t copied = 0;
      if (ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), &copied) != 0) return 0;
      gNotifies.fetch_add(1, std::memory_order_relaxed);
#if LOG_LEVEL >= 2
      char hex[3 * sizeof(buf) + 1] = {0};
      for (uint16_t i = 0; i < copied && i < sizeof(buf); ++i) snprintf(hex + i * 3, 4, "%02x ", buf[i]);
      LOG_DBG(TAG, "notify h=0x%04x len=%u [%s]", event->notify_rx.attr_handle, (unsigned)copied, hex);
#endif
      decodeReport(buf, copied);
      return 0;
    }

    case BLE_GAP_EVENT_MTU:
      LOG_DBG(TAG, "MTU = %u", event->mtu.value);
      return 0;

    default:
      return 0;
  }
}

void startScan() {
  setState(State::Scanning);
  ble_gap_disc_params params = {};
  params.itvl = 0;
  params.window = 0;
  params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
  params.limited = 0;
  params.passive = 0;  // active: we want the scan response for the name
  params.filter_duplicates = 1;
  const int rc = ble_gap_disc(gOwnAddrType, 30000, &params, onGapEvent, nullptr);
  if (rc != 0) {
    LOG_ERR(TAG, "ble_gap_disc rc=%d", rc);
    setState(State::Failed);
  }
}

void onSync() {
  if (ble_hs_util_ensure_addr(0) != 0) LOG_ERR(TAG, "ensure_addr failed");
  if (ble_hs_id_infer_auto(0, &gOwnAddrType) != 0) {
    LOG_ERR(TAG, "id_infer_auto failed");
    setState(State::Failed);
    return;
  }
  LOG_INF(TAG, "host synced, own addr type=%u", gOwnAddrType);
  startScan();
}

void onReset(int reason) { LOG_ERR(TAG, "host reset, reason=%d", reason); }

void hostTask(void* /*param*/) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

}  // namespace

// Measured on the X4: nimble_port_init() consumed 64,800-65,204 B, and every
// success had maxalloc >= 73,716. The post-exchange restart had maxalloc 24,564
// with free still 62,304 — free looked survivable, the contiguous block was not,
// and it hung. maxalloc is the discriminator.
constexpr uint32_t MIN_MAXALLOC_FOR_INIT = 70000;

bool canStart() { return ESP.getMaxAllocHeap() >= MIN_MAXALLOC_FOR_INIT; }

bool begin() {
  if (gStarted) return true;

  if (!canStart()) {
    // Attempting anyway is a guaranteed hang -> watchdog reboot -> crash
    // report, which is exactly what happened when the editor restarted BLE
    // straight after a Claude exchange. Refuse loudly instead.
    LOG_ERR(TAG, "refusing nimble_port_init: maxalloc=%u < %u (free=%u) — heap too fragmented",
            (unsigned)ESP.getMaxAllocHeap(), (unsigned)MIN_MAXALLOC_FOR_INIT, (unsigned)ESP.getFreeHeap());
    setState(State::Failed);
    return false;
  }

  // maxalloc matters more than free here: the controller wants one large
  // contiguous block, and this call has been seen to hang the device outright
  // when the heap is fragmented rather than merely small.
  LOG_INF(TAG, "heap before nimble_port_init: free=%u maxalloc=%u", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap());

  // nimble_port_init() can HANG outright (not panic, not return an error) —
  // observed repeatedly across the ~85-95 KB free range, including at 94,724
  // after the same call succeeded at 94,972. A hang leaves the device dead with
  // USB still enumerated and only a physical power cycle recovers it, which
  // makes unattended iteration impossible. Arm the task watchdog across the
  // call so a hang becomes an automatic reboot instead: main.cpp replays the
  // persisted panic as SPIKE-PANIC on the next boot, so the event is still
  // visible. This is a spike workaround, NOT a fix — the nondeterminism itself
  // is unexplained and is the biggest open risk in this work.
  const esp_task_wdt_config_t wdtCfg = {
      .timeout_ms = 15000,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  const bool wdtReady = (esp_task_wdt_init(&wdtCfg) == ESP_OK || esp_task_wdt_reconfigure(&wdtCfg) == ESP_OK) &&
                        esp_task_wdt_add(nullptr) == ESP_OK;
  if (!wdtReady) LOG_ERR(TAG, "task WDT not armed; a hang here will need a power cycle");

  const esp_err_t err = nimble_port_init();

  if (wdtReady) {
    esp_task_wdt_reset();
    esp_task_wdt_delete(nullptr);
  }
  if (err != ESP_OK) {
    LOG_ERR(TAG, "nimble_port_init failed: %d", (int)err);
    setState(State::Failed);
    return false;
  }
  LOG_INF(TAG, "heap after nimble_port_init: %u", (unsigned)ESP.getFreeHeap());

  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.reset_cb = onReset;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;  // Just Works
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  ble_store_config_init();

  gStarted = true;
  setState(State::Starting);
  nimble_port_freertos_init(hostTask);
  LOG_INF(TAG, "heap after host task start: %u", (unsigned)ESP.getFreeHeap());
  return true;
}

void end() {
  if (!gStarted) return;
  if (gConnHandle != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(gConnHandle, BLE_ERR_REM_USER_CONN_TERM);
  ble_gap_disc_cancel();
  const int rc = nimble_port_stop();
  if (rc == 0) {
    nimble_port_deinit();
  } else {
    LOG_ERR(TAG, "nimble_port_stop rc=%d", rc);
  }
  gStarted = false;
  gConnHandle = BLE_HS_CONN_HANDLE_NONE;
  gHavePeer = false;
  gState = State::Off;
  LOG_INF(TAG, "heap after nimble_port_deinit: %u", (unsigned)ESP.getFreeHeap());
}

State state() { return gState; }

const char* stateName() {
  switch (gState) {
    case State::Off:
      return "off";
    case State::Starting:
      return "starting";
    case State::Scanning:
      return "scanning";
    case State::Connecting:
      return "connecting";
    case State::Securing:
      return "pairing";
    case State::Discovering:
      return "discovering";
    case State::Ready:
      return "ready";
    case State::Failed:
      return "failed";
  }
  return "?";
}

const char* peerName() { return gPeerName; }

int popChar() {
  const uint16_t tail = gTail.load(std::memory_order_relaxed);
  if (tail == gHead.load(std::memory_order_acquire)) return -1;
  const int c = gRing[tail];
  gLastPoppedTs = gRingTs[tail];
  gTail.store((tail + 1) % RING_SIZE, std::memory_order_release);
  return c;
}

uint32_t lastKeyMillis() { return gLastPoppedTs; }

uint32_t notifyCount() { return gNotifies.load(std::memory_order_relaxed); }

uint32_t droppedCount() { return gDropped.load(std::memory_order_relaxed); }

}  // namespace blespike

#endif  // SIMULATOR
