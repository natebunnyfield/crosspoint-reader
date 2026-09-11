#pragma once
// The host has no ROM printf; Logging.cpp only calls it to echo to the UART.
#include <cstdarg>
#include <cstdio>
inline void esp_rom_printf(const char*, ...) {}
