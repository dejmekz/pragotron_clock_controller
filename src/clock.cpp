#include "clock.h"
#include "config.h"
#include "ds3231.h"
#include "input.h"
#include "motor.h"
#include "storage.h"

static DebouncedInput auto_sw(PIN_AUTO_SW, LOW);
static DebouncedInput btn_adv(PIN_BTN_ADV, HIGH);
static DebouncedInput btn_step(PIN_BTN_STEP, HIGH);

static DialState dial = {0, false};
static bool      dial_known = false;

static bool     rtc_ok = false;
static RtcTime  rtc = {0, true};
static int      target = -1;     // dial position matching the RTC, -1 = unknown
static uint32_t last_poll = 0;
static int      free_run_steps = 0; // minute ticks owed while the RTC time is invalid

static bool     blink_active = false;
static uint32_t blink_start = 0;

static int      step_press_target = -1; // RTC target when STEP was pressed
static bool     step_long_done = false;
static bool     step_pending = false;

// ── Helpers ────────────────────────────────────────────────────────────────

static int dial_position_for(time_t utc)
{
  struct tm lt;
  localtime_r(&utc, &lt);
  return (lt.tm_hour % (DIAL_MINUTES / 60)) * 60 + lt.tm_min;
}

static void format_position(char *buf, size_t len, int pos)
{
  snprintf(buf, len, "%02d:%02d", pos / 60, pos % 60);
}

static void apply_tz(const String &tz)
{
  setenv("TZ", tz.c_str(), 1);
  tzset();
}

// Minutes the dial must advance to reach the target; 0 when in step or when
// the dial is slightly ahead (it then simply waits, e.g. after DST ends).
static int steps_needed()
{
  if (!dial_known || target < 0 || rtc.osf)
    return 0;
  int diff = (target - dial.position + DIAL_MINUTES) % DIAL_MINUTES;
  if (diff == 0 || DIAL_MINUTES - diff <= HOLD_MAX_MIN)
    return 0;
  return diff;
}

static void step()
{
  motor_pulse(dial.polarity);
  dial.polarity = !dial.polarity;
  dial.position = (dial.position + 1) % DIAL_MINUTES;
  save_dial(dial);
}

// ── RTC polling ────────────────────────────────────────────────────────────

static void poll_rtc(uint32_t now)
{
  if (now - last_poll < RTC_POLL_MS)
    return;
  last_poll = now;

  RtcTime r;
  bool ok = rtc_read(r);
  if (ok != rtc_ok)
    Serial.println(ok ? "[rtc] OK" : "[rtc] read failed");
  rtc_ok = ok;
  if (!ok)
  {
    target = -1;
    return;
  }

  if (r.utc != rtc.utc)
  {
    // RTC time invalid (oscillator stopped): the date/time is junk, so just
    // keep the dial running one step per minute until it is set again.
    if (r.osf && rtc.utc != 0 && r.utc / 60 != rtc.utc / 60)
      free_run_steps++;

    // New second: refresh target and blink the indicator.
    rtc = r;
    target = dial_position_for(r.utc);
    blink_active = true;
    blink_start = now;

    if (!dial_known)
    {
      // First boot: assume the dial shows the current time. Correct it with
      // a STEP long-press or the `dial` console command.
      dial.position = target;
      dial_known = true;
      save_dial(dial);
      char buf[8];
      format_position(buf, sizeof(buf), target);
      Serial.printf("[clock] dial position unknown, assuming %s\n", buf);
    }
  }
  rtc.osf = r.osf;
}

// ── STEP long-press: "the dial is showing the correct time now" ────────────

static void declare_dial_correct()
{
  char buf[8];
  bool trusted = rtc_ok && !rtc.osf && load_rtc_trusted();
  if (trusted && step_press_target >= 0)
  {
    // RTC is NTP/console synced → trust it, adopt its time for the dial.
    dial.position = step_press_target;
    dial_known = true;
    save_dial(dial);
    format_position(buf, sizeof(buf), dial.position);
    Serial.printf("[clock] dial set to %s from RTC\n", buf);
    return;
  }

  // RTC not trustworthy → trust the dial. The user pressed STEP exactly when
  // the minute shown on the dial began, so the RTC gets dial:mm + hold time.
  time_t base = rtc_ok ? rtc.utc : utc_from_fields(2026, 1, 1, 12, 0, 0);
  struct tm lt;
  localtime_r(&base, &lt);
  int half = (DIAL_MINUTES == 720 && lt.tm_hour >= 12) ? 12 : 0;
  lt.tm_hour = half + dial.position / 60;
  lt.tm_min = dial.position % 60;
  lt.tm_sec = LONG_PRESS_MS / 1000;
  lt.tm_isdst = -1;
  time_t utc = mktime(&lt);
  if (rtc_write(utc))
  {
    save_rtc_set(false, utc);
    rtc.utc = 0; // force refresh on next poll
    format_position(buf, sizeof(buf), dial.position);
    Serial.printf("[clock] RTC set from dial: %s:%02d\n", buf, (int)(LONG_PRESS_MS / 1000));
  }
  else
    Serial.println("[clock] RTC write failed");
}

// ── Manual mode ────────────────────────────────────────────────────────────

