#pragma once

#if FREEINK_CAP_USB_MSC
// Releases the ESP32-S3 USB-OTG PHY and reconnects the hardware USB
// Serial/JTAG peripheral before a normal application restart, so the reader
// comes back as the USB Serial/JTAG device that flashing and logs use
// (upstream 218dc6aaf). Only USB Drive builds (FREEINK_CAP_USB_MSC) have it.
void handoffUsbOtgToSerialJtag();
#endif
