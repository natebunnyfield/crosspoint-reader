#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)

#if FREEINK_CAP_USB_MSC
// Reboots straight to Home after USB Drive releases the raw SD card (upstream
// 218dc6aaf): hands the USB PHY back to Serial/JTAG first, so the device
// returns as the port flashing and logs use. Never remounts in place.
void restartToHomeAfterStorageHandoff();
#endif