static void manual_mode(uint32_t now, bool step_changed)
{
  if (step_changed)
  {
    if (btn_step.active())
    {
      step_press_target = target;
      step_long_done = false;
    }
    else if (!step_long_done)
      step_pending = true; // short press → single step on release
  }

  if (btn_step.active() && !step_long_done && now - btn_step.since() >= LONG_PRESS_MS)
  {
    step_long_done = true;
    declare_dial_correct();
  }

  if (motor_busy())
    return;
  if (step_pending)
  {
    step_pending = false;
    step();
  }
  else if (btn_adv.active())
    step(); // held → continuous advance, one step per PULSE_MS + PAUSE_MS
}

// ── Indicator LED ──────────────────────────────────────────────────────────

static void update_indicator(uint32_t now, bool automatic)
{
  bool on;
  if (motor_energised())
    on = true;
  else if (!rtc_ok)
    on = (now / 125) & 1; // fast blink: RTC fault
  else
  {
    if (blink_active && now - blink_start >= BLINK_MS)
      blink_active = false;
    on = automatic && blink_active;
  }
  digitalWrite(PIN_INDICATOR, on);
}

// ── Public ─────────────────────────────────────────────────────────────────

void clock_begin()
{
  motor_begin();
  pinMode(PIN_INDICATOR, OUTPUT);
  digitalWrite(PIN_INDICATOR, LOW);
  auto_sw.begin();
  btn_adv.begin();
  btn_step.begin();

  apply_tz(load_tz(DEFAULT_TZ));

  dial_known = load_dial(dial);
  if (dial.position >= DIAL_MINUTES)
    dial_known = false;

  if (!rtc_begin(PIN_I2C_SDA, PIN_I2C_SCL))
    Serial.println("[rtc] DS3231 not responding");
  int8_t aging = load_aging(DS3231_AGING_DEFAULT);
  rtc_set_aging(aging);

  char buf[8];
  format_position(buf, sizeof(buf), dial.position);
  Serial.printf("[clock] dial %s (%s), next polarity %c, aging %d\n",
                dial_known ? buf : "--:--", dial_known ? "stored" : "unknown",
                dial.polarity ? 'B' : 'A', aging);
}

void clock_update()
{
  uint32_t now = millis();
  motor_update();

  auto_sw.update(now, DEBOUNCE_MS);
  btn_adv.update(now, DEBOUNCE_MS);
  bool step_changed = btn_step.update(now, DEBOUNCE_MS);

  poll_rtc(now);

  bool automatic = auto_sw.active();
  if (automatic)
  {
    step_pending = false;
    if (rtc_ok && !motor_busy())
    {
      if (rtc.osf && free_run_steps > 0)
      {
        free_run_steps--;
        step();
      }
      else if (steps_needed() > 0)
        step();
    }
  }
  else
  {
    free_run_steps = 0;
    manual_mode(now, step_changed);
  }

  update_indicator(now, automatic);
}

void clock_print_status(Print &out)
{
  char buf[40];
  if (rtc_ok)
  {
    struct tm t;
    gmtime_r(&rtc.utc, &t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    out.printf("RTC UTC:  %s%s%s\n", buf, rtc.osf ? "  (oscillator stopped - not valid)" : "",
               load_rtc_trusted() ? "" : "  (not synced)");
    localtime_r(&rtc.utc, &t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &t);
    out.printf("Local:    %s\n", buf);
    float temp;
    if (rtc_read_temperature(temp))
      out.printf("RTC temp: %.2f C\n", temp);
  }
  else
    out.println("RTC:      not responding");

  int8_t aging;
  if (rtc_get_aging(aging))
    out.printf("Aging:    %d\n", aging);

  format_position(buf, sizeof(buf), dial.position);
  out.printf("Dial:     %s%s, next polarity %c\n", dial_known ? buf : "--:--",
             dial_known ? "" : " (unknown)", dial.polarity ? 'B' : 'A');
  if (target >= 0)
  {
    format_position(buf, sizeof(buf), target);
    int diff = (target - dial.position + DIAL_MINUTES) % DIAL_MINUTES;
    out.printf("Target:   %s", buf);
    if (rtc.osf)
      out.print(" (RTC time invalid: free-running, set the time)");
    else if (diff && steps_needed())
      out.printf(" (catching up %d min)", diff);
    else if (diff)
      out.printf(" (dial ahead, waiting %d min)", DIAL_MINUTES - diff);
    out.println();
  }
  out.printf("Mode:     %s\n", auto_sw.active() ? "automatic" : "manual");
}

bool clock_set_dial(int hour, int minute)
{
  if (hour < 0 || minute < 0 || minute > 59)
    return false;
  dial.position = (hour * 60 + minute) % DIAL_MINUTES;
  dial_known = true;
  save_dial(dial);
  return true;
}

void clock_flip_polarity()
{
  dial.polarity = !dial.polarity;
  save_dial(dial);
}

bool clock_set_local_time(struct tm &local)
{
  local.tm_isdst = -1;
  time_t utc = mktime(&local);
  if (utc < 0 || !rtc_write(utc))
    return false;
  save_rtc_set(true, utc);
  rtc.utc = 0;
  return true;
}

bool clock_set_tz(const String &tz)
{
  if (tz.isEmpty())
    return false;
  save_tz(tz);
  apply_tz(tz);
  rtc.utc = 0; // recompute target
  return true;
}

bool clock_set_aging(int value)
{
  if (value < -128 || value > 127 || !rtc_set_aging((int8_t)value))
    return false;
  save_aging((int8_t)value);
  return true;
}
