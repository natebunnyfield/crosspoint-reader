// SPIKE — throwaway. See ClaudeChat.h.
#include "ClaudeChat.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"

namespace claudechat {
namespace {

constexpr const char* TAG = "CLAUDE";
// Fixed for the spike per ruling: REDACTED-SSID is the only test network.
constexpr const char* SPIKE_SSID = "REDACTED-SSID";
constexpr const char* KEY_PATH = "/claude-key.txt";
constexpr const char* TRANSCRIPT_PATH = "/claude-chat.md";
constexpr const char* MODEL = "claude-haiku-4-5";
constexpr int MAX_TOKENS = 1024;
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;

void logHeap(const char* point) {
  LOG_INF(TAG, "SPIKE-HEAP %s free=%u maxalloc=%u", point, (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap());
}

// "2026-08-05 21:42 UTC" from the DS3231 (NTP-synced UTC), or a millis()
// fallback so a transcript written before any sync is still ordered.
std::string timestamp() {
  uint16_t y;
  uint8_t mo, d, h, mi;
  if (halClock.getDateTime(y, mo, d, h, mi)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u UTC", y, mo, d, h, mi);
    return buf;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "uptime+%lus", millis() / 1000);
  return buf;
}

bool appendToTranscript(const std::string& text) {
  HalFile f = Storage.open(TRANSCRIPT_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!f.isOpen()) {
    LOG_ERR(TAG, "cannot open %s for append", TRANSCRIPT_PATH);
    return false;
  }
  const size_t written = f.write(text.data(), text.size());
  f.flush();
  return written == text.size();
}

bool readApiKey(std::string& outKey) {
  HalFile f;
  if (!Storage.openFileForRead(TAG, KEY_PATH, f)) return false;
  char buf[256];
  const int n = f.read(buf, sizeof(buf) - 1);
  if (n <= 0) return false;
  buf[n] = '\0';
  outKey = buf;
  // Trim trailing newline/whitespace an editor leaves behind.
  while (!outKey.empty() && (outKey.back() == '\n' || outKey.back() == '\r' || outKey.back() == ' ')) outKey.pop_back();
  return !outKey.empty();
}

bool connectWifi(std::string& outErr, void (*statusCb)(void*, const char*), void* ctx) {
  WIFI_STORE.loadFromFile();
  const WifiCredential* cred = WIFI_STORE.findCredential(SPIKE_SSID);
  if (cred == nullptr) {
    outErr = std::string("no saved credential for ") + SPIKE_SSID + " — connect once via File Transfer";
    return false;
  }

  statusCb(ctx, "wifi: connecting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      outErr = "wifi: timeout joining " + cred->ssid;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  LOG_INF(TAG, "wifi connected, ip=%s rssi=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // Timestamps need a synced RTC. Honour the same debounce flag the WiFi
  // picker uses; in-RAM only, no settings write in the spike.
  if (SETTINGS.clockHasBeenSynced == 0 && halClock.isAvailable()) {
    statusCb(ctx, "ntp: syncing clock");
    if (halClock.syncFromNTP()) SETTINGS.clockHasBeenSynced = 1;
  }
  return true;
}

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  logHeap("P9-wifi-off");
}

}  // namespace

Result runExchange(const std::string& prompt, void (*statusCb)(void* ctx, const char* phase), void* cbCtx) {
  Result res;
  logHeap("P6-exchange-start-ble-down");

  std::string apiKey;
  if (!readApiKey(apiKey)) {
    res.error = std::string("no API key at ") + KEY_PATH;
    return res;
  }

  // The prompt goes into the transcript FIRST, so failed attempts are recorded.
  {
    std::string entry = "## " + timestamp() + " — me\n\n" + prompt + "\n\n";
    if (!appendToTranscript(entry)) {
      res.error = "SD append failed";
      return res;
    }
  }

  if (!connectWifi(res.error, statusCb, cbCtx)) {
    appendToTranscript("## " + timestamp() + " — error\n\n" + res.error + "\n\n---\n\n");
    return res;
  }
  logHeap("P7-wifi-connected");

  // Request body via ArduinoJson so prompt quoting/newlines are escaped right.
  std::string payload;
  {
    JsonDocument doc;
    doc["model"] = MODEL;
    doc["max_tokens"] = MAX_TOKENS;
    JsonObject msg = doc["messages"].add<JsonObject>();
    msg["role"] = "user";
    msg["content"] = prompt;
    serializeJson(doc, payload);
  }

  statusCb(cbCtx, "api: TLS + POST");
  {
    freeink::SecureHttpClient http;
    // Matches the firmware's own HTTPS posture (HttpDownloader/OTA): no CA
    // bundle is wired into the wolfSSL transport yet, so peer verification is
    // off. Fine for a bench spike on our own AP; NOT shippable with an API key.
    http.setInsecure();
    if (!http.begin("https://api.anthropic.com/v1/messages")) {
      res.error = "http begin failed";
    } else {
      http.addHeader("x-api-key", apiKey);
      http.addHeader("anthropic-version", "2023-06-01");
      http.addHeader("content-type", "application/json");
      const uint32_t t0 = millis();
      const int status = http.POST(payload);
      const uint32_t elapsed = millis() - t0;
      logHeap("P8-after-post");
      LOG_INF(TAG, "POST status=%d in %lums, body=%u bytes", status, (unsigned long)elapsed,
              (unsigned)http.getString().size());

      if (status == 200) {
        JsonDocument doc;
        const DeserializationError derr = deserializeJson(doc, http.getString());
        if (derr) {
          res.error = std::string("JSON parse: ") + derr.c_str();
        } else {
          const char* text = doc["content"][0]["text"];
          if (text == nullptr) {
            res.error = "no content[0].text in response";
          } else {
            res.responseText = text;
            res.ok = true;
          }
        }
      } else {
        // Keep the first line of the error body — it names the actual problem
        // (bad key, overloaded, bad request) without dumping KBs to the file.
        std::string body = http.getString();
        if (body.size() > 200) body.resize(200);
        res.error = "HTTP " + std::to_string(status) + ": " + body;
      }
    }
  }

  wifiOff();

  if (res.ok) {
    std::string entry = "## " + timestamp() + " — claude (" + MODEL + ")\n\n" + res.responseText + "\n\n---\n\n";
    if (!appendToTranscript(entry)) {
      // The response survived the network but not the SD write — surface that.
      res.ok = false;
      res.error = "response received but SD append failed";
    }
  } else {
    appendToTranscript("## " + timestamp() + " — error\n\n" + res.error + "\n\n---\n\n");
  }
  return res;
}

}  // namespace claudechat
