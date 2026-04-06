# Pocket Oscilloscope for Cardputer ADV (Internal Display)

**Version:** 2.0.0
**Platform:** M5Stack Cardputer ADV (ESP32-S3) -- internal 240x135 display
**Author:** Andy + AI

## Overview

Single-channel pocket oscilloscope running on M5Stack Cardputer ADV using
the built-in 240x135 display. No external display needed.

Same feature set as the [Cardputer v1.1 oscilloscope](../oscilloscope/), but
adapted for ADV hardware (different keyboard controller, different init sequence).

## Features

- **DMA-accelerated ADC** -- up to 83 kSps (12 us/sample)
- **Dynamic sample rate** -- auto-adjusts from 611 Hz to 83333 Hz based on time scale
- **11 time scales** -- 10 us/div to 50 ms/div
- **5 voltage scales** -- 100 mV/div to 1.65 V/div
- **Trigger modes** -- Auto, Normal, Single (rising edge)
- **Measurements** -- Vpp, Vmin, Vmax, Frequency
- **Calibration** -- two-point voltage + frequency calibration
- **Built-in test signal** -- square wave on GPIO 2 (100 Hz - 10 kHz)
- **240x135 internal display** -- flicker-free sprite rendering

## Hardware

- M5Stack Cardputer ADV (ESP32-S3)
- Probe wire to **G1** (GPIO 1) on Grove connector
- GND to **G** on Grove connector

### Self-Test

Connect **G1 to G2** with a jumper wire. Press **G** to enable the test signal
generator and you have a working oscilloscope + signal source with zero external
equipment.

## Keyboard Controls

| Key | Function |
|-----|----------|
| R | Run / Stop |
| A | Auto-scale voltage |
| C | Calibrate (needs signal on G1) |
| T / Y | Trigger level up / down |
| + / - | Voltage scale (zoom Y) |
| ; / . | Time scale (zoom X) |
| F | Toggle frequency counter |
| M | Trigger mode (Auto/Normal/Single) |
| G | Toggle test signal generator |
| H / J | Test signal frequency up / down |

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
cd oscilloscope_adv_internal
pio run                  # compile
pio run -t upload        # flash
pio device monitor       # serial output (115200 baud)
```

### Pre-compiled Firmware

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXXX --baud 460800 \
  write_flash 0x10000 ../firmware/oscilloscope_adv_int.bin
```

## Difference from Cardputer v1.1 Version

The only differences are in `setup()`:
- Does **NOT** delete I2C drivers (Cardputer ADV uses TCA8418 I2C keyboard on GPIO 8/9)
- Uses `cfg.output_power = true` for ADV power management
- Uses `M5Cardputer.begin(cfg)` without second argument

## Credits

Andy + AI, April 2026
