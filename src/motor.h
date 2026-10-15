#pragma once
#include <Arduino.h>

// Non-blocking polarised pulse driver for the slave clock movement.
// Each pulse energises the coil for PULSE_MS, then enforces a PAUSE_MS gap.

void motor_begin();
void motor_pulse(bool polarity); // ignored while busy
void motor_update();             // call every loop iteration
bool motor_busy();               // pulse or post-pulse pause in progress
bool motor_energised();          // coil currently driven
