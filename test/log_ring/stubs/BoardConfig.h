#pragma once
// Logging.cpp reaches for the board's RTC_NOINIT_ATTR through BoardConfig.h. On
// the host there is no RTC domain and nothing survives a reset, so the
// attribute is simply nothing -- the ring becomes ordinary static storage,
// which is exactly what these tests want to inspect.
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
