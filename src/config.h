#pragma once
#include <Arduino.h>

// ── Pins (Arduino Nano ESP32 form factor — same wiring as the original Nano) ─
// Arduino pin names are mapped to ESP32-S3 GPIOs by the board variant.
// NOTE: ESP32-S3 inputs are 3.3 V only — nothing may pull a pin to 5 V.
const uint8_t PIN_I2C_SDA   = SDA; // A4
const uint8_t PIN_I2C_SCL   = SCL; // A5
const uint8_t PIN_MOTOR_A   = D3;  // motor coil A
const uint8_t PIN_MOTOR_B   = D12; // motor coil B
const uint8_t PIN_INDICATOR = D13; // LED indicator (also the on-board LED)
const uint8_t PIN_AUTO_SW   = D5;  // auto/manual switch (LOW = auto, connect to GND)
const uint8_t PIN_BTN_ADV   = D6;  // manual advance button (HIGH = pressed)
const uint8_t PIN_BTN_STEP  = D7;  // single step button   (HIGH = pressed)

// ── Dial ───────────────────────────────────────────────────────────────────
// Number of minute positions on the slave clock: 720 for a 12 h dial,
// 1440 for a 24 h dial.
const uint16_t DIAL_MINUTES = 720;

// If the dial is ahead of real time by at most this many minutes, the
// controller stops and waits instead of advancing all the way round.
// Must be > 60 so the autumn DST change is handled by waiting.
const uint16_t HOLD_MAX_MIN = 75;

// ── Timing (milliseconds) ───────────────────────────────────────────────────
const uint32_t PULSE_MS        = 700;  // motor coil energise time
const uint32_t PAUSE_MS        = 300;  // minimum gap after each pulse
const uint32_t BLINK_MS        = 20;   // per-second LED blink
const uint32_t DEBOUNCE_MS     = 30;
const uint32_t LONG_PRESS_MS   = 3000; // STEP hold time for "dial is correct"
const uint32_t RTC_POLL_MS     = 10;

// ── DS3231 ─────────────────────────────────────────────────────────────────
// Default aging offset written to register 0x10 (can be changed at runtime
// with the `aging` console command, which is stored in NVS).
// 1 LSB ≈ 0.1 ppm ≈ 8.6 ms/day. Positive values slow the oscillator.
const int8_t DS3231_AGING_DEFAULT = 5;

// ── Time zone (POSIX TZ string; changeable with the `tz` command) ───────────
#define DEFAULT_TZ "CET-1CEST,M3.5.0,M10.5.0/3" // Europe/Prague

// ── Network time sync ──────────────────────────────────────────────────────
const uint32_t SYNC_FIRST_DELAY_S   = 10;          // after boot
const uint32_t SYNC_INTERVAL_S      = 24 * 3600;   // after a successful sync
const uint32_t SYNC_RETRY_MIN_S     = 15 * 60;     // first retry after failure
const uint32_t SYNC_RETRY_MAX_S     = 6 * 3600;    // retry back-off cap
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
const uint32_t NTP_TIMEOUT_MS       = 2000;        // per request
static const char *const NTP_SERVERS[] = {"pool.ntp.org", "time.google.com", "tik.cesnet.cz"};
