#include "net_sync.h"
#include "config.h"
#include "ds3231.h"
#include "storage.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_timer.h>
#include <math.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

struct SyncStatus
{
  const char *last_result = "pending";
  time_t   last_success = 0;    // UTC
  int32_t  last_offset_ms = 0;  // RTC minus NTP before correction
  bool     offset_valid = false;
  float    drift_ppm = NAN;     // + = RTC runs fast
  uint32_t next_due_ms = 0;
};

static SyncStatus status;
static portMUX_TYPE status_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t task = nullptr;

static void set_result(const char *r)
{
  portENTER_CRITICAL(&status_mux);
  status.last_result = r;
  portEXIT_CRITICAL(&status_mux);
}

// ── WiFi ───────────────────────────────────────────────────────────────────

static void wifi_off()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

static bool wifi_connect()
{
  String ssid = load_wifi_ssid(WIFI_SSID);
  String pass = load_wifi_pass(WIFI_PASSWORD);
  if (ssid.isEmpty())
    return false;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("pragotron");
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS)
      return false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  Serial.printf("[sync] WiFi connected, IP %s, RSSI %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

// ── NTP ────────────────────────────────────────────────────────────────────

static const uint32_t NTP_UNIX_OFFSET = 2208988800UL; // 1900 → 1970

static int64_t ntp_ts_us(const uint8_t *p)
{
  uint32_t sec  = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
  uint32_t frac = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
  // Era 0 ends in 2036; timestamps with the top bit clear belong to era 1.
  int64_t s = (sec & 0x80000000UL) ? (int64_t)sec - NTP_UNIX_OFFSET
                                   : (int64_t)sec + 0x100000000LL - NTP_UNIX_OFFSET;
  return s * 1000000LL + (((uint64_t)frac * 1000000ULL) >> 32);
}

// On success returns the Unix time in µs that corresponds to the local
// esp_timer value stored in local_us.
static bool ntp_query(const char *host, int64_t &unix_us, int64_t &local_us)
{
  IPAddress ip;
  if (!WiFi.hostByName(host, ip))
    return false;

  WiFiUDP udp;
  if (!udp.begin(0))
    return false;

  uint8_t pkt[48] = {0};
  pkt[0] = 0x23; // LI=0, VN=4, Mode=3 (client)

  int64_t t1 = esp_timer_get_time();
  udp.beginPacket(ip, 123);
  udp.write(pkt, sizeof(pkt));
  udp.endPacket();

  bool ok = false;
  while (esp_timer_get_time() - t1 < (int64_t)NTP_TIMEOUT_MS * 1000)
  {
    if (udp.parsePacket() >= 48)
    {
      int64_t t4 = esp_timer_get_time();
      udp.read(pkt, sizeof(pkt));
      uint8_t li = pkt[0] >> 6, mode = pkt[0] & 7, stratum = pkt[1];
      if (li == 3 || mode != 4 || stratum == 0 || stratum > 15)
        break;
      int64_t T2 = ntp_ts_us(pkt + 32); // server receive
      int64_t T3 = ntp_ts_us(pkt + 40); // server transmit
      int64_t delay = (t4 - t1) - (T3 - T2);
      unix_us  = T3 + delay / 2;
      local_us = t4;
      ok = true;
      break;
    }
    vTaskDelay(1);
  }
  udp.stop();
  return ok;
}

// ── Sync ───────────────────────────────────────────────────────────────────

// Wait (mostly sleeping) until esp_timer reaches the given value.
static void wait_until_us(int64_t target)
{
  int64_t remain;
  while ((remain = target - esp_timer_get_time()) > 3000)
    vTaskDelay(pdMS_TO_TICKS((remain - 2000) / 1000));
  while (esp_timer_get_time() < target)
    ;
}

static bool do_sync()
{
  if (!wifi_connect())
  {
    set_result(load_wifi_ssid(WIFI_SSID).isEmpty() ? "no WiFi configured" : "WiFi connect failed");
    wifi_off();
    return false;
  }

  int64_t ntp_us = 0, ref_us = 0;
  const char *server = nullptr;
  for (const char *s : NTP_SERVERS)
    if (ntp_query(s, ntp_us, ref_us)) { server = s; break; }
  wifi_off(); // all further work is local

  if (!server)
  {
    set_result("NTP failed");
    return false;
  }
  if (ntp_us < (int64_t)utc_from_fields(2024, 1, 1, 0, 0, 0) * 1000000LL)
  {
    set_result("NTP time implausible");
    return false;
  }
  auto ntp_now = [&](int64_t local) { return ntp_us + (local - ref_us); };

  // Measure the RTC error by catching its next second transition.
  bool offset_valid = false;
  int32_t offset_ms = 0;
  RtcTime first, cur;
  if (rtc_read(first) && !first.osf)
  {
    int64_t deadline = esp_timer_get_time() + 1100000;
    while (esp_timer_get_time() < deadline)
    {
      if (rtc_read(cur) && cur.utc != first.utc)
      {
        int64_t edge = esp_timer_get_time();
        offset_ms = (int32_t)(((int64_t)cur.utc * 1000000LL - ntp_now(edge)) / 1000);
        offset_valid = true;
        break;
      }
      vTaskDelay(1);
    }
  }

  // Write the RTC exactly at the next NTP second boundary (writing the
  // seconds register restarts the DS3231 countdown chain).
  int64_t target_sec = ntp_now(esp_timer_get_time()) / 1000000LL + 1;
  wait_until_us(ref_us + (target_sec * 1000000LL - ntp_us));
  if (!rtc_write((time_t)target_sec))
  {
    set_result("RTC write failed");
    return false;
  }

  // Drift since the previous trusted set → suggested aging offset change.
  float ppm = NAN;
  int64_t prev = load_rtc_last_set();
  if (offset_valid && load_rtc_trusted() && prev > 0 && target_sec - prev > 6 * 3600)
    ppm = offset_ms * 1000.0f / (float)(target_sec - prev);
  save_rtc_set(true, target_sec);

  portENTER_CRITICAL(&status_mux);
  status.last_result = "OK";
  status.last_success = (time_t)target_sec;
  status.offset_valid = offset_valid;
  status.last_offset_ms = offset_ms;
  if (!isnan(ppm))
    status.drift_ppm = ppm;
  portEXIT_CRITICAL(&status_mux);

  Serial.printf("[sync] RTC set from %s", server);
  if (offset_valid)
    Serial.printf(", RTC was off by %+ld ms", (long)offset_ms);
  if (!isnan(ppm))
    Serial.printf(", drift %+.2f ppm (suggest changing aging by %+d)", ppm, (int)lroundf(ppm * 10));
  Serial.println();
  return true;
}

static void sync_task(void *)
{
  uint32_t retry_s = SYNC_RETRY_MIN_S;
  uint32_t due = millis() + SYNC_FIRST_DELAY_S * 1000;
  status.next_due_ms = due;

  for (;;)
  {
    int32_t wait = (int32_t)(due - millis());
    bool forced = ulTaskNotifyTake(pdTRUE, wait > 0 ? pdMS_TO_TICKS(wait) : 0) > 0;
    if (!forced && (int32_t)(due - millis()) > 0)
      continue;

    set_result("running");
    Serial.println("[sync] starting");
    if (do_sync())
    {
      due = millis() + SYNC_INTERVAL_S * 1000;
      retry_s = SYNC_RETRY_MIN_S;
    }
    else
    {
      Serial.printf("[sync] failed: %s, retry in %lu min\n", status.last_result, (unsigned long)(retry_s / 60));
      due = millis() + retry_s * 1000;
      retry_s = min(retry_s * 2, SYNC_RETRY_MAX_S);
    }
    portENTER_CRITICAL(&status_mux);
    status.next_due_ms = due;
    portEXIT_CRITICAL(&status_mux);
  }
}

void netsync_begin()
{
  WiFi.mode(WIFI_OFF);
  // Same priority as loopTask but almost always blocked; pinned to core 0
  // (the WiFi core) so loop() on core 1 keeps its timing.
  xTaskCreatePinnedToCore(sync_task, "netsync", 6144, nullptr, 1, &task, 0);
}

void netsync_request()
{
  if (task)
    xTaskNotifyGive(task);
}

void netsync_print_status(Print &out)
{
  portENTER_CRITICAL(&status_mux);
  SyncStatus s = status;
  portEXIT_CRITICAL(&status_mux);

  out.printf("Sync:     %s", s.last_result);
  if (s.last_success)
  {
    struct tm t;
    localtime_r(&s.last_success, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &t);
    out.printf(", last OK %s", buf);
  }
  int32_t in = (int32_t)(s.next_due_ms - millis());
  out.printf(", next in %ld min\n", (long)(in > 0 ? in / 60000 : 0));
  if (s.offset_valid)
    out.printf("RTC err:  %+ld ms before last sync", (long)s.last_offset_ms);
  if (!isnan(s.drift_ppm))
    out.printf(", drift %+.2f ppm (suggest changing aging by %+d)", s.drift_ppm, (int)lroundf(s.drift_ppm * 10));
  if (s.offset_valid)
    out.println();
}
