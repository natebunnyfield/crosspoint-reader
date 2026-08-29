#include "OtaCommit.h"

#include <Logging.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace ota_commit {

void confirmBootIfPending() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    // Not an OTA-booted image (a factory app, or the simulator). Nothing to
    // confirm and nothing wrong.
    LOG_DBG("OTA", "no running OTA partition; nothing to confirm");
    return;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    // Reachable when the partition table has no otadata, which is a valid
    // single-slot layout. Not an error for us.
    LOG_DBG("OTA", "no OTA state for %s; nothing to confirm", running->label);
    return;
  }

  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    LOG_DBG("OTA", "running %s, state=%d; already committed", running->label, static_cast<int>(state));
    return;
  }

  // This boot is the trial run of a freshly installed image, and reaching here
  // means it got through everything setup() does. Confirm it, or the bootloader
  // rolls back on the next boot.
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err == ESP_OK) {
    LOG_INF("OTA", "new firmware confirmed on %s; rollback cancelled", running->label);
  } else {
    // Leave it pending rather than trying to force it: a failure here means the
    // next boot reverts to the previous image, which is the safe direction.
    LOG_ERR("OTA", "could not confirm new firmware (%s); it will roll back on the next boot", esp_err_to_name(err));
  }
}

}  // namespace ota_commit
