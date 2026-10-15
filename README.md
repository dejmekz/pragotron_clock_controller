# Pragotron clock controller (ESP32-S3)

Drives a Pragotron (or any polarised 1-minute-impulse) slave clock from an
ESP32-S3 and a DS3231 RTC. It has the following features:

* **One alternating-polarity pulse per minute**, exactly at second 0.
* **WiFi is optional.** Once a day the controller joins WiFi, reads NTP time,
  corrects the DS3231 to within a few milliseconds and turns WiFi off again.
  Without WiFi the clock keeps running on the DS3231 (±2 ppm ≈ 1 min/year).
* **It remembers the dial position** in flash. After a power cut it
  fast-forwards the dial (1 step/s) to the correct time.
* **DST is automatic** through a POSIX time zone (default Europe/Prague). In
  spring the dial jumps 60 steps forward. In autumn the clock waits 60 minutes.
* **Drift measurement:** each sync logs how far the RTC had drifted and
  suggests a DS3231 aging-offset correction.
* **Serial console** for status and configuration (115200 Bd).

## Wiring

The board is an **Arduino Nano ESP32** (ESP32-S3 in the Nano footprint). It
plugs into the same socket as the original Arduino Nano, and all pins stay
the same:

| Function            | Nano pin  | Notes                                 |
|---------------------|-----------|---------------------------------------|
| I²C SDA / SCL       | A4 / A5   | DS3231                                |
| Motor coil A / B    | D3 / D12  | motor driver inputs                   |
| Indicator LED       | D13       | also the on-board LED                 |
| Auto/manual switch  | D5        | LOW (to GND) = automatic              |
| ADVANCE button      | D6        | HIGH = pressed                        |
| STEP button         | D7        | HIGH = pressed                        |

Pins are set in [src/config.h](src/config.h). The internal pull-up (D5) and
pull-downs (D6, D7) are enabled. They work alongside any existing external
resistors.

> ⚠️ **The ESP32-S3 uses 3.3 V logic and its pins are not 5 V tolerant.**
> On the old 5 V Nano, check that nothing drives a pin to 5 V:
> * **Buttons (D6, D7)** must switch to **3V3**, not 5V.
> * The **switch pull-up on D5** must go to 3V3, or be removed (the internal
>   pull-up is enough).
> * The **DS3231 module** must be powered from **3V3**. Its on-board I²C
>   pull-ups connect to the module's VCC.
>
> Motor driver inputs (e.g. L298N) accept 3.3 V as HIGH. The Nano ESP32 VIN
> accepts 6–21 V. Its 5V pin only outputs power when USB is connected.
>
> The ZS-042 DS3231 board has a charging circuit meant for an LIR2032 cell.
> If you use a CR2032, remove the diode or resistor next to the battery
> holder.

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.h` and enter your WiFi
   details. Alternatively, set them later with the `ssid` and `pass` console
   commands.
2. Build and flash: `pio run -t upload && pio device monitor`. If the upload
   is not detected, double-tap RESET to enter the bootloader.
3. The first boot assumes the dial already shows the correct time. If it
   doesn't, tell the controller what the dial shows: `dial 10:23`. It then
   catches up by itself.

## Operation

**Automatic mode** (switch closed): the dial follows local time. If the dial
is behind, it advances. If it is up to `HOLD_MAX_MIN` (75) minutes ahead, the
controller waits.

**Manual mode** (switch open):

* **ADVANCE** held: the dial steps continuously, once per second.
* **STEP** short press: the dial steps once.
* **STEP held for 3 s** means "the dial shows the correct time now":
  * If the RTC was synced (by NTP or the `time` command), the controller adopts
    the RTC time as the dial position.
  * If not (no WiFi ever, or the RTC battery died), the dial sets the RTC
    instead. Step the dial to the *next* minute and press STEP exactly when
    that minute begins. This is the old "set the seconds" workflow, and it
    works fully offline.

Every manual step is counted, so the stored dial position stays correct.
Switch back to automatic when done.

**LED:** a short blink every second means automatic mode is running. The LED
stays lit during a pulse. Fast blinking means the DS3231 is not responding.

### Console commands

```
status                     RTC/local time, dial, target, sync state, drift
dial HH:MM                 what the dial physically shows right now
flip                       swap next pulse polarity (if one step was ignored)
time YYYY-MM-DD HH:MM:SS   set RTC from local time
sync                       sync via WiFi/NTP now
ssid <name> / pass <pw>    WiFi credentials (stored in flash)
tz <POSIX TZ>              time zone
aging <n>                  DS3231 aging offset (+ slows the clock, 0.1 ppm/LSB)
restart
```

## How it works

| File | Role |
|------|------|
| `clock.cpp`    | Dial tracking, catch-up/hold logic, buttons, LED |
| `motor.cpp`    | Non-blocking pulse + pause state machine |
| `ds3231.cpp`   | RTC driver (mutex-protected, stores UTC, OSF handling) |
| `net_sync.cpp` | Background task: WiFi on → NTP → RTC write → WiFi off |
| `storage.cpp`  | NVS persistence (dial, polarity, TZ, aging, WiFi) |
| `console.cpp`  | Serial commands |

The DS3231 stores **UTC**, so a DST change never alters the RTC. Local time
comes from the TZ rule. The sync task runs on core 0 and the motor loop runs
on core 1, so a slow WiFi connection never delays a pulse. If the DS3231
reports *oscillator stopped*, its time is not trusted. The dial then
free-runs one step per minute until the time is set by NTP, by the console,
or by a STEP long-press.

Retry schedule: the first sync runs 10 s after boot, then every 24 h. A
failed sync is retried after 15 min, then 30 min, and so on up to 6 h.
