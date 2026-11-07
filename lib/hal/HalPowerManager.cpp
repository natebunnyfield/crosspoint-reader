#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

#include <cassert>

#include "HalGPIO.h"

#if FREEINK_DEVICE_PICCO
#include <HalFrontlight.h>
#endif

HalPowerManager powerManager;  // Singleton instance

// GPIO13 must stay high during light sleep on the C3 Xteink boards: it controls
// the X4 battery latch and the X3 SD power rail. Other boards use it for
// unrelated signals, including the X4 Pro display chip select.
static constexpr gpio_num_t XTEINK_C3_GPIO13 = GPIO_NUM_13;

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
#if FREEINK_DEVICE_PICCO
  // SGM41562 charger setup: the stock charge voltage and limits for the
  // Picco's high-voltage cell (upstream 261726f61).
  if (BatteryMonitor::configureCharger()) LOG_INF("PWR", "Charger configured");
#endif
  mainLoopTask = xTaskGetCurrentTaskHandle();  // Arduino runs setup() on the loop task
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
  sleepMutex = xSemaphoreCreateMutex();
  assert(sleepMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Note: We don't use mutex here to avoid too much overhead,
  // it's not very important if we read a slightly stale value for currentLockMode
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio, const bool wakeOnHeldButton) const {
#if FREEINK_DEVICE_PICCO
  // The frontlight driver and touch share I2C; turn the light off before touch
  // releases the bus. This also puts the TMA525C to sleep (upstream 261726f61).
  Frontlight.setOn(false);
  gpio.prepareForDeepSleep();
#else
  (void)wakeOnHeldButton;
#endif
#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  if (gpio.isXteinkDevice()) {
    // GPIO13 gates the battery MOSFET on both Xteink C3 boards; driving it low
    // is the battery power-off (the SDK wake source still handles USB power).
    // Unlike upstream, X3 is included: lightSleep() pad-holds the latch HIGH
    // persistently while running (the IDF flash-leakage workaround would drop
    // it mid-slice, hard-powering the device off), so the implicit low that
    // upstream's X3 relies on never happens here — release the hold and drive
    // the latch low explicitly. The hold_en survives deep sleep via the SDK's
    // deepSleep() (esp_sleep_config_gpio_isolate + gpio_deep_sleep_hold_en).
    gpio_hold_dis(XTEINK_C3_GPIO13);
    gpio_set_direction(XTEINK_C3_GPIO13, GPIO_MODE_OUTPUT);
    gpio_set_level(XTEINK_C3_GPIO13, 0);
    gpio_hold_en(XTEINK_C3_GPIO13);
  }
#endif

#if FREEINK_DEVICE_PICCO
  // Hold the Picco's power latch (power.latch0 = GPIO4) HIGH through deep
  // sleep, as stock does: deepSleep() isolates every pad, and a latch with no
  // armed hold loses its driver and floats. Upstream runs this loop for every
  // board; here it is the Picco's alone, so the Sticky's sleep path is the one
  // it always was. A held pad ignores a new drive, so release it first.
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0) continue;
    const auto g = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(g);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(g);
  }
#endif

  // Cut the gated peripheral rails (touch/SD/EPD on boards like the Sticky) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4/X3). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  freeink::PowerManager::powerDownRailsForSleep();

#if FREEINK_DEVICE_PICCO
  if (wakeOnHeldButton) {
    freeink::PowerManager::armPowerButtonWakeup();
    freeink::PowerManager::deepSleep();
  }
#endif
  // Waits for the power button to be physically released (so holding it doesn't
  // immediately wake the device again), then arms the wake source and sleeps.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

#if FREEINK_DEVICE_PICCO
// ponytail: 30 s poll; the SGM41562 status is I2C-only, so USB unplug and
// charge progress are only seen when the timer fires.
static constexpr uint64_t USB_HOLD_POLL_US = 30ULL * 1000 * 1000;

