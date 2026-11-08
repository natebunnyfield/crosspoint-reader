#include <HalGPIO.h>
#include <Logging.h>
#include <PowerManager.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <XteinkDetect.h>
#include <esp_sleep.h>
#include <soc/usb_serial_jtag_struct.h>

#if FREEINK_DEVICE_PICCO
#include <BatteryMonitor.h>
#endif

// Global HalGPIO instance
HalGPIO gpio;

namespace X3GPIO {

bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *outValue = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

bool readBQ27220CurrentMA(int16_t* outCurrent) {
  uint16_t raw = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_CUR_REG, &raw)) {
    return false;
  }
  *outCurrent = static_cast<int16_t>(raw);
  return true;
}

}  // namespace X3GPIO

namespace {
constexpr char HW_NAMESPACE[] = "cphw";
constexpr char NVS_KEY_DEV_OVERRIDE[] = "dev_ovr";  // 0=auto, 1=x4, 2=x3
constexpr char NVS_KEY_DEV_CACHED[] = "dev_det";    // 0=unknown, 1=x4, 2=x3

enum class NvsDeviceValue : uint8_t { Unknown = 0, X4 = 1, X3 = 2 };

NvsDeviceValue readNvsDeviceValue(const char* key, NvsDeviceValue defaultValue) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) {
    return defaultValue;
  }
  const uint8_t raw = prefs.getUChar(key, static_cast<uint8_t>(defaultValue));
  prefs.end();
  if (raw > static_cast<uint8_t>(NvsDeviceValue::X3)) {
    return defaultValue;
  }
  return static_cast<NvsDeviceValue>(raw);
}

void writeNvsDeviceValue(const char* key, NvsDeviceValue value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) {
    return;
  }
  prefs.putUChar(key, static_cast<uint8_t>(value));
  prefs.end();
}

HalGPIO::DeviceType nvsToDeviceType(NvsDeviceValue value) {
  return value == NvsDeviceValue::X3 ? HalGPIO::DeviceType::X3 : HalGPIO::DeviceType::X4;
}

HalGPIO::DeviceType detectDeviceTypeWithFingerprint() {
  // Explicit override for recovery/support:
  // 0 = auto, 1 = force X4, 2 = force X3
  const NvsDeviceValue overrideValue = readNvsDeviceValue(NVS_KEY_DEV_OVERRIDE, NvsDeviceValue::Unknown);
  if (overrideValue == NvsDeviceValue::X3 || overrideValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Device override active: %s", overrideValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(overrideValue);
  }

  const NvsDeviceValue cachedValue = readNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::Unknown);
  if (cachedValue == NvsDeviceValue::X3 || cachedValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Using cached device type: %s", cachedValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(cachedValue);
  }

  // No cache yet: use FreeInk's canonical two-pass X3 fingerprint and persist
  // only confirmed results. Inconclusive probes deliberately remain uncached.
  uint8_t score1 = 0;
  uint8_t score2 = 0;
  const freeink::XteinkVerdict verdict = freeink::detectXteinkVerdict(&score1, &score2);
  LOG_INF("HW", "Xteink probe scores: pass1=%u pass2=%u verdict=%u", score1, score2, static_cast<unsigned>(verdict));

  if (verdict == freeink::XteinkVerdict::X3Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X3);
    return HalGPIO::DeviceType::X3;
  }

  if (verdict == freeink::XteinkVerdict::X4Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X4);
    return HalGPIO::DeviceType::X4;
  }

  // Conservative fallback for first boot with inconclusive probes.
  return HalGPIO::DeviceType::X4;
}

}  // namespace

