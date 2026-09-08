#pragma once
#include <cstdio>
#define LOG_DBG(tag, ...)            \
  do {                               \
    fprintf(stderr, "[D %s] ", tag); \
    fprintf(stderr, __VA_ARGS__);    \
    fputc('\n', stderr);             \
  } while (0)
#define LOG_INF(tag, ...)            \
  do {                               \
    fprintf(stderr, "[I %s] ", tag); \
    fprintf(stderr, __VA_ARGS__);    \
    fputc('\n', stderr);             \
  } while (0)
#define LOG_ERR(tag, ...)            \
  do {                               \
    fprintf(stderr, "[E %s] ", tag); \
    fprintf(stderr, __VA_ARGS__);    \
    fputc('\n', stderr);             \
  } while (0)

// The firmware keeps a small ring of recent lines that crash reports and, since
// 2026-09-08, a failed font sync dump to the card. Nothing on the host feeds a
// ring, so this is an honest empty answer rather than a fake one -- callers
// write a header and then whatever this returns.
#include <string>
inline std::string getLastLogs() { return std::string(); }