bool HalPowerManager::holdWhileUsbPowered(HalGPIO& gpio, void (*onStatus)(bool charging, uint16_t percent)) const {
  // Electrical power only, not isUsbConnected(): see isUsbPowerPresent().
  if (!gpio.isUsbPowerPresent()) return false;
  LOG_DBG("PWR", "USB power present, holding in light sleep");
  const auto powerPin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  // The press that requested sleep may still be down; it must not end the hold.
  while (digitalRead(powerPin) == LOW) delay(10);  // Picco power button is active-low
  const BatteryMonitor battery;
  int lastCharging = -1;
  int lastPercent = -1;
  while (gpio.isUsbPowerPresent()) {
    const bool charging = battery.isCharging();
    const uint16_t percent = getBatteryPercentage();
    if (static_cast<int>(charging) != lastCharging || percent != lastPercent) {
      lastCharging = charging;
      lastPercent = percent;
      onStatus(charging, percent);
    }
    // No sleepMutex here, as upstream: this runs on the loop task inside
    // enterDeepSleep(), so lightSleep() cannot run concurrently, and the only
    // other sleeper, onEinkBusyWaitSlice(), runs (if at all) inside onStatus()'s
    // refresh on this same task and disarms before it returns.
    esp_sleep_enable_timer_wakeup(USB_HOLD_POLL_US);
    gpio_wakeup_enable(powerPin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    gpio_wakeup_disable(powerPin);
    // gpio_wakeup_disable() keeps the LOW_LEVEL type, and a later pinMode() would
    // enable it as an interrupt that storms while the button is held.
    gpio_set_intr_type(powerPin, GPIO_INTR_DISABLE);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (digitalRead(powerPin) == LOW) return true;
  }
  return false;
}
#endif

bool HalPowerManager::lightSleep(const HalGPIO& gpio) const {
  // A performance Lock means a render (or similar) task is mid-flight; light sleep
  // freezes the whole chip, so it would stall that task.
  // Note: like setPowerSaving(), read without the mutex — stale in either
  // direction is acceptable: a stale lock delays sleeping by one 50 ms slice;
  // a Lock acquired after this check freezes that task for at most one slice
  // (timer wake; RAM/peripheral state retained), which is cheaper than
  // serializing Lock ctor/dtor against the entire sleep window.
  if (currentLockMode != None) {
    return false;
  }
  // Light sleep drops a WiFi association and kills an enumerated USB-CDC link.
  if (WiFi.getMode() != WIFI_MODE_NULL || gpio.isUsbConnectedCached()) {
    return false;
  }

  // Serialize wake-source arming with the render task's BUSY-wait slice (see
  // sleepMutex declaration for the failure mode this prevents).
  xSemaphoreTake(sleepMutex, portMAX_DELAY);

  // Timer wake keeps the button/tilt poll cadence identical to the delay() it
  // replaces: the front/side buttons are ADC resistor-ladder inputs whose
  // pressed levels sit mid-rail, invisible to a digital GPIO wake, so they
  // must be polled. The power button (a true GPIO) is ALSO armed, as a level
  // wake at its pressed level: committing a press needs two update() samples
  // >=5 ms apart, so at the timer-only 50 ms cadence a tap shorter than a
  // slice could land in one sample — or none — and be dropped entirely
  // (field-reported as unreliable short-press sleep). The wake is only an
  // early poll: update()'s debounce still decides whether it was a real
  // press, so a misread around the sleep transition costs one extra wake
  // blip, never a phantom press. Idle cost is zero — the pin only holds its
  // pressed level while a finger is on it.
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(LIGHT_SLEEP_SLICE_MS) * 1000ULL);
  const int8_t powerPin = BoardConfig::ACTIVE.input.power;
  if (powerPin >= 0) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(powerPin),
                       BoardConfig::ACTIVE.input.powerActiveHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
#if FREEINK_DEVICE_PICCO
  // The TMA525C's INT is armed the same way, so a touch-down wakes the slice
  // at once instead of waiting up to 50 ms to be polled (owner ruling
  // 2026-11-06, "so short taps after idle are not missed"). INT is active
  // low, and low means a report is pending (freeink-sdk InputManager.cpp:1212,
  // BoardConfig.h:1468). Armed only when INT is high at entry: a pending
  // report (or a line stuck low) would otherwise wake every slice at once and
  // spin; that slice keeps the timer-only cadence instead, which is the
  // behavior before this change. Like the Power wake, it is only
  // an early poll: the gesture decoder still decides what the contact was.
  // Idle cost is zero while INT stays high. UNCONFIRMED on device: whether
  // INT also pulses with no finger on the glass.
  const int8_t touchIrq = BoardConfig::ACTIVE.touch.irq;
  const bool touchWake = touchIrq >= 0 && gpio_get_level(static_cast<gpio_num_t>(touchIrq)) != 0;
  if (touchWake) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(touchIrq), GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
#endif

  // The C3 flash-leakage workaround pulls its DIO-unused SPIWP pad low during
  // light sleep. Hold GPIO13 high only on the X3/X4 boards that use that pad as
  // a power control; on other boards GPIO13 may be a bus signal.
  if (gpio.isXteinkDevice()) {
    gpio_set_level(XTEINK_C3_GPIO13, 1);
    gpio_set_direction(XTEINK_C3_GPIO13, GPIO_MODE_OUTPUT);
    gpio_hold_en(XTEINK_C3_GPIO13);
  }

  const esp_err_t err = esp_light_sleep_start();

  // Disarm immediately: an armed timer wake persists across sleep calls and would
  // carry over into startDeepSleep(), waking the device on USB power after 50 ms.
  // gpio_wakeup_disable() clears only the wake-enable bit — the pin's LEVEL
  // interrupt type survives it. A leftover level type is live ammunition for
  // any later-installed GPIO ISR service: an asserted level feeding the
  // shared service with no per-pin handler registered re-enters the ISR
  // forever and livelocks the CPU. Clear the type explicitly.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (powerPin >= 0) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(powerPin));
    gpio_set_intr_type(static_cast<gpio_num_t>(powerPin), GPIO_INTR_DISABLE);
  }
