#pragma once
#include <Arduino.h>
#include <time.h>

// Minimal thread-safe DS3231 driver. The RTC always holds UTC.

struct RtcTime
{
  time_t utc;      // seconds since 1970-01-01 UTC
  bool   osf;      // oscillator-stop flag: time is not trustworthy
};

bool rtc_begin(uint8_t sda, uint8_t scl);
bool rtc_read(RtcTime &out);
bool rtc_write(time_t utc); // also clears the oscillator-stop flag
bool rtc_set_aging(int8_t offset);
bool rtc_get_aging(int8_t &offset);
bool rtc_read_temperature(float &celsius);

// Calendar helpers (UTC, no time zone involved)
time_t utc_from_fields(int year, int month, int day, int hour, int min, int sec);