void HalGPIO::begin() {
#if FREEINK_MCU_C3
  // Deselect the SD card before ANYTHING clocks the shared SCLK/MOSI. The card
  // is still powered after a warm reset (ESP.restart, a USB-powered deep sleep)
  // and still in SPI mode, so with its CS low it parses whatever crosses the
  // bus as commands -- and the panel probe below bit-bangs that bus on every
  // boot, before Storage.begin() ever drives this pin. A display byte stream
  // that happens to form CMD24 + 0xFE writes 512 bytes of it to some sector.
  // GPIO12 is the SD CS on both C3 boards (XTEINK_X4/X3 sd.cs).
  static_assert(BoardConfig::XTEINK_X4.sd.cs == BoardConfig::XTEINK_X3.sd.cs, "X3/X4 SD CS diverged");
  pinMode(BoardConfig::XTEINK_X4.sd.cs, OUTPUT);
  digitalWrite(BoardConfig::XTEINK_X4.sd.cs, HIGH);

  _deviceType = detectDeviceTypeWithFingerprint();
  BoardConfig::selectDevice(deviceIsX3() ? BoardConfig::Board::XteinkX3 : BoardConfig::Board::XteinkX4);

  // Resolve the per-batch controller before SPI owns the display pins. FreeInk
  // checks the OEM hw_calib/screenType value first, then falls back to its
  // two-pass display-bus probe. X3's facade keys panel selection off the sibling
  // board profile, so preserve a detected UC8279 through setDisplayX3().
  freeink::applyXteinkDisplayController();
  if (deviceIsX3() && BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
  }

  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);

  if (deviceIsX4()) {
    pinMode(BAT_GPIO0, INPUT);
    pinMode(UART0_RXD, INPUT);
  }
#else
  _deviceType = DeviceType::X4;
#endif
  inputMgr.begin();
}

void HalGPIO::update() {
  inputMgr.update();
#if FREEINK_DEVICE_PICCO
  // A synthesized press lasts one frame: it releases here (injectButtonTap).
  synthReleased = synthHeld;
  synthHeld = 0;
  synthPressed = 0;
#endif
  updateUsbState(millis());
}

void HalGPIO::updateUsbState(const unsigned long now) {
  // SOF-based host-link sampling (see the member comment). A cheap register
  // read, so it runs at its own short cadence on both devices and is never
  // behind the I2C throttle below — a fresh enumeration must cancel light
  // sleep within a poll or two, or the next slice kills the CDC link again.
  if (sofLastSampleMs == 0 || now - sofLastSampleMs >= SOF_SAMPLE_MS) {
    const auto sof = static_cast<uint16_t>(USB_SERIAL_JTAG.fram_num.sof_frame_index);
    usbSofActive = (sof != lastSofFrameIndex);
    lastSofFrameIndex = sof;
    sofLastSampleMs = now;
  }

  // Throttle the X3's I2C-based USB detection; see USB_POLL_X3_MS. First call
  // (usbLastPollMs == 0) always polls so boot state is correct. The combined
  // verdict below is still recomputed every call so a SOF-detected attach is
  // not held back by the throttle window. The Picco's check is an I2C read of
  // its SGM41562 charger, so it takes the same 1 s throttle.
#if FREEINK_DEVICE_PICCO
  const bool i2cUsbDetect = true;
#else
  const bool i2cUsbDetect = deviceIsX3();
#endif
  if (usbLastPollMs == 0 || !i2cUsbDetect || now - usbLastPollMs >= USB_POLL_X3_MS) {
    usbLastPollMs = now;
    usbElectricalConnected = isUsbElectricalConnected();
  }
  const bool connected = usbSofActive || usbElectricalConnected;
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

void HalGPIO::pollUsbState() {
  // Wait out the SOF sample floor so the comparison sees a real frame delta:
  // two reads inside one USB frame compare equal and read as "no host".
  const unsigned long elapsed = millis() - sofLastSampleMs;
  if (sofLastSampleMs != 0 && elapsed < SOF_SAMPLE_MS) {
    delay(SOF_SAMPLE_MS - elapsed);
  }
  updateUsbState(millis());
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

#if FREEINK_DEVICE_PICCO
namespace {
bool synthBit(const uint8_t mask, const uint8_t buttonIndex) { return buttonIndex < 8 && ((mask >> buttonIndex) & 1u); }
}  // namespace
#endif

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
#if FREEINK_DEVICE_PICCO
  if (synthBit(synthHeld, buttonIndex)) return true;
#endif
  return inputMgr.isPressed(buttonIndex);
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
#if FREEINK_DEVICE_PICCO
  if (synthBit(synthPressed, buttonIndex)) return true;
#endif
  return inputMgr.wasPressed(buttonIndex);
}

bool HalGPIO::wasAnyPressed() const {
#if FREEINK_DEVICE_PICCO
  if (synthPressed != 0) return true;
#endif
  return inputMgr.wasAnyPressed();
}

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
#if FREEINK_DEVICE_PICCO
  if (synthBit(synthReleased, buttonIndex)) return true;
#endif
  return inputMgr.wasReleased(buttonIndex);
}

