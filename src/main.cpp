#include <Arduino.h>
#include <Wire.h>

#define DS3231_I2C_ADDRESS 0x68

// Pin definitions
#define PIN_MOTOR_A   3   // motor coil A
#define PIN_MOTOR_B   12  // motor coil B
#define PIN_INDICATOR 13  // LED indicator
#define PIN_AUTO_SW   5   // auto/manual switch (LOW = auto, connect to GND)
#define PIN_BTN_ADV   6   // manual advance button (LOW = pressed, connect to GND)
#define PIN_BTN_STEP  7   // single step button   (LOW = pressed, connect to GND)

int pulse_fired = 0;   // 1 = motor already pulsed this minute
int polarity    = 0;   // last pulse polarity: 0 = coil A, 1 = coil B
int last_sec    = 0;   // last observed RTC second

byte decToBcd(byte val) {
  return ((val / 10 * 16) + (val % 10));
}

byte bcdToDec(byte val) {
  return ((val / 16 * 10) + (val % 16));
}

// Write full time to DS3231 (year is 2-digit, e.g. 26 for 2026)
void setDS3231time(byte second, byte minute, byte hour,
                   byte dayOfWeek, byte dayOfMonth, byte month, byte year) {
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0);  // start at register 0 (seconds)
  Wire.write(decToBcd(second));
  Wire.write(decToBcd(minute));
  Wire.write(decToBcd(hour));
  Wire.write(decToBcd(dayOfWeek));
  Wire.write(decToBcd(dayOfMonth));
  Wire.write(decToBcd(month));
  Wire.write(decToBcd(year));
  Wire.endTransmission();
}

// Read only seconds from DS3231, drain remaining bytes to keep bus clean
void readDS3231time(byte *second) {
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0);
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 7);
  *second = bcdToDec(Wire.read() & 0x7f);
  for (int i = 0; i < 6; i++) Wire.read();  // drain remaining 6 bytes
}

void setup() {
  Wire.begin();
  pinMode(PIN_MOTOR_A,   OUTPUT);
  pinMode(PIN_MOTOR_B,   OUTPUT);
  pinMode(PIN_INDICATOR, OUTPUT);
  pinMode(PIN_AUTO_SW,   INPUT_PULLUP);
  pinMode(PIN_BTN_ADV,   INPUT_PULLUP);
  pinMode(PIN_BTN_STEP,  INPUT_PULLUP);

  // To set RTC time: uncomment the line below, upload once, then comment it out again.
  setDS3231time(0, 0, 12, 4, 5, 3, 26);  // 12:00:00, Wed, Mar 5, 2026
}

// Pulse motor one step, alternating polarity (used for manual advance)
void aut_posun() {
  if (polarity == 0) {
    digitalWrite(PIN_MOTOR_A, HIGH);
    digitalWrite(PIN_MOTOR_B, LOW);
    digitalWrite(PIN_INDICATOR, HIGH);
    delay(700);
    digitalWrite(PIN_MOTOR_A, LOW);
    digitalWrite(PIN_INDICATOR, LOW);
    polarity = 1;
  } else {
    digitalWrite(PIN_MOTOR_B, HIGH);
    digitalWrite(PIN_INDICATOR, HIGH);
    delay(700);
    digitalWrite(PIN_MOTOR_B, LOW);
    digitalWrite(PIN_INDICATOR, LOW);
    polarity = 0;
  }
}

// Automatic minute tick: fires motor pulse at second == 0, alternating polarity
void minuta() {
  byte second;
  readDS3231time(&second);

  if (last_sec != second) {
    digitalWrite(PIN_INDICATOR, HIGH);
    delay(20);
    digitalWrite(PIN_INDICATOR, LOW);
    last_sec = second;
  }

  if (second == 58) {
    pulse_fired = 0;
  }

  if (second == 0 && pulse_fired == 0 && polarity == 0) {
    digitalWrite(PIN_MOTOR_A, HIGH);
    digitalWrite(PIN_MOTOR_B, LOW);
    digitalWrite(PIN_INDICATOR, HIGH);
    delay(700);
    digitalWrite(PIN_MOTOR_A, LOW);
    digitalWrite(PIN_MOTOR_B, LOW);
    digitalWrite(PIN_INDICATOR, LOW);
    pulse_fired = 1;
    polarity = 1;
  } else if (second == 0 && pulse_fired == 0 && polarity == 1) {
    digitalWrite(PIN_MOTOR_B, HIGH);
    digitalWrite(PIN_INDICATOR, HIGH);
    delay(700);
    digitalWrite(PIN_MOTOR_B, LOW);
    digitalWrite(PIN_INDICATOR, LOW);
    pulse_fired = 1;
    polarity = 0;
  }
}

// Single step pulse, alternating polarity
void tlacitko() {
  if (polarity == 0) {
    digitalWrite(PIN_MOTOR_A, HIGH);
    digitalWrite(PIN_MOTOR_B, LOW);
    digitalWrite(PIN_INDICATOR, HIGH);
    delay(700);
    digitalWrite(PIN_MOTOR_A, LOW);
    digitalWrite(PIN_MOTOR_B, LOW);
    digitalWrite(PIN_INDICATOR, LOW);
    polarity = 1;
  } else {
    digitalWrite(PIN_MOTOR_B, HIGH);
    digitalWrite(PIN_INDICATOR, HIGH);
    delay(700);
    digitalWrite(PIN_MOTOR_B, LOW);
    digitalWrite(PIN_INDICATOR, LOW);
    polarity = 0;
  }
  delay(50);  // debounce
}

void loop() {
  if (digitalRead(PIN_AUTO_SW) == LOW) {
    minuta();
  } else if (digitalRead(PIN_BTN_ADV) == LOW) {
    aut_posun();
    delay(50);  // debounce
  } else if (digitalRead(PIN_BTN_STEP) == LOW) {
    tlacitko();
    setDS3231time(0, 0, 12, 4, 5, 3, 26);  // 12:00:00, Wed, Mar 5, 2026
  }

  delay(50);
}
