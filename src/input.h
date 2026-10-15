#pragma once
#include <Arduino.h>

// Debounced digital input. The internal pull resistor matches the idle level
// (pull-up for active-LOW, pull-down for active-HIGH); it simply sits in
// parallel with any external resistor already on the board.
class DebouncedInput
{
public:
  DebouncedInput(uint8_t pin, uint8_t active_level) : pin_(pin), level_(active_level) {}

  void begin()
  {
    pinMode(pin_, level_ == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    raw_ = stable_ = read();
    changed_at_ = millis();
  }

  // Returns true when the debounced state changed in this call.
  bool update(uint32_t now, uint32_t debounce_ms)
  {
    bool raw = read();
    if (raw != raw_)
    {
      raw_ = raw;
      changed_at_ = now;
    }
    if (raw_ != stable_ && now - changed_at_ >= debounce_ms)
    {
      stable_ = raw_;
      since_ = now;
      return true;
    }
    return false;
  }

  bool active() const { return stable_; }
  uint32_t since() const { return since_; } // time of last stable change

private:
  bool read() const { return digitalRead(pin_) == level_; }

  uint8_t  pin_;
  uint8_t  level_;
  bool     raw_ = false;
  bool     stable_ = false;
  uint32_t changed_at_ = 0;
  uint32_t since_ = 0;
};
