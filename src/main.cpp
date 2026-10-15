// Pragotron slave clock controller for ESP32-S3 + DS3231.
//
// The DS3231 keeps UTC. Once a day the ESP32 briefly joins WiFi, fetches NTP
// time, corrects the RTC and turns WiFi off again; without WiFi the clock
// simply keeps running on the RTC. The controller remembers which minute the
// dial shows (in NVS) and advances or waits until it matches local time, so
// power cuts and DST changes are corrected automatically.

#include <Arduino.h>
#include "clock.h"
#include "console.h"
#include "net_sync.h"

void setup()
{
  Serial.begin(115200);
  Serial.println("\nPragotron clock controller - type 'help' for commands");

  clock_begin();
  netsync_begin();
}

void loop()
{
  clock_update();
  console_update();
  delay(1); // let the idle task run (feeds the task watchdog)
}