bool HalGPIO::wasAnyReleased() const {
#if FREEINK_DEVICE_PICCO
  if (synthReleased != 0) return true;
#endif
  return inputMgr.wasAnyReleased();
}

bool HalGPIO::isDebouncePending() const { return inputMgr.isDebouncePending(); }

unsigned long HalGPIO::getHeldTime() const {
#if FREEINK_DEVICE_PICCO
  // A synthesized tap is a short press, whatever the last real button's hold
  // was: without this its release frame would report the SDK's stale global
  // duration and could read as a long press.
  if ((synthHeld | synthReleased) != 0) return millis() - synthPressedAt;
#endif
  return inputMgr.getHeldTime();
}

unsigned long HalGPIO::getPowerButtonHeldTime() const { return inputMgr.getPowerButtonHeldTime(); }

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

// Picco: each touch query below reads as "no touch" unless a keyboard has
// switched touch on (setKeyboardTouch(), HalGPIO.h). The gate sits here, in the
// one bridge from the SDK's touch API to the firmware, so no touch consumer can
// be written that bypasses it. Every other board compiles these unchanged.
bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
#if FREEINK_DEVICE_PICCO
  if (!keyboardTouch) return false;
#endif
  return inputMgr.wasTouchTap(nx, ny);
}

bool HalGPIO::wasTouchDown(float& nx, float& ny) const {
#if FREEINK_DEVICE_PICCO
  if (!keyboardTouch) return false;
#endif
  return inputMgr.wasTouchPressedAt(nx, ny);
}

bool HalGPIO::wasTouchReleased() const {
#if FREEINK_DEVICE_PICCO
  if (!keyboardTouch) return false;
#endif
  return inputMgr.wasTouchReleased();
}

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
#if FREEINK_DEVICE_PICCO
  if (!keyboardTouch) return false;
#endif
  return inputMgr.isTouchTapCandidate(nx, ny, heldMs);
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const {
#if FREEINK_DEVICE_PICCO
  if (!keyboardTouch) return false;
#endif
  return inputMgr.isTouchHeldAt(nx, ny);
}

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const {
#if FREEINK_DEVICE_PICCO
  if (!keyboardTouch) return false;
#endif
  return inputMgr.wasTouchLongPress(nx, ny);
}

void HalGPIO::suppressTouchContact() { inputMgr.suppressTouchContact(); }

unsigned long HalGPIO::lastTouchHeldMs() const {
#if FREEINK_DEVICE_PICCO
  if (!keyboardTouch) return 0;
#endif
  return inputMgr.lastTouchHeldMs();
}

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
#if FREEINK_DEVICE_PICCO
  // Never a swipe here on the Picco, keyboard or not. That keeps the Home and
  // menu swipes, SwipeDir and every swipe-paged list inert
  // (MappedInputManager::decodeSwipe). Gestures reach activities only as
  // synthesized buttons, through readTouchContact()/injectButtonTap() below.
  (void)nxStart;
  (void)nyStart;
  (void)nxEnd;
  (void)nyEnd;
  return false;
#else
  return inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
#endif
}

bool HalGPIO::wasTouchActivity() const {
#if FREEINK_DEVICE_PICCO
  if (!keyboardTouch) return false;
#endif
  return inputMgr.wasTouchActivity();
}

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(const bool enabled) {
  InputManager::setSharedConfirmPowerShortPressEmitsPower(enabled);
}

#if FREEINK_DEVICE_PICCO
bool HalGPIO::hasToggleSwitch() const { return inputMgr.hasToggleSwitch(); }

bool HalGPIO::isToggleSwitchOn() const { return inputMgr.isToggleSwitchOn(); }

void HalGPIO::prepareForDeepSleep() {
  if (!inputMgr.prepareForDeepSleep()) LOG_ERR("GPIO", "Failed to put touch controller to sleep");
}

