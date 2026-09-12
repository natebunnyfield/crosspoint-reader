#include "Logging.h"

#include <BoardConfig.h>
#include <esp_rom_sys.h>

#include <cstring>
#include <string>

#define MAX_ENTRY_LEN 256
#define MAX_LOG_LINES 16

// Simple ring buffer log, useful for error reporting when we encounter a crash
RTC_NOINIT_ATTR char logMessages[MAX_LOG_LINES][MAX_ENTRY_LEN];
RTC_NOINIT_ATTR size_t logHead = 0;
// Magic word written alongside logHead to detect uninitialized RTC memory.
// RTC_NOINIT_ATTR is not zeroed on cold boot, so logHead may appear in-range
// (0..MAX_LOG_LINES-1) by chance even though logMessages is garbage. The magic
// value is only set by clearLastLogs(), so its absence means the buffer was
// never properly initialized.
RTC_NOINIT_ATTR uint32_t rtcLogMagic;
static constexpr uint32_t LOG_RTC_MAGIC = 0xDEADBEEF;

// How many times in a row the newest line has repeated, and the newest line as
// it arrived. Both live in RTC memory with the ring so they survive the reset
// that produces a crash report.
//
// The raw copy is not redundant: once a repeat is folded into the slot the
// stored text carries a " (xN)" suffix, so comparing the NEXT message against
// the slot would stop matching and only pairs would ever collapse. That is
// exactly what the first version of this did, and test/log_ring caught it.
RTC_NOINIT_ATTR uint32_t logRepeatCount;
RTC_NOINIT_ATTR char logLastRaw[MAX_ENTRY_LEN];

// Compare two entries ignoring the leading "[timestamp] " that logPrintf
// prepends -- a retry storm produces identical text at different milliseconds,
// and comparing the whole line would never match.
static const char* afterTimestamp(const char* line) {
  if (line[0] != '[') return line;
  const char* close = strchr(line, ']');
  return close && close[1] == ' ' ? close + 2 : line;
}

void addToLogRingBuffer(const char* message) {
  // Add the message to the ring buffer, overwriting old messages if necessary.
  // If the magic is wrong or logHead is out of range (RTC_NOINIT_ATTR garbage
  // on cold boot), clear the entire buffer so subsequent reads are safe.
  if (rtcLogMagic != LOG_RTC_MAGIC || logHead >= MAX_LOG_LINES) {
    memset(logMessages, 0, sizeof(logMessages));
    logHead = 0;
    logRepeatCount = 0;
    logLastRaw[0] = '\0';
    rtcLogMagic = LOG_RTC_MAGIC;
  }

  // COLLAPSE CONSECUTIVE DUPLICATES instead of spending a ring slot on each.
  //
  // Sixteen slots is the whole crash report. The B-040 report is THIRTEEN
  // identical "buildAdvanceTable: failed to allocate codepoint buffer (16384
  // bytes)" lines, which pushed every line that led up to the failure out of
  // the ring -- so the one report of a crash that happens in daily use told us
  // nothing about how the device got there. A retry storm is exactly when the
  // preceding context matters most, and exactly when the old ring threw it
  // away.
  //
  // The repeat is folded into the newest slot as a "(xN)" suffix, so the line
  // is still there and the count is visible.
  const size_t newest = (logHead + MAX_LOG_LINES - 1) % MAX_LOG_LINES;
  const bool haveNewest = logHead != 0 || logMessages[newest][0] != '\0';
  if (haveNewest && logLastRaw[0] != '\0' && strcmp(afterTimestamp(logLastRaw), afterTimestamp(message)) == 0) {
    logRepeatCount++;
    // Rewrite the slot from the RAW text plus the running count, so the suffix
    // never accumulates and the count is always right.
    char collapsed[MAX_ENTRY_LEN];
    snprintf(collapsed, sizeof(collapsed), "%s (x%u)", logLastRaw, static_cast<unsigned>(logRepeatCount + 1));
    strncpy(logMessages[newest], collapsed, MAX_ENTRY_LEN - 1);
    logMessages[newest][MAX_ENTRY_LEN - 1] = '\0';
    return;
  }

  logRepeatCount = 0;
  strncpy(logLastRaw, message, MAX_ENTRY_LEN - 1);
  logLastRaw[MAX_ENTRY_LEN - 1] = '\0';
  strncpy(logMessages[logHead], message, MAX_ENTRY_LEN - 1);
  logMessages[logHead][MAX_ENTRY_LEN - 1] = '\0';
  logHead = (logHead + 1) % MAX_LOG_LINES;
}

// Since logging can take a large amount of flash, we want to make the format string as short as possible.
// This logPrintf prepend the timestamp, level and origin to the user-provided message, so that the user only needs to
// provide the format string for the message itself.
void logPrintf(const char* level, const char* origin, const char* format, ...) {
  va_list args;
  va_start(args, format);
  char buf[MAX_ENTRY_LEN];
  char* c = buf;
  // add timestamp, level and origin
  {
    unsigned long ms = millis();
    int len = snprintf(c, sizeof(buf), "[%lu] [%s] [%s] ", ms, level, origin);
    // error while writing => return
    if (len < 0) {
      va_end(args);
      return;
    }
    // clamp c to be in buffer range
    c += std::min(len, MAX_ENTRY_LEN);
  }
  // add the user message
  {
    int len = vsnprintf(c, sizeof(buf) - (c - buf), format, args);
    if (len < 0) {
      va_end(args);
      return;
    }
  }
  va_end(args);
#if FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF
  // IDF/ROM console path for boards monitored over USB-Serial-JTAG, where the
  // HWCDC `operator bool` reads false under `pio device monitor` and logs would
  // otherwise be silently dropped (e.g. Sticky).
  esp_rom_printf("%s", buf);
#else
  if (logSerial) {
    logSerial.print(buf);
  }
#endif
  addToLogRingBuffer(buf);
}

std::string getLastLogs() {
  if (rtcLogMagic != LOG_RTC_MAGIC) {
    return {};
  }
  std::string output;
  for (size_t i = 0; i < MAX_LOG_LINES; i++) {
    size_t idx = (logHead + i) % MAX_LOG_LINES;
    if (logMessages[idx][0] != '\0') {
      const size_t len = strnlen(logMessages[idx], MAX_ENTRY_LEN);
      output.append(logMessages[idx], len);
    }
  }
  return output;
}

// Checks whether the RTC log state is consistent: rtcLogMagic must equal
// LOG_RTC_MAGIC and logHead must be in 0..MAX_LOG_LINES-1. Returns true if
// corruption is detected, in which case rtcLogMagic is still invalid and
// logMessages may contain garbage. Callers (e.g. HalSystem::begin on the
// panic-reboot path) must call clearLastLogs() after a true result to fully
// reinitialize the ring buffer and stamp the magic before getLastLogs() is used.
bool sanitizeLogHead() {
  if (rtcLogMagic != LOG_RTC_MAGIC || logHead >= MAX_LOG_LINES) {
    logHead = 0;
    return true;
  }
  return false;
}

void clearLastLogs() {
  for (size_t i = 0; i < MAX_LOG_LINES; i++) {
    logMessages[i][0] = '\0';
  }
  logHead = 0;
  logRepeatCount = 0;
  logLastRaw[0] = '\0';
  rtcLogMagic = LOG_RTC_MAGIC;
}
