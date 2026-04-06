# Pocket Oscilloscope for M5Stack Cardputer

A single-channel pocket oscilloscope running on M5Stack Cardputer v1.1 (ESP32-S3).

## Features

- **1 channel ADC input** (GPIO 1 / Grove G1), 0-3.3V range, 12-bit resolution
- **240x135 TFT display** with sprite buffer (flicker-free rendering)
- **Keyboard controls** for all oscilloscope functions
- **3 trigger modes**: Auto, Normal, Single
- **Calibrated frequency counter** (accurate to <1%)
- **Auto-scale** voltage and trigger level
- **Built-in test signal generator** on GPIO 2 (G2): 100 Hz - 10 kHz square wave
- **Voltage measurements**: Vpp, Vmin, Vmax

## Hardware

- M5Stack Cardputer v1.1 (ESP32-S3)
- Probe wire to **G1** on Grove connector
- GND to **G** on Grove connector

### Self-Test Mode

Connect **G1 to G2** on the Grove connector with a jumper wire.
The built-in signal generator outputs a square wave on G2 that
the oscilloscope reads on G1. No external equipment needed!

### Important: Grove Port Pull-ups

The Cardputer's Grove Port A has hardware pull-up resistors (10k to 3.3V)
for I2C. This means:
- **Finger touch won't work** (too high impedance)
- **Low-impedance signal sources work fine** (GPIO output, function generators, active circuits)
- The built-in test signal on G2 easily overpowers the pull-ups

## Keyboard Controls

| Key | Function |
|-----|----------|
| **R** | Run / Stop |
| **A** | Auto-scale (fit signal to screen + set trigger) |
| **T** | Trigger level UP |
| **Y** | Trigger level DOWN |
| **+** (=) | Voltage zoom IN (smaller mV/div) |
| **-** | Voltage zoom OUT (larger mV/div) |
| **;** | Time zoom IN (faster, less us/div) |
| **.** | Time zoom OUT (slower, more us/div) |
| **F** | Toggle frequency counter |
| **M** | Cycle trigger mode (Auto → Normal → Single) |
| **G** | Toggle test signal generator (G2) |
| **H** | Test signal frequency UP |
| **J** | Test signal frequency DOWN |

## Voltage Scales

100 mV/div, 200 mV/div, 500 mV/div, 1 V/div, 1.65 V/div

## Time Scales

50 us/div, 100 us/div, 200 us/div, 500 us/div, 1 ms/div, 2 ms/div, 5 ms/div, 10 ms/div, 50 ms/div

## Test Signal Frequencies

100 Hz, 500 Hz, 1 kHz, 5 kHz, 10 kHz

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```bash
cd cardputer/oscilloscope
pio run              # compile
pio run -t upload    # flash to Cardputer
pio device monitor --baud 115200   # serial monitor (debug output)
```

## Dependencies

- `m5stack/M5Cardputer @ ^1.1.1`
- `m5stack/M5Unified @ ^0.2.10`
- `m5stack/M5GFX @ ^0.2.17`
- Platform: `espressif32 @ 6.9.0`

## How It Works

The oscilloscope runs a simple loop:

1. **Sample ADC** — reads 600 samples from GPIO 1 via `analogRead()`
2. **Find trigger** — searches for rising edge crossing the trigger level
3. **Draw to sprite** — grid, waveform (green lines), trigger line (red dashed), measurements
4. **Push to screen** — single `pushSprite()` call for flicker-free display
5. **Handle keyboard** — check for key presses, adjust settings

The frequency counter measures actual sampling time using `micros()` for calibration,
then counts rising edges in the displayed waveform to calculate frequency.

## Version History

- **v1.0.0** (April 4, 2026) — Initial release. ADC sampling, waveform display, keyboard controls, test signal generator, calibrated frequency counter.

## Credits

Andy + AI, April 2026
