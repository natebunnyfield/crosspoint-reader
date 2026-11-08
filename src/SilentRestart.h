#pragma once

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session. The live frontlight
// state rides along (silentRebootPayload) so the reboot is invisible on the
// Picco: the light comes back exactly as it was, whatever Restore Light on Wake
// says, unless the slide switch owns the light.

void silentRestart();          // home screen
void silentRestartToReader();  // currently-open EPUB (APP_STATE.openEpubPath)
