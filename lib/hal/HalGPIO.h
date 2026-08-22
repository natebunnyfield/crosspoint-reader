#pragma once

#include <Arduino.h>
#include <InputManager.h>

#include <string>

// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define SPI_MISO 7  // SPI MISO, shared between SD card and display (Master In Slave Out)

#define BAT_GPIO0 0  // Battery voltage

#define UART0_RXD 20  // Used for USB connection detection

// Xteink X3 Hardware
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#define X3_I2C_FREQ 400000

// TI BQ27220 Fuel gauge I2C
#define I2C_ADDR_BQ27220 0x55  // Fuel gauge I2C address
#define BQ27220_SOC_REG 0x2C   // StateOfCharge() command code (%)
#define BQ27220_CUR_REG 0x0C   // Current() command code (signed mA)
#define BQ27220_VOLT_REG 0x08  // Voltage() command code (mV)

// Analog DS3231 RTC I2C
#define I2C_ADDR_DS3231 0x68  // RTC I2C address
#define DS3231_SEC_REG 0x00   // Seconds command code (BCD)

// QST QMI8658 IMU I2C
#define I2C_ADDR_QMI8658 0x6B        // IMU I2C address
#define I2C_ADDR_QMI8658_ALT 0x6A    // IMU I2C fallback address
#define QMI8658_WHO_AM_I_REG 0x00    // WHO_AM_I command code
#define QMI8658_WHO_AM_I_VALUE 0x05  // WHO_AM_I expected value

