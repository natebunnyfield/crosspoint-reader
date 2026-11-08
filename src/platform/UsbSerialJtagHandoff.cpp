#include "UsbSerialJtagHandoff.h"

// Ported from upstream 218dc6aaf (src/platform/UsbSerialJtagHandoff.cpp)
// unchanged in its register sequence. Upstream also compiles a no-op body for
// every other build; here the only caller (restartToHomeAfterStorageHandoff in
// main.cpp) exists only in USB Drive builds, so every other build compiles this
// translation unit to nothing.
#if FREEINK_CAP_USB_MSC

#include <sdkconfig.h>

#if !CONFIG_IDF_TARGET_ESP32S3
#error "USB Drive's Serial/JTAG handoff is written for the ESP32-S3 USB PHY"
#endif

#include <Arduino.h>
#include <esp_err.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <hal/clk_gate_ll.h>
#include <hal/usb_serial_jtag_ll.h>
#include <soc/periph_defs.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <soc/usb_serial_jtag_reg.h>
#include <soc/usb_serial_jtag_struct.h>

// Defined by the Arduino core's esp32-hal-tinyusb.c, which compiles only with
// CONFIG_TINYUSB_ENABLED (scripts/picco_tinyusb_config.py supplies it here).
extern "C" esp_err_t deinit_usb_hal();

namespace {
void IRAM_ATTR onUsbSerialJtagReset(void* context) {
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  const uint32_t interruptStatus = usb_serial_jtag_ll_get_intsts_mask();
  usb_serial_jtag_ll_clr_intsts_mask(interruptStatus);
  if (interruptStatus & USB_SERIAL_JTAG_INTR_BUS_RESET) {
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context), &higherPriorityTaskWoken);
  }
  if (higherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}
}  // namespace

void handoffUsbOtgToSerialJtag() {
  // Serial/JTAG was active before USB Drive claimed the shared PHY. Release
  // its old ISR first so the temporary BUS_RESET listener below can own it.
  Serial.end();

  // Match Arduino's usb_switch_to_cdc_jtag(): stop USB-OTG, select the
  // hardware Serial/JTAG PHY, and force a host-visible disconnect/reconnect.
  (void)deinit_usb_hal();
  periph_ll_reset(PERIPH_USB_MODULE);
  periph_ll_disable_clk_set_rst(PERIPH_USB_MODULE);
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG,
                      RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL | USB_SERIAL_JTAG_USB_PAD_ENABLE);

  pinMode(USB_INT_PHY0_DM_GPIO_NUM, OUTPUT_OPEN_DRAIN);
  pinMode(USB_INT_PHY0_DP_GPIO_NUM, OUTPUT_OPEN_DRAIN);
  digitalWrite(USB_INT_PHY0_DM_GPIO_NUM, LOW);
  digitalWrite(USB_INT_PHY0_DP_GPIO_NUM, LOW);

  const usb_serial_jtag_pull_override_vals_t pullConfig = {
      .dp_pu = 1,
      .dm_pu = 0,
      .dp_pd = 0,
      .dm_pd = 0,
  };
  usb_serial_jtag_ll_phy_enable_pull_override(&pullConfig);
  usb_serial_jtag_ll_phy_disable_pull_override();
  usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
  usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
  usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);

  // Wait (at most 1 s) for the host to reset the bus on the Serial/JTAG
  // device, so the reboot below starts against a host that has seen the
  // disconnect. The semaphore and the interrupt live only for this call.
  SemaphoreHandle_t resetSemaphore = xSemaphoreCreateBinary();
  intr_handle_t resetInterrupt = nullptr;
  if (!resetSemaphore || esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE, 0, onUsbSerialJtagReset, resetSemaphore,
                                        &resetInterrupt) != ESP_OK) {
    if (resetSemaphore) vSemaphoreDelete(resetSemaphore);
    resetSemaphore = nullptr;
  }

  SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  if (resetSemaphore) {
    (void)xSemaphoreTake(resetSemaphore, pdMS_TO_TICKS(1000));
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
    esp_intr_free(resetInterrupt);
    vSemaphoreDelete(resetSemaphore);
  } else {
    // Preserve a visible disconnect even if the temporary listener could not
    // be allocated; the subsequent normal reboot initializes Serial/JTAG.
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
    delay(20);
  }
}

#endif  // FREEINK_CAP_USB_MSC
