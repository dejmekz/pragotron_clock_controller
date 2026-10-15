#pragma once
#include <Arduino.h>

// Background task that wakes WiFi once a day, fetches NTP time, writes it to
// the DS3231 aligned to the second boundary and switches WiFi off again.
// It never blocks the motor control loop.

void netsync_begin();
void netsync_request();          // run a sync as soon as possible
void netsync_print_status(Print &out);
