#include "storage.h"
#include <Preferences.h>

static const char *NS_CLOCK = "clock";
static const char *NS_NET   = "net";

// Dial position and polarity are packed into one value so each pulse costs a
// single NVS write (~1440/day; NVS wear-levelling makes this last decades).
bool load_dial(DialState &s)
{
  Preferences p;
  p.begin(NS_CLOCK, true);
  bool found = p.isKey("dial");
  uint32_t v = p.getUInt("dial", 0);
  p.end();
  if (!found)
    return false;
  s.position = v & 0xFFFF;
  s.polarity = v >> 16;
  return true;
}

void save_dial(const DialState &s)
{
  Preferences p;
  p.begin(NS_CLOCK, false);
  p.putUInt("dial", ((uint32_t)s.polarity << 16) | s.position);
  p.end();
}

int8_t load_aging(int8_t fallback)
{
  Preferences p;
  p.begin(NS_CLOCK, true);
  int8_t v = p.getChar("aging", fallback);
  p.end();
  return v;
}

void save_aging(int8_t v)
{
  Preferences p;
  p.begin(NS_CLOCK, false);
  p.putChar("aging", v);
  p.end();
}

String load_tz(const char *fallback)
{
  Preferences p;
  p.begin(NS_CLOCK, true);
  String v = p.isKey("tz") ? p.getString("tz") : String(fallback);
  p.end();
  return v;
}

void save_tz(const String &tz)
{
  Preferences p;
  p.begin(NS_CLOCK, false);
  p.putString("tz", tz);
  p.end();
}

bool load_rtc_trusted()
{
  Preferences p;
  p.begin(NS_CLOCK, true);
  bool v = p.getBool("trusted", false);
  p.end();
  return v;
}

int64_t load_rtc_last_set()
{
  Preferences p;
  p.begin(NS_CLOCK, true);
  int64_t v = p.getLong64("lastset", 0);
  p.end();
  return v;
}

void save_rtc_set(bool trusted, int64_t utc)
{
  Preferences p;
  p.begin(NS_CLOCK, false);
  p.putBool("trusted", trusted);
  p.putLong64("lastset", utc);
  p.end();
}

static String load_net(const char *key, const char *fallback)
{
  Preferences p;
  p.begin(NS_NET, false); // read-write creates the namespace, avoiding NOT_FOUND log spam
  String v = p.isKey(key) ? p.getString(key) : String(fallback);
  p.end();
  return v;
}

static void save_net(const char *key, const String &v)
{
  Preferences p;
  p.begin(NS_NET, false);
  p.putString(key, v);
  p.end();
}

String load_wifi_ssid(const char *fallback) { return load_net("ssid", fallback); }
String load_wifi_pass(const char *fallback) { return load_net("pass", fallback); }
void   save_wifi_ssid(const String &v) { save_net("ssid", v); }
void   save_wifi_pass(const String &v) { save_net("pass", v); }