// One word's rect in the read-aloud capture (see the channel note inside the
// class). Namespace scope, not a member, because the simulator defines the
// identical POD at namespace scope in its src/ReadAloudChannel.h and the
// capture code in the reader references it unqualified — the two definitions
// must stay field-identical. Coordinates are logical portrait panel pixels;
// byteOffset/byteLen index the published UTF-8 page text; a word the layout
// wrapped across lines publishes one rect per visual fragment, all sharing
// the word's range.
struct ReadAloudWordRect {
  uint16_t x, y, w, h;
  uint32_t byteOffset;
  uint16_t byteLen;
};

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

  bool lastUsbConnected = false;
  bool usbStateChanged = false;
  unsigned long usbLastPollMs = 0;
  bool usbElectricalConnected = false;  // last result of the per-device electrical/charge check

  // X3 USB detection is a BQ27220 I2C read (~0.3-1 ms of awake CPU per call);
  // polled every loop it costs a few percent of the light-sleep idle floor for
  // nothing. At >=1 s intervals the energy cost is unmeasurable (~µC/s), so 1 s
  // is chosen for prompt plug/unplug UX (battery icon, light-sleep USB guard /
  // CDC recovery). X4 detection is a single digitalRead and stays per-loop.
  static constexpr unsigned long USB_POLL_X3_MS = 1000;

  // USB-Serial-JTAG SOF activity, sampled by update(): the host sends a SOF
  // frame every 1 ms while the bus is enumerated, so a frame index that moved
  // between two samples means a live host link. Catches what the charge-based
  // X3 check misses: a data-only cable, and any cable once the battery is full
  // (charge current ~0). Samples must be >SOF_SAMPLE_MS apart — update() can be
  // called back-to-back (inner input loops), and adjacent reads would compare
  // equal and flicker the verdict.
  uint16_t lastSofFrameIndex = 0;
  unsigned long sofLastSampleMs = 0;
  bool usbSofActive = false;
  static constexpr unsigned long SOF_SAMPLE_MS = 10;

  // Per-device electrical/charge-inference USB check (fresh read; X3 = BQ27220
  // charge current over I2C, X4 = VBUS-driven level on GPIO20).
  bool isUsbElectricalConnected() const;

  // Shared body of update()/pollUsbState(): SOF sampling + throttled electrical
  // check + combined-verdict edge tracking.
  void updateUsbState(unsigned long now);

 public:
  enum class DeviceType : uint8_t { X4, X3 };

 private:
  DeviceType _deviceType = DeviceType::X4;

 public:
  HalGPIO() = default;

  // Inline device type helpers for cleaner downstream checks
  inline bool deviceIsX3() const { return _deviceType == DeviceType::X3; }
  inline bool deviceIsX4() const { return _deviceType == DeviceType::X4; }
  bool isXteinkDevice() const;

  // Start button GPIO and setup SPI for screen and SD card
  void begin();

  // Button input methods
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  // True while a raw button-state change is still inside the debounce window.
  // The idle loop polls fast while this is set so the confirming sample lands
  // ~10 ms after the first; at the 50 ms light-sleep cadence a short tap can
  // otherwise appear in a single sample and never commit (dropped press).
  bool isDebouncePending() const;
  unsigned long getHeldTime() const;
  unsigned long getPowerButtonHeldTime() const;
  bool hasTouch() const;
  bool wasTouchTap(float& nx, float& ny) const;
  bool wasTouchDown(float& nx, float& ny) const;
  bool isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const;
  bool isTouchHeldAt(float& nx, float& ny) const;
  unsigned long lastTouchHeldMs() const;
  bool wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const;
  bool wasTouchActivity() const;
  void setSharedConfirmPowerShortPressEmitsPower(bool enabled);

  // --- Host keyboard text entry -------------------------------------------
  //
  // No-ops here, and they stay no-ops: this board has seven buttons and no
  // keyboard, which is why text entry pecks characters out of an on-screen
  // grid. They exist so a text-entry activity can be written once and pick up
  // a real keyboard wherever one exists -- today that is the simulator (the
  // Mac's keyboard, an iPhone's software keyboard, a Bluetooth keyboard
  // paired to the phone), where both are implemented for real.
  //
  // setTextEntryActive() is an announcement, not a request: the activity says
  // a text field is open on enter and closed on exit. A host that owns a
  // keyboard needs it for two things -- raising its on-screen one, and taking
  // the keyboard back off the button map while a field is open, since there
  // the letters P/S/H would otherwise be Power/Sleep/Home.
  //
  // consumeTypedText() drains whatever the host has typed since the last
  // call. It is a byte stream, not a char stream: printable input is UTF-8
  // and the three editing keys ride along as the control bytes below, so a
  // consumer walks the chunk and splits it on those. util/TypedTextInput.h
  // does exactly that, and is what both text-entry activities call.
  //
  // TextEntryLines says whether the open field is one line or many. A host
  // needs it because it has ONE Return key and this board has two separate
  // inputs: Confirm is a GPIO button here, while a paired keyboard's Enter
  // arrives as '\n' through the HID path (notes/HidKeymap.h maps usage 0x28).
  // The host has to choose between them per field -- Select on a single-line
  // grid, a line break in a multi-line editor -- and only the activity knows
  // which it opened. Nothing on this board reads it; it is announced for the
  // same reason the flag itself is.
  enum class TextEntryLines : uint8_t { Single, Multi };
  void setTextEntryActive(bool /*active*/, TextEntryLines /*lines*/ = TextEntryLines::Single) {}
  bool consumeTypedText(std::string& /*out*/) { return false; }

  // Is a HOST's own software keyboard covering the screen right now?
  //
  // Always false here, and not a stub awaiting an implementation: this board
  // has no host and no software keyboard. It exists so an activity can ask the
  // question in ONE form that compiles everywhere, rather than every caller
  // carrying an #if for the simulator -- the simulator's HalGPIO answers it for
  // real (crosspoint-simulator/src/HalGPIO.h), and on a phone it is what says
  // the iOS keyboard is up.
  //
  // The editors use it to drop their own on-screen keyboard panel and give the
  // rows to text while a host keyboard is doing that job (owner ruling
  // 2026-08-16). Because this returns false on device, that branch folds away
  // and an X3 always draws its panel, exactly as before.
  bool isHostKeyboardVisible() const { return false; }

  static constexpr char TYPED_BACKSPACE = '\b';
  static constexpr char TYPED_COMMIT = '\n';
  static constexpr char TYPED_CANCEL = '\x1b';

  // --- Read-aloud page channel --------------------------------------------
  //
  // No-ops here for the mirror-image reason the keyboard channel above is:
  // this board has no speaker, and every host running the simulator has one.
  // The EPUB reader asks readAloudCaptureWanted() when it renders a page for
  // display and, when true, publishes that page's text and per-word rects
  // (ReadAloudWordRect above) through publishReadAloudPage() — and publishes
  // nullptr on exit ("no page", consumers stop speech). Here wanted is
  // constant false, so the capture branch folds away to nothing. The
  // simulator implements both for real and speaks the pages; the full
  // contract lives in its src/ReadAloudChannel.h.
  bool readAloudCaptureWanted() const { return false; }
  void publishReadAloudPage(const char* /*utf8*/, size_t /*utf8Len*/, const ReadAloudWordRect* /*rects*/,
                            size_t /*rectCount*/) {}

  // --- Font-family step channel ---------------------------------------------
  //
  // A no-op here for the same host-capability reason the keyboard channel
  // above is: this board cannot be shaken meaningfully (no accelerometer), and
  // a phone running the simulator can. A host shake in zen mode injects one
  // step; the EPUB reader polls this in its input handling and cycles to the
  // next reading font family (cycleReaderFontFamily(+1)). Constant false, so
  // the poll folds away on device. The simulator implements it for real; the
  // contract lives in its src/FontFamilyStepChannel.h (consume-once, bursts
  // collapse to one step).
  bool consumeFontFamilyStep() { return false; }

  // The reader's FINAL text-block insets — top after the paint-time cap-ink
  // trim, then right, bottom, left — in FRAMEBUFFER pixels (logical px times
  // the active render scale). A no-op here for the same reason the read-aloud
  // channel above is: nothing on this board sits outside the panel, so nothing
  // here could consume where the ink starts. A host drawing furniture around
  // the panel (the iOS zen paper sheet) needs the real insets to place the
  // page, instead of the calibrated constants that drifted the moment the
  // margin or font dial moved (crosspoint-simulator/docs/zen-page-margins.md
  // §4). EpubReaderActivity publishes on every render; the simulator's HalGPIO
  // stores the four values for its harness to read.
  void publishReaderTextInsets(int /*topPx*/, int /*rightPx*/, int /*bottomPx*/, int /*leftPx*/) {}

  // Verify power button was held long enough after wakeup.
  // Returns true if verification succeeded, false if device should return to sleep.
  // Should only be called when wakeup reason is PowerButton.
  bool verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

  // Check if USB is connected
  bool isUsbConnected() const;

  // Sample USB state without a full input update. Called during setup() BEFORE
  // the first e-ink refresh: the boot paint happens before loop() ever runs
  // update(), so without this the light-sleep slice guards see the unsampled
  // default ("no USB") and sleeping through the boot refresh kills a live CDC
  // link (charge-based X3 detection also reads false whenever the battery is
  // full). Two calls >=SOF_SAMPLE_MS apart establish the SOF verdict; the
  // method itself waits out the floor if called too soon after the last sample.
  void pollUsbState();

  // USB state as sampled by the last update() call.
  // Prefer this in per-loop polling: isUsbConnected() performs a fresh I2C read on X3.
  bool isUsbConnectedCached() const { return lastUsbConnected; }

  // Returns true once per edge (plug or unplug) since the last update()
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;
