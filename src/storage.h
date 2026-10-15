#pragma once
#include <Arduino.h>

// Persistent settings in NVS flash. Every function opens and closes its own
// handle, so they are safe to call from any task.

struct DialState
{
  uint16_t position; // minute position shown on the dial, 0..DIAL_MINUTES-1
  bool     polarity; // polarity of the NEXT pulse (false = coil A)
};

bool   load_dial(DialState &s); // false if never stored
void   save_dial(const DialState &s);

int8_t load_aging(int8_t fallback);
void   save_aging(int8_t v);

String load_tz(const char *fallback);
void   save_tz(const String &tz);

// RTC provenance: "trusted" = last set from NTP or an explicit console time,
// last_set = UTC epoch of that set (used to compute drift at the next sync).
bool   load_rtc_trusted();
int64_t load_rtc_last_set();
void   save_rtc_set(bool trusted, int64_t utc);

String load_wifi_ssid(const char *fallback);
String load_wifi_pass(const char *fallback);
void   save_wifi_ssid(const String &v);
void   save_wifi_pass(const String &v);
