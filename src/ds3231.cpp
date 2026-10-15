#include "ds3231.h"
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const uint8_t ADDR       = 0x68;
static const uint8_t REG_TIME   = 0x00;
static const uint8_t REG_CTRL   = 0x0E;
static const uint8_t REG_STATUS = 0x0F;
static const uint8_t REG_AGING  = 0x10;
static const uint8_t REG_TEMP   = 0x11;

static const uint8_t CTRL_CONV  = 0x20;
static const uint8_t STAT_OSF   = 0x80;
static const uint8_t STAT_BSY   = 0x04;

// Wire transactions are locked individually by the core, but a register
// pointer write followed by a read must not be interleaved with another task.
static SemaphoreHandle_t bus_mutex = nullptr;

struct BusLock
{
  bool ok;
  BusLock() : ok(xSemaphoreTake(bus_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {}
  ~BusLock() { if (ok) xSemaphoreGive(bus_mutex); }
};

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static bool read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)
    return false;
  if (Wire.requestFrom(ADDR, (uint8_t)len) != len)
    return false;
  for (size_t i = 0; i < len; i++)
    buf[i] = Wire.read();
  return true;
}

static bool write_regs(uint8_t reg, const uint8_t *buf, size_t len)
{
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(buf, len);
  return Wire.endTransmission() == 0;
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant).
static int32_t days_from_civil(int y, unsigned m, unsigned d)
{
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

time_t utc_from_fields(int year, int month, int day, int hour, int min, int sec)
{
  return (time_t)days_from_civil(year, month, day) * 86400 + hour * 3600 + min * 60 + sec;
}

bool rtc_begin(uint8_t sda, uint8_t scl)
{
  if (!bus_mutex)
    bus_mutex = xSemaphoreCreateMutex();
  Wire.begin(sda, scl, 400000);
  Wire.setTimeOut(20);

  BusLock lock;
  uint8_t ctrl;
  if (!lock.ok || !read_regs(REG_CTRL, &ctrl, 1))
    return false;
  // EOSC=0 (run on battery), BBSQW=0, INTCN=1, alarms off.
  ctrl = 0x04;
  return write_regs(REG_CTRL, &ctrl, 1);
}

bool rtc_read(RtcTime &out)
{
  uint8_t r[7], status;
  {
    BusLock lock;
    if (!lock.ok || !read_regs(REG_TIME, r, 7) || !read_regs(REG_STATUS, &status, 1))
      return false;
  }

  int sec = bcd2dec(r[0] & 0x7F);
  int min = bcd2dec(r[1] & 0x7F);
  int hour;
  if (r[2] & 0x40)
  { // 12-hour mode (never written by us, but tolerate it)
    hour = bcd2dec(r[2] & 0x1F) % 12;
    if (r[2] & 0x20)
      hour += 12;
  }
  else
    hour = bcd2dec(r[2] & 0x3F);
  int day   = bcd2dec(r[4] & 0x3F);
  int month = bcd2dec(r[5] & 0x1F);
  int year  = 2000 + bcd2dec(r[6]);

  if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12)
    return false;

  out.utc = utc_from_fields(year, month, day, hour, min, sec);
  out.osf = status & STAT_OSF;
  return true;
}

bool rtc_write(time_t utc)
{
  struct tm t;
  gmtime_r(&utc, &t);
  uint8_t r[7] = {
      dec2bcd(t.tm_sec),
      dec2bcd(t.tm_min),
      dec2bcd(t.tm_hour), // 24-hour mode
      (uint8_t)(t.tm_wday + 1),
      dec2bcd(t.tm_mday),
      dec2bcd(t.tm_mon + 1),
      dec2bcd(t.tm_year % 100),
  };

  BusLock lock;
  uint8_t status;
  if (!lock.ok || !write_regs(REG_TIME, r, 7) || !read_regs(REG_STATUS, &status, 1))
    return false;
  status &= ~STAT_OSF;
  return write_regs(REG_STATUS, &status, 1);
}

bool rtc_set_aging(int8_t offset)
{
  BusLock lock;
  uint8_t v = (uint8_t)offset, ctrl, status;
  if (!lock.ok || !write_regs(REG_AGING, &v, 1))
    return false;
  // Force a temperature conversion so the new offset applies immediately.
  if (read_regs(REG_STATUS, &status, 1) && !(status & STAT_BSY) && read_regs(REG_CTRL, &ctrl, 1))
  {
    ctrl |= CTRL_CONV;
    write_regs(REG_CTRL, &ctrl, 1);
  }
  return true;
}

bool rtc_get_aging(int8_t &offset)
{
  BusLock lock;
  uint8_t v;
  if (!lock.ok || !read_regs(REG_AGING, &v, 1))
    return false;
  offset = (int8_t)v;
  return true;
}

bool rtc_read_temperature(float &celsius)
{
  BusLock lock;
  uint8_t r[2];
  if (!lock.ok || !read_regs(REG_TEMP, r, 2))
    return false;
  celsius = (int8_t)r[0] + (r[1] >> 6) * 0.25f;
  return true;
}
