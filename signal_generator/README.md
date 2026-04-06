# Signal Generator for M5Stack Cardputer

**Version:** 1.0.1  
**Platform:** M5Stack Cardputer v1.1 (ESP32-S3)  
**Author:** Andy + AI (Cursor + Claude)  
**Date:** April 2026

## Overview

Square wave signal generator using hardware LEDC/PWM on M5Stack Cardputer v1.1.
Designed as a companion tool for the Pocket Oscilloscope ADV.

## Features

- **Hardware PWM** -- uses ESP32-S3 LEDC for clean square waves
- **Frequency range** -- 1 Hz to 10 kHz (14 presets + fine tuning)
- **Duty cycle** -- adjustable in 10% steps (10% - 90%)
- **Output** -- GPIO 2 (Grove G2), 3.3V logic level
- **Waveform preview** -- shows signal on internal 240x135 display

## Hardware

- M5Stack Cardputer v1.1 (ESP32-S3)
- Output wire from G2 (GPIO 2) on Grove connector

### Connection to Oscilloscope

Connect Cardputer v1.1 (generator) to Cardputer ADV (oscilloscope):
- Generator G2 (GPIO 2) -> Oscilloscope G1 (GPIO 1)
- Generator GND -> Oscilloscope GND

## Keyboard Controls

| Key   | Function                         |
|-------|----------------------------------|
| H / J | Frequency preset up / down      |
| + / - | Fine frequency adjust            |
| T / Y | Duty cycle up / down (10% step) |
| R     | Toggle output on/off             |
| A     | Reset to defaults                |
| 1-9   | Quick frequency presets          |

## Frequency Presets

1 Hz, 2 Hz, 5 Hz, 10 Hz, 20 Hz, 50 Hz, 100 Hz, 200 Hz, 500 Hz,
1 kHz, 2 kHz, 5 kHz, 10 kHz

## Building

Requires PlatformIO.

```bash
cd signal_generator
pio run                  # compile
pio run -t upload        # flash
pio device monitor       # serial output (115200 baud)
```

## Technical Notes

- Uses `ledcSetup()` / `ledcAttachPin()` / `ledcWrite()` Arduino-ESP32 API
- PWM resolution (bits) adjusted dynamically based on frequency to avoid LEDC errors
- Low frequencies (< 1 kHz): 14-bit resolution
- Medium (1-5 kHz): 10-bit resolution
- High (>= 5 kHz): 8-bit resolution

## Companion: Oscilloscope ADV

See `../oscilloscope_adv/` -- a DMA-accelerated oscilloscope for Cardputer ADV
with external ILI9341 display, designed to measure signals from this generator.