#if FREEINK_DEVICE_PICCO
  if (touchWake) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(touchIrq));
    gpio_set_intr_type(static_cast<gpio_num_t>(touchIrq), GPIO_INTR_DISABLE);
  }
#endif

  xSemaphoreGive(sleepMutex);

  if (err != ESP_OK) {
    // e.g. ESP_ERR_SLEEP_REJECT — we did not actually sleep
    LOG_DBG("PWR", "Light sleep rejected: %d", static_cast<int>(err));
    return false;
  }
  return true;
}

bool HalPowerManager::onEinkBusyWaitSlice(const int8_t busyPin, const uint8_t busyLevel) {
  // Same exclusions as lightSleep(): light sleep drops a WiFi association and
  // kills an enumerated USB-CDC link. No LOG here — this runs ~50x/s mid-refresh.
  if (WiFi.getMode() != WIFI_MODE_NULL || gpio.isUsbConnectedCached()) {
    return false;
  }

  // Mid-debounce: committing needs a second sample, so let the SDK's short poll
  // delay run instead of halting the chip. Same guard lightSleep() gets in loop().
  // Unsynchronized like the checks above; stale costs at most one slice.
  if (gpio.isDebouncePending()) {
    return false;
  }

  xSemaphoreTake(sleepMutex, portMAX_DELAY);

  // Wake the instant BUSY leaves its active level (refresh complete). Level
  // wake (not edge) means an already-completed refresh returns immediately
  // instead of sleeping a full slice. The timer bound only exists so the main
  // loop gets scheduling windows to keep sampling the ADC-ladder buttons.
  const auto pin = static_cast<gpio_num_t>(busyPin);
  gpio_wakeup_enable(pin, busyLevel == HIGH ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  // Also wake the instant the power button is pressed, so the main loop's poll
  // sees even sub-slice taps mid-refresh (same rationale and safety argument
  // as lightSleep(): the wake is an early poll, never a synthesized press).
  const int8_t powerPin = BoardConfig::ACTIVE.input.power;
  if (powerPin >= 0) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(powerPin),
                       BoardConfig::ACTIVE.input.powerActiveHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(BUSY_SLEEP_SLICE_MS) * 1000ULL);

  // Preserve the C3 Xteink GPIO13 power control during this sleep slice without
  // reconfiguring GPIO13 on boards where it is a bus signal.
  if (gpio.isXteinkDevice()) {
    gpio_set_level(XTEINK_C3_GPIO13, 1);
    gpio_set_direction(XTEINK_C3_GPIO13, GPIO_MODE_OUTPUT);
    gpio_hold_en(XTEINK_C3_GPIO13);
  }

  const esp_err_t err = esp_light_sleep_start();

  // Disarm everything armed above; an armed source persisting into
  // startDeepSleep() would wake the device on USB power (see lightSleep()).
  // As in lightSleep(): also clear the LEVEL interrupt types, which
  // gpio_wakeup_disable() leaves behind (see the livelock note there).
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(pin);
  gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
  if (powerPin >= 0) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(powerPin));
    gpio_set_intr_type(static_cast<gpio_num_t>(powerPin), GPIO_INTR_DISABLE);
  }

  xSemaphoreGive(sleepMutex);

  if (err != ESP_OK) {
    return false;  // e.g. ESP_ERR_SLEEP_REJECT — fall back to the SDK's poll delay
  }

  // Yield until the equal-priority main loop finishes an iteration, not just one
  // tick: one tick is less awake CPU than an iteration needs, so we would re-halt
  // the chip mid-poll. Capped so a loop parked on SD I/O can't hold the panel awake.
  const uint32_t seenIterations = mainLoopIterations;
  // Skipped entirely when the loop cannot make progress and the yield would just
  // burn the cap awake: parked in requestUpdateAndWait() on this very render;
  // never started (setup-time paints, iterations still 0); or this task IS the
  // loop task (direct displayBuffer calls from setup()/loop() busy-wait here).
  const bool loopCanPoll = !mainLoopBlocked && seenIterations != 0 && xTaskGetCurrentTaskHandle() != mainLoopTask;
  unsigned yieldTicks = 0;
  if (loopCanPoll) {
    for (; yieldTicks < SLICE_YIELD_MAX_TICKS && mainLoopIterations == seenIterations; ++yieldTicks) {
      vTaskDelay(1);
    }
  }
  return true;
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  static const BatteryMonitor battery;
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    const unsigned long now = millis();
    if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
      return _batteryCachedPercent;
    }

    _batteryLastPollMs = now;
    uint16_t percent = 0;
    if (!battery.readPercentageChecked(percent)) {
      return _batteryCachedPercent;
    }
    _batteryCachedPercent = percent;
    return _batteryCachedPercent;
  }

  // smooth the battery %.
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = 10 * battery.readPercentage();
  } else {
    _batteryCachedPercent = (_batteryCachedPercent * 9 + battery.readPercentage() * 10) / 10;
  }
  return _batteryCachedPercent / 10;
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
