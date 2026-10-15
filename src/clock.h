#pragma once
#include <Arduino.h>
#include <time.h>

// Slave clock controller: keeps the dial in step with the RTC.

void clock_begin();
void clock_update(); // call every loop iteration

// Console helpers
void clock_print_status(Print &out);
bool clock_set_dial(int hour, int minute);  // tell the controller what the dial shows
void clock_flip_polarity();                 // next pulse uses the other coil polarity
bool clock_set_local_time(struct tm &local); // set the RTC from local wall time
bool clock_set_tz(const String &tz);
bool clock_set_aging(int value);