bool HalGPIO::readTouchContact(float& nx, float& ny, bool& live, unsigned long& reportMs) const {
  // Deliberately NOT gated on keyboardTouch: gestures are buttons on every
  // screen. isTouchHeldAt() reads false for a contact suppressTouchContact()
  // latched, which is what `live` reports.
  if (!inputMgr.isTouchPressed()) return false;
  live = inputMgr.isTouchHeldAt(nx, ny);
  // Stamped by the SDK on every contact report it reads (InputManager.cpp:1261),
  // and left alone when no new report arrived: the decoder tells a finger
  // still reporting from one whose lift-off was lost by this alone.
  reportMs = inputMgr.getTouchPoint().timestamp;
  return true;
}

void HalGPIO::injectButtonTap(const uint8_t index) {
  if (index >= 8) return;
  const auto bit = static_cast<uint8_t>(1u << index);
  synthPressed |= bit;
  synthHeld |= bit;
  synthPressedAt = millis();
}

bool HalGPIO::isTouchContactDown() const { return inputMgr.isTouchPressed(); }

unsigned long HalGPIO::lastTouchReportMs() const { return inputMgr.getTouchPoint().timestamp; }

void HalGPIO::setKeyboardTouch(const bool on) {
  if (on == keyboardTouch) return;
  keyboardTouch = on;
  // A contact already down when the gate moves belongs to whatever was on
  // screen before; the SDK clears the latch itself once that finger lifts.
  inputMgr.suppressTouchContact();
}

bool HalGPIO::isUsbPowerPresent() const { return isUsbElectricalConnected(); }
#endif

bool HalGPIO::hasEdgeSideButtons() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Pro;
}

bool HalGPIO::isXteinkDevice() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4;
}

bool HalGPIO::verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed) {
  // Boards without a power button (or M5Paper's latch circuit) cannot verify a
  // hold; treat the wake as valid.
  if (BoardConfig::ACTIVE.input.power < 0) {
    return true;
  }
#if defined(FREEINK_DEVICE_M5PAPER) && FREEINK_DEVICE_M5PAPER
  return true;
#endif
  if (shortPressAllowed) {
    // Fast path - no duration check needed
    return true;
  }
  // TODO: Intermittent edge case remains: a single tap followed by another single tap
  // can still power on the device. Tighten wake debounce/state handling here.

  // Calibrate: subtract boot time already elapsed, assuming button held since boot.
  const unsigned long calibration = millis();
  const unsigned long calibratedDuration = (calibration < requiredDurationMs) ? (requiredDurationMs - calibration) : 1;

  const auto start = millis();
  inputMgr.update();
  // inputMgr.isPressed() may take up to ~500ms to return correct state
  while (!inputMgr.isPressed(BTN_POWER) && millis() - start < 1000) {
    delay(10);
    inputMgr.update();
  }
  if (inputMgr.isPressed(BTN_POWER)) {
    do {
      delay(10);
      inputMgr.update();
    } while (inputMgr.isPressed(BTN_POWER) && inputMgr.getPowerButtonHeldTime() < calibratedDuration);
    if (inputMgr.getPowerButtonHeldTime() < calibratedDuration) {
      return false;
    }
  } else {
    return false;
  }
  return true;
}

bool HalGPIO::isUsbConnected() const {
  // Recent SOF activity means an enumerated host regardless of what the
  // electrical check says (false at boot until update() has sampled twice).
  return usbSofActive || isUsbElectricalConnected();
}

bool HalGPIO::isUsbElectricalConnected() const {
  if (deviceIsX3()) {
    // X3: infer USB/charging via BQ27220 Current() register (0x0C, signed mA).
    // Positive current means charging. Misses a data-only cable and a full
    // battery — the SOF check in update() covers those.
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
      int16_t currentMa = 0;
      if (X3GPIO::readBQ27220CurrentMA(&currentMa)) {
        return currentMa > 0;
      }
      delay(2);
    }
    return false;
  }
#if FREEINK_DEVICE_PICCO
  {
    // Picco: no USB-detect pin. The SGM41562 charger reports its input rail
    // directly; when it cannot answer, infer from charging, where charge
    // termination at 100% reads as "not connected" (upstream 261726f61).
    static const BatteryMonitor battery;
    bool known = false;
    const bool present = battery.isExternalPowerPresent(&known);
    if (known) return present;
    return battery.isCharging();
  }
#endif
  if (BoardConfig::ACTIVE.usbDetect < 0) {
    return false;
  }
  return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
