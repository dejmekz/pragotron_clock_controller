#include "console.h"
#include "clock.h"
#include "net_sync.h"
#include "storage.h"
#include <Arduino.h>

static String line;

static void print_help()
{
  Serial.println(
      "Commands:\n"
      "  status                     show RTC, dial and sync state\n"
      "  dial HH:MM                 tell the controller what the dial shows now\n"
      "  flip                       swap the polarity of the next pulse\n"
      "  time YYYY-MM-DD HH:MM:SS   set the RTC (local time)\n"
      "  sync                       run a WiFi/NTP sync now\n"
      "  ssid <name>                set WiFi SSID (empty = disable WiFi)\n"
      "  pass <password>            set WiFi password\n"
      "  tz <POSIX TZ>              set time zone, e.g. CET-1CEST,M3.5.0,M10.5.0/3\n"
      "  aging <-128..127>          set DS3231 aging offset\n"
      "  restart                    reboot");
}

static void execute(String cmd)
{
  cmd.trim();
  if (cmd.isEmpty())
    return;
  int sp = cmd.indexOf(' ');
  String name = sp < 0 ? cmd : cmd.substring(0, sp);
  String arg = sp < 0 ? String() : cmd.substring(sp + 1);
  arg.trim();
  name.toLowerCase();

  if (name == "help" || name == "?")
    print_help();
  else if (name == "status")
  {
    clock_print_status(Serial);
    netsync_print_status(Serial);
  }
  else if (name == "dial")
  {
    int h, m;
    if (sscanf(arg.c_str(), "%d:%d", &h, &m) == 2 && clock_set_dial(h, m))
      Serial.println("OK");
    else
      Serial.println("usage: dial HH:MM");
  }
  else if (name == "flip")
  {
    clock_flip_polarity();
    Serial.println("OK");
  }
  else if (name == "time")
  {
    struct tm t = {};
    if (sscanf(arg.c_str(), "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec) == 6)
    {
      t.tm_year -= 1900;
      t.tm_mon -= 1;
      Serial.println(clock_set_local_time(t) ? "OK" : "RTC write failed");
    }
    else
      Serial.println("usage: time YYYY-MM-DD HH:MM:SS");
  }
  else if (name == "sync")
  {
    netsync_request();
    Serial.println("sync requested");
  }
  else if (name == "ssid")
  {
    save_wifi_ssid(arg);
    Serial.println("OK");
  }
  else if (name == "pass")
  {
    save_wifi_pass(arg);
    Serial.println("OK");
  }
  else if (name == "tz")
    Serial.println(clock_set_tz(arg) ? "OK" : "usage: tz <POSIX TZ string>");
  else if (name == "aging")
  {
    char *end;
    long v = strtol(arg.c_str(), &end, 10);
    bool ok = !arg.isEmpty() && *end == '\0' && clock_set_aging((int)v);
    Serial.println(ok ? "OK" : "usage: aging <-128..127>");
  }
  else if (name == "restart")
    ESP.restart();
  else
    Serial.println("unknown command, type 'help'");
}

void console_update()
{
  while (Serial.available())
  {
    char c = Serial.read();
    if (c == '\r' || c == '\n')
    {
      if (!line.isEmpty())
      {
        Serial.printf("> %s\n", line.c_str());
        execute(line);
        line = "";
      }
    }
    else if (line.length() < 128)
      line += c;
  }
}
