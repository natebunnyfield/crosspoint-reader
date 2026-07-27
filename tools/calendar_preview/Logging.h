#pragma once
#include <cstdio>
#define LOG_DBG(tag, ...) do { fprintf(stderr, "[D %s] ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr);} while(0)
#define LOG_INF(tag, ...) do { fprintf(stderr, "[I %s] ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr);} while(0)
#define LOG_ERR(tag, ...) do { fprintf(stderr, "[E %s] ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr);} while(0)
