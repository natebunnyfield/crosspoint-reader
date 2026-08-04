#include "DeviceId.h"

#include <esp_mac.h>

#include <cstdio>

void getDeviceIdHex(char* out, size_t outSize) {
  uint8_t mac[6] = {0};
  if (esp_efuse_mac_get_default(mac) != 0) {
    snprintf(out, outSize, "000000");
    return;
  }
  snprintf(out, outSize, "%02x%02x%02x", mac[3], mac[4], mac[5]);
}
