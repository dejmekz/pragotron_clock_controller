#include <Arduino.h>
#include <Wire.h>

#define DS3231_I2C_ADDRESS 0x68

// Pin definitions
#define PIN_MOTOR_A 3    // motor coil A
#define PIN_MOTOR_B 12   // motor coil B
#define PIN_INDICATOR 13 // LED indicator
#define PIN_AUTO_SW 5    // auto/manual switch (LOW = auto, connect to GND)
#define PIN_BTN_ADV 6    // manual advance button (LOW = pressed, connect to GND)
#define PIN_BTN_STEP 7   // single step button   (LOW = pressed, connect to GND)

// Timing constants (milliseconds)
const unsigned long PULSE_MS  = 700; // motor coil energise time
const unsigned long PAUSE_MS  = 300; // inter-pulse pause for manual advance

// Pulse state machine
enum PulseState { IDLE, PULSE_ACTIVE, PAUSE_AFTER };
PulseState pulse_state    = IDLE;
unsigned long pulse_start = 0;
bool          pause_after = false; // true when a post-pulse pause is needed

bool pulse_fired = false; // motor already pulsed this minute
bool polarity    = false; // false = coil A next, true = coil B next
int  last_sec    = -1;    // last observed RTC second

// ── DS3231 helpers ─────────────────────────────────────────────────────────

byte decToBcd(byte val) { return (val / 10 * 16) + (val % 10); }
byte bcdToDec(byte val) { return (val / 16 * 10) + (val % 16); }

void setDS3231time(byte second, byte minute, byte hour,
                   byte dayOfWeek, byte dayOfMonth, byte month, byte year)
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0);
  Wire.write(decToBcd(second));
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.write(decToBcd(dayOfWeek));
  Wire.write(decToBcd(dayOfMonth));
  Wire.write(decToBcd(month));
  Wire.write(decToBcd(year));
  Wire.endTransmission();
}

void readDS3231time(byte *second)
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0);
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 7);
  *second = bcdToDec(Wire.read() & 0x7f);
  for (int i = 0; i < 6; i++) Wire.read(); // drain remaining bytes
}

// ── Non-blocking motor pulse ────────────────────────────────────────────────

// Start a single motor pulse. Pass with_pause=true for manual advance.
void start_pulse(bool high, bool with_pause)
{
  if (high) {
    digitalWrite(PIN_MOTOR_A, LOW);
    digitalWrite(PIN_MOTOR_B, HIGH);
  } else {
    digitalWrite(PIN_MOTOR_A, HIGH);
    digitalWrite(PIN_MOTOR_B, LOW);
  }
  digitalWrite(PIN_INDICATOR, HIGH);
  pulse_start = millis();
  pulse_state = PULSE_ACTIVE;
  pause_after = with_pause;
}

// Advance the pulse state machine — call every loop iteration.
void update_pulse()
{
  if (pulse_state == IDLE) return;

  unsigned long now     = millis();
  unsigned long elapsed = now - pulse_start;

  if (pulse_state == PULSE_ACTIVE) {
    if (elapsed >= PULSE_MS) {
      digitalWrite(PIN_MOTOR_A, LOW);
      digitalWrite(PIN_MOTOR_B, LOW);
      digitalWrite(PIN_INDICATOR, LOW);
      if (pause_after) {
        pulse_state = PAUSE_AFTER;
        pulse_start = now;
      } else {
        pulse_state = IDLE;
      }
    }
  } else if (pulse_state == PAUSE_AFTER) {
    if (elapsed >= PAUSE_MS) {
      pulse_state = IDLE;
    }
  }
}

bool pulse_busy() { return pulse_state != IDLE; }

// ── Arduino entry points ────────────────────────────────────────────────────

void setup()
{
  Wire.begin();
  pinMode(PIN_MOTOR_A,    OUTPUT);
  pinMode(PIN_MOTOR_B,    OUTPUT);
  pinMode(PIN_INDICATOR,  OUTPUT);
  pinMode(PIN_AUTO_SW,    INPUT);
  pinMode(PIN_BTN_ADV,    INPUT);
  pinMode(PIN_BTN_STEP,   INPUT);

  // To set RTC time: uncomment, upload once, then comment out again.
  // setDS3231time(0, 0, 12, 4, 5, 3, 26);  // 12:00:00, Wed, Mar 5, 2026
}

void loop()
{
  update_pulse(); // always advance pulse state machine first

  if (digitalRead(PIN_AUTO_SW) == LOW)
  {
    // ── Automatic mode: pulse motor once per minute at second == 0 ──
    byte second;
    readDS3231time(&second);

    if ((int)second != last_sec) last_sec = (int)second;

    if (second == 58) pulse_fired = false;

    if (second == 0 && !pulse_fired && !pulse_busy()) {
      start_pulse(polarity, false);
      polarity    = !polarity;
      pulse_fired = true;
    }
  }
  else if (digitalRead(PIN_BTN_ADV) == HIGH && !pulse_busy())
  {
    // ── Manual advance: one step with post-pulse pause ──
    start_pulse(polarity, true);
    polarity = !polarity;
  }
  else if (digitalRead(PIN_BTN_STEP) == HIGH && !pulse_busy())
  {
    // ── Single step + reset RTC ──
    start_pulse(polarity, false);
    polarity = !polarity;
    setDS3231time(0, 0, 0, 1, 1, 1, 26);
  }
}
