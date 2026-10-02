#pragma once

// Compile-time logging. SIREN_DEBUG and SIREN_DEBUG_VERBOSE are set in each
// target's platformio.ini. Heartbeats and per-loop state must use VERBOSE
// so a normal debug build does not write every iteration.

#include <Arduino.h>

#ifndef SIREN_DEBUG
#define SIREN_DEBUG 0
#endif

#ifndef SIREN_DEBUG_VERBOSE
#define SIREN_DEBUG_VERBOSE 0
#endif

#if SIREN_DEBUG
#define SIREN_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define SIREN_LOG(...) ((void)0)
#endif

#if SIREN_DEBUG && SIREN_DEBUG_VERBOSE
#define SIREN_LOG_VERBOSE(...) Serial.printf(__VA_ARGS__)
#else
#define SIREN_LOG_VERBOSE(...) ((void)0)
#endif
