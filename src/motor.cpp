#include "motor.h"
#include "config.h"

enum class State
{
  Idle,
  Pulse,
  Pause
};

static State    state = State::Idle;
static uint32_t state_start = 0;

static void coils_off()
{
  digitalWrite(PIN_MOTOR_A, LOW);
  digitalWrite(PIN_MOTOR_B, LOW);
}

void motor_begin()
{
  pinMode(PIN_MOTOR_A, OUTPUT);
  pinMode(PIN_MOTOR_B, OUTPUT);
  coils_off();
}

void motor_pulse(bool polarity)
{
  if (state != State::Idle)
    return;
  // Never drive both H-bridge inputs high at the same time.
  digitalWrite(polarity ? PIN_MOTOR_A : PIN_MOTOR_B, LOW);
  digitalWrite(polarity ? PIN_MOTOR_B : PIN_MOTOR_A, HIGH);
  state = State::Pulse;
  state_start = millis();
}

void motor_update()
{
  uint32_t elapsed = millis() - state_start;
  if (state == State::Pulse && elapsed >= PULSE_MS)
  {
    coils_off();
    state = State::Pause;
    state_start = millis();
  }
  else if (state == State::Pause && elapsed >= PAUSE_MS)
  {
    state = State::Idle;
  }
}

bool motor_busy() { return state != State::Idle; }
bool motor_energised() { return state == State::Pulse; }
