# Pocket Oscilloscope ADV

**Version:** 1.3.0  
**Platform:** M5Stack Cardputer ADV (ESP32-S3) + External ILI9341 Display  
**Author:** Andy + AI (Cursor + Claude)  
**Date:** April 2026

## Overview

Single-channel pocket oscilloscope with DMA-accelerated ADC sampling, running on
M5Stack Cardputer ADV with an external 2.4" ILI9341 display (320x240).

## Features

- **DMA ADC sampling** -- up to 83 kSps (12 us/sample), ~5x faster than analogRead
- **Dynamic sample rate** -- automatically adjusts from 611 Hz to 83333 Hz based on time scale
- **11 time scales** -- 10 us/div to 50 ms/div
- **5 voltage scales** -- 100 mV/div to 1.65 V/div
- **Trigger modes** -- Auto, Normal, Single (rising edge)
- **Measurements** -- Vpp, Vmin, Vmax, Frequency
- **Calibration** -- two-point voltage + frequency calibration (press C)
- **Built-in test signal** -- square wave on GPIO 2 (100 Hz - 10 kHz)
- **Flicker-free display** -- full-screen sprite buffer

## Hardware

- M5Stack Cardputer ADV (ESP32-S3)
- External ILI9341 display 320x240 (connected via EXT 2.54-14P connector)
- Probe wire to G1 (GPIO 1) on Grove connector

### Pin Mapping

| Function       | GPIO | Notes                          |
|----------------|------|--------------------------------|
| ADC Input      | 1    | Grove G1, 0-3.3V              |
| Test Signal    | 2    | Grove G2, LEDC PWM output     |
| LCD SCK        | 40   | Shared with SD card            |
| LCD MOSI       | 14   | Shared with SD card            |
| LCD CS         | 5    | EXT connector PIN 13           |
| LCD DC         | 6    | EXT connector PIN 5            |
| LCD RST        | 3    | EXT connector PIN 1            |

## Keyboard Controls

| Key   | Function                        |
|-------|---------------------------------|
| R     | Run / Stop                      |
| A     | Auto-scale voltage              |
| C     | Calibrate (needs signal on G1)  |
| T / Y | Trigger level up / down        |
| + / - | Voltage scale (zoom Y)         |
| ; / . | Time scale (zoom X)            |
| F     | Toggle frequency counter        |
| M     | Trigger mode (Auto/Norm/Single) |
| G     | Toggle test signal generator    |
| H / J | Test signal frequency up/down  |

## Building

Requires PlatformIO.

```bash
cd oscilloscope_adv
pio run                  # compile
pio run -t upload        # flash
pio device monitor       # serial output (115200 baud)
```

## Technical Notes

### DMA ADC
Uses ESP-IDF legacy `driver/adc.h` API (`adc_digi_*` functions) for DMA-based ADC
sampling. Key points:
- Pattern `bit_width` must be `SOC_ADC_DIGI_MAX_BITWIDTH` (12), not `ADC_WIDTH_BIT_12` (3)
- DMA must be initialized before any Arduino `analogRead()` calls
- ESP32-S3 uses TYPE2 output format (32-bit per sample: 12-bit data + channel + unit)
- Sample rate reconfigured on-the-fly when time scale changes

### Calibration
Press C with a known-frequency signal connected to G1:
- Collects 8 frames, averages min/max ADC values and measured frequency
- Snaps measured frequency to nearest standard value (100, 500, 1000, 5000 Hz etc.)
- Calculates correction factor for both voltage and frequency

## File Structure

```
oscilloscope_adv/
  platformio.ini
  src/
    main.cpp                    -- main application (850+ lines)
    pins.h                      -- GPIO pin definitions
    external_display/
      LGFX_ILI9341.h           -- M5GFX display driver config
      LGFX_ILI9341.cpp         -- display instance
```

## Companion: Signal Generator

See `../signal_generator/` -- a square wave generator for Cardputer v1.1 that can
be used as a signal source for testing this oscilloscope.
