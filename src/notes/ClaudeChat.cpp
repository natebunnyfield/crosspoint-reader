// See ClaudeChat.h.
#include "ClaudeChat.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <time.h>

#include <optional>
#include <vector>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"

namespace claudechat {
namespace {

#ifdef SIMULATOR
#define POST_COMPAT(s) String((s).c_str())
#else
#define POST_COMPAT(s) (s)
#endif

constexpr const char* TAG = "CLAUDE";
constexpr const char* KEY_PATH = "/claude-key.txt";
constexpr const char* TRANSCRIPT_PATH = "/claude-chat.md";
constexpr const char* MODEL = "claude-haiku-4-5";
constexpr int MAX_TOKENS = 1024;
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;  // whole-search budget
constexpr uint32_t MIN_ATTEMPT_MS = 6000;    // per-network cap when several are saved
// Conversation memory for this editor session. In RAM only — /claude-chat.md is
// the durable record, and re-parsing it would mean holding the whole file.
// Both caps exist because history inflates the request body, which on device
// competes with TLS for the same scarce heap.
constexpr size_t MAX_HISTORY_TURNS = 6;
constexpr size_t MAX_HISTORY_CHARS = 4000;

struct Turn {
  bool assistant;
  std::string text;
};
std::vector<Turn> gHistory;

// Drop oldest turns until both caps hold.
void trimHistory() {
  while (gHistory.size() > MAX_HISTORY_TURNS) gHistory.erase(gHistory.begin());
  size_t total = 0;
  for (const Turn& t : gHistory) total += t.text.size();
  while (total > MAX_HISTORY_CHARS && !gHistory.empty()) {
    total -= gHistory.front().text.size();
    gHistory.erase(gHistory.begin());
  }
}

void logHeap(const char* point) {
  LOG_INF(TAG, "SPIKE-HEAP %s free=%u maxalloc=%u", point, (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap());
}

// "2026-08-05 21:42 UTC". Three sources, best first:
//   1. the DS3231 (NTP-synced UTC) — the device's real answer;
//   2. the C library clock — set by syncFromNTP (which configures "UTC0") on
//      device, and the host wall clock under the simulator, which has no RTC
//      and would otherwise stamp every entry "uptime+16s";
//   3. uptime, so a transcript written before any clock exists is still ordered.
std::string timestamp() {
  char buf[32];

  uint16_t y;
  uint8_t mo, d, h, mi;
  if (halClock.getDateTime(y, mo, d, h, mi)) {
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u UTC", y, mo, d, h, mi);
    return buf;
  }

  const time_t now = time(nullptr);
  if (now > 1700000000) {  // sanity: past 2023, so not an unset epoch clock
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &utc);
    return buf;
  }

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
  // Try every saved network in turn, the way WifiSelectionActivity's auto-connect
  // does. This used to name one hardcoded SSID -- which meant the feature only
  // worked on its author's home network, and put that network's name in a public
  // repo. Nothing here should know any particular network.
  //
  // Take a COPY of the saved networks before the connect loop. The loop below
  // blocks for seconds at a time (WiFi.begin, then delay(100) polling), and the
  // web server task can add or remove a credential in that window -- which
  // reallocates the store's vector and would dangle a reference held across it.
  // getCredentialAt() copies each entry under the store's mutex, so nothing
  // here aliases store internals, and no index into the store survives a
  // blocking call. At most MAX_NETWORKS (8) entries of two short strings each
  // (~1 KB worst case), freed on return; a fixed stack array was rejected
  // because std::string is heap-backed regardless.
  const size_t savedCount = WIFI_STORE.getCredentialCount();
  std::vector<WifiCredential> creds;
  creds.reserve(savedCount);
  for (size_t i = 0; i < savedCount; ++i) {
    std::optional<WifiCredential> cred = WIFI_STORE.getCredentialAt(i);
    if (!cred) break;  // store shrank between the count and the read; use what we got
    creds.push_back(std::move(*cred));
  }
  if (creds.empty()) {
    outErr = "no saved Wi-Fi network — connect once via File Transfer";
    return false;
  }

  WiFi.mode(WIFI_STA);
  const uint32_t budgetStart = millis();
  bool joined = false;
  for (const WifiCredential& cred : creds) {
    const uint32_t spent = millis() - budgetStart;
    if (spent >= WIFI_TIMEOUT_MS) break;
    // WIFI_TIMEOUT_MS is the budget for the WHOLE search, not per network, so a
    // card with eight saved networks cannot stall the editor for minutes. A
    // single saved network still gets the full original timeout.
    uint32_t attemptMs = WIFI_TIMEOUT_MS - spent;
    if (creds.size() > 1 && attemptMs > MIN_ATTEMPT_MS) attemptMs = MIN_ATTEMPT_MS;

    statusCb(ctx, "wifi: connecting");
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
    const uint32_t attemptStart = millis();
    while (millis() - attemptStart <= attemptMs) {
      if (WiFi.status() == WL_CONNECTED) {
        joined = true;
        break;
      }
      delay(100);
    }
    if (joined) break;
    WiFi.disconnect(true);
  }
  if (!joined) {
    outErr = "wifi: could not join any saved network";
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
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
    LOG_ERR(TAG, "%s (exists=%d)", res.error.c_str(), Storage.exists(KEY_PATH) ? 1 : 0);
    return res;
  }
  LOG_INF(TAG, "API key loaded (%u chars)", (unsigned)apiKey.size());

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
  //
  // The API is stateless: a request carrying only the current prompt makes every
  // turn a cold start, and the second question in a thread gets "I don't have
  // context about what you're asking" (observed in the simulator). So replay the
  // session's prior turns as messages. Capped in both directions because this
  // grows the request body, and on device the body competes with TLS for heap.
  std::string payload;
  {
    JsonDocument doc;
    doc["model"] = MODEL;
    doc["max_tokens"] = MAX_TOKENS;
    JsonArray messages = doc["messages"].to<JsonArray>();
    for (const Turn& t : gHistory) {
      JsonObject m = messages.add<JsonObject>();
      m["role"] = t.assistant ? "assistant" : "user";
      m["content"] = t.text;
    }
    JsonObject msg = messages.add<JsonObject>();
    msg["role"] = "user";
    msg["content"] = prompt;
    serializeJson(doc, payload);
    LOG_INF(TAG, "request: %u prior turn(s), %u byte body", (unsigned)gHistory.size(), (unsigned)payload.size());
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
      // The simulator's SecureHttpClient shim speaks Arduino String, the
      // device's speaks std::string; String() converts cleanly for both.
      // The device's SecureHttpClient takes std::string, the simulator's shim
      // takes Arduino String. POST_COMPAT resolves to whichever this build has.
      const int status = http.POST(POST_COMPAT(payload));
      const uint32_t elapsed = millis() - t0;
      logHeap("P8-after-post");
      LOG_INF(TAG, "POST status=%d in %lums, body=%u bytes", status, (unsigned long)elapsed,
              (unsigned)std::string(http.getString().c_str()).size());

      if (status == 200) {
        JsonDocument doc;
        const DeserializationError derr = deserializeJson(doc, std::string(http.getString().c_str()));
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
        std::string body(http.getString().c_str());
        if (body.size() > 200) body.resize(200);
        res.error = "HTTP " + std::to_string(status) + ": " + body;
      }
    }
  }

  wifiOff();

  if (res.ok) {
    // Only a completed exchange joins the history; a failed turn would leave a
    // user message with no assistant reply and skew every later request.
    gHistory.push_back({false, prompt});
    gHistory.push_back({true, res.responseText});
    trimHistory();

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
