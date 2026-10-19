# Bike light controller

Adafruit KB2040 (RP2040, arduino-pico core) running a bike's headlight, rear brake/turn lamp, HUD turn LEDs and battery bar off an M12 12V pack.

## Commands
- Build (kb2040 is the default env): `~/.platformio/penv/Scripts/pio.exe run`
- Upload: `pio run -t upload`
- Logic tests (host, Unity): `pio test -e native`
- Serial monitor (115200): `pio device monitor`, prints vbat/brake/turn/head/bright/strobe once a second
- Schematic SVGs: `python docs/schematic/schematic.py` (schemdraw)

## Layout
- `lib/BikeLogic/BikeLogic.h`: all behaviour as pure, Arduino-free logic. Every tunable (timings, duty levels, battery thresholds) is a constant at its top.
- `src/main.cpp`: pins, debouncing, ADC, PWM and EEPROM only. Keep logic out of it so it stays testable.
- `test/test_logic/test_main.cpp`: Unity tests for BikeLogic. Add a test with every behaviour change.
- `docs/wiring-diagram.html`: block diagram, pin table and design notes. `docs/datasheets/`: part datasheets.

## Hardware facts the code relies on
- Inputs use internal pull-ups and are active-LOW, with no external pull-ups or caps; firmware debounces at 20ms.
- The brake levers are NC to GND, so HIGH = squeezed, and a broken wire reads as braking.
- Headlight: low-side IRLB3813 on GP9, PWM at 25kHz with `PWM_RANGE` 1000. Lower frequencies whine audibly.
- The rear lamp outputs (GP6–8 brake/turns, GP27 tail) drive TLP241A photorelays. These switch on/off only, never PWM (up to 5ms to switch).
  - The lamp flashes its own turn signal, so hold turn outputs steady.
  - The tail (runner) is on in every mode except a daytime strobe; the brake output flashes with any strobe, and real braking always overrides with solid on.
- The HUD LEDs (GP0/1, GP10/18/19/20) are PWM-dimmed at night via `hudDuty`.
- VSENSE (A3/GP29) has no filter cap; average many ADC reads. Calibrate with `VBAT_CAL`.
- The low-voltage cutoff latches until a power cycle. On the bench, jumper A3 (VSENSE) to 3V3 and GP2/GP3 to GND, or it trips.

## Conventions
- Prefer firmware over extra parts, and through-hole, hand-solderable parts (perfboard build).
