# DDS Signal Generator -- Complete Guide

**Project:** `cardputer/dds_generator/` v1.2.0  
**Date:** April 29, 2026  
**Author:** Andy + AI

---

## What is DDS?

**DDS (Direct Digital Synthesis)** is a technique for generating analog waveforms (sine, triangle, etc.) using digital electronics.

Think of it like a record player, but instead of a needle and vinyl, it uses math and a digital-to-analog converter (DAC).

### How DDS Works Inside:

1. **Phase Accumulator** -- a counter that continuously cycles from 0 to its maximum value (2^28 = 268 million values). The rate of cycling determines the output frequency.

2. **Lookup Table (ROM)** -- a table containing one full period of a sine wave is stored inside the chip. The phase accumulator points to which value to read at any given moment.

3. **DAC (Digital-to-Analog Converter)** -- takes the number from the lookup table and converts it into a real analog voltage on the output.

Analogy: imagine rapidly flipping through photos of one sine wave period. The faster you flip, the higher the frequency. Each "frame" produces an exact voltage level.

### Why DDS is Better Than PWM

Our previous generator (signal_generator v2.2.0) created a "sine wave" like this:
- Generated a high-frequency PWM carrier at 40 kHz
- Rapidly changed the duty cycle (0-255) following a sine lookup table
- Passed the output through an RC low-pass filter (resistor + capacitor)
- Resulted in an approximate sine wave with noise and ripple

DDS works differently:
- The built-in DAC directly outputs precise voltage levels
- No 40 kHz PWM carrier, resulting in a much cleaner signal. However, at higher frequencies there are still DAC step artifacts and spectral images (this is inherent physics of any DAC)
- The sine wave is significantly cleaner than PWM+RC
- 28-bit frequency resolution (0.037 Hz precision!)

---

## The AD9833 Chip

**AD9833** is a DDS IC manufactured by Analog Devices.

### Specifications:
- **Master Clock (MCLK):** 25 MHz maximum per datasheet. The M5Stack DDS Unit uses 10 MHz -- all calculations in this project are based on 10 MHz.
- **Frequency Resolution:** 28 bits
- **Maximum Frequency:** up to MCLK/2 (for our module = 5 MHz theoretical, 1 MHz practical for sine)
- **Output Waveforms:** sine, triangle, square
- **Output Voltage:** 0-0.6V (analog output), 0-3.3V (digital square wave)
- **Interface:** SPI (3-wire)
- **Power Supply:** 2.3V - 5.5V

### Frequency Formula:

```
Frequency = (FREQ_REG * MCLK) / 2^28

For our module (MCLK = 10 MHz):
  FREQ_REG = freq * 268435456 / 10000000
  FREQ_REG = freq * 26.8435456

Example: 1000 Hz = 1000 * 26.8435456 = 26843 (written to register)
```

This 28-bit number allows setting frequency with sub-Hz precision!

### Dual Frequency Registers:

The AD9833 has two frequency registers (FREQ0 and FREQ1) and two phase registers (PHASE0 and PHASE1). You can preload two different frequencies and switch between them instantly -- useful for FSK modulation (telegraphy, modems).

### Operating Modes:

| Mode | Description | Output |
|------|-------------|--------|
| Sine | Clean sine wave from DAC | Analog, 0-0.6V |
| Triangle | Triangle wave from DAC | Analog, 0-0.6V |
| Square | Square wave (MSB of phase accumulator) | Digital, 0-3.3V! |
| Sawtooth | Sawtooth wave | Fixed at ~13.6 kHz (DDS Unit firmware limitation, not the AD9833 itself) |
| DC | Constant voltage | ~0.3V |

**Important:** The Square wave is NOT an analog output. It's a digital signal directly from the phase accumulator, so its amplitude is 3.3V (full logic swing). Be careful connecting it to sensitive inputs!

---

## M5Stack DDS Unit

The DDS Unit is an M5Stack module that packages the AD9833 in a convenient enclosure with a Grove connector.

### What's Inside:

```
[Cardputer] --I2C--> [STM32 MCU] --SPI--> [AD9833] --analog--> [SMA output]
               Grove       (0x31)               (DDS chip)
```

Note: there's a **separate microcontroller (STM32)** between the Cardputer and the AD9833. We communicate with it over I2C, and it controls the AD9833 over SPI. This simplifies the interface -- I2C needs only 2 wires instead of 4.

### I2C Address: 0x31

### DDS Unit Registers (I2C):

| Address | Name | Description |
|---------|------|-------------|
| 0x10 | DESC | Device identifier (read 6 bytes = "ad9833") |
| 0x20 | MODE | Waveform mode (1=Sine, 2=Triangle, 3=Square, 4=Saw, 5=DC) |
| 0x21 | CTRL | Control (sleep, reset, register select) |
| 0x30 | FREQ | Frequency (4 bytes) + phase (2 bytes) = 6 bytes |
| 0x34 | PHASE | Phase only (2 bytes) |

### Output:
- **SMA connector** -- standard coaxial connector
- Two wires: signal + ground
- Can be connected directly to an oscilloscope

---

## How Our Generator Works (Code)

### Program Architecture:

```
setup()
  +-- Initialize Cardputer (display, keyboard)
  +-- Wire.begin(2, 1)  -- I2C on Grove pins
  +-- dds.begin(&Wire)  -- connect to DDS Unit
  +-- applyDDS()        -- start with Sine 1 kHz

loop() -- runs ~33 times per second (30ms delay)
  +-- M5Cardputer.update()  -- poll keyboard
  +-- handleKeyboard()      -- process key presses
  +-- handleSweep()         -- auto frequency sweep (if enabled)
  +-- drawUI()              -- render interface
  +-- canvas.pushSprite()   -- push to display
```

### How Frequency is Set (command path):

```
Press H key (frequency up)
  |
  +-- handleKeyboard()
  |     currentFreq = presetFreqs[++presetIdx]   // e.g., 2000
  |     changed = true
  |
  +-- applyDDS()
  |     dds.setFreqAndPhase(0, 2000, 0, 0)
  |       |
  |       +-- freq = 2000 * 268435456 / 10000000 = 53687
  |       +-- Pack into 6 bytes: [freq_hi, freq_mid2, freq_mid1, freq_lo, phase_hi, phase_lo]
  |       +-- I2C write: address 0x31, register 0x30, 6 bytes
  |
  |     dds.setMode(kSINUSMode)
  |       +-- I2C write: address 0x31, register 0x20, value 0x81
  |
  |     dds.setCTRL(0)
  |       +-- I2C write: address 0x31, register 0x21, value 0x80
  |
  +-- SMA output now produces 2000 Hz sine wave
```

### Sweep Mode:

When S is pressed, the frequency increments/decrements every 50ms:

```
Current frequency: 1000 Hz
Sweep ON -> range: 100 Hz -- 10000 Hz (1/10 ... x10)
  Step = (10000 - 100) / 100 = 99 Hz

Every 50ms:
  100 -> 199 -> 298 -> ... -> 9901 -> 10000 (reverse) -> 9901 -> ... -> 100 (reverse) -> ...
```

With a speaker connected, this sounds like a rising and falling tone.

---

## Hardware Connection

### Physical Diagram:

```
+------------------+        Grove cable        +----------------+
|  Cardputer v1.1  |  <--------------------->  |  DDS Unit      |
|                  |   SDA=GPIO2  SCL=GPIO1    |  (AD9833)      |
|  [Port A]        |   +5V        GND          |                |
|                  |                            |  [SMA output] -+--> to oscilloscope
|  Screen: gen UI  |                            |  0-0.6V / 3.3V |     or speaker
|  Keyboard: ctrl  |                            +----------------+
+------------------+
```

### CRITICAL: I2C Pin Mapping on Cardputer v1.1

```
Grove Port A:
  Pin 1 (yellow wire) = GPIO 2 = SDA  (data)
  Pin 2 (white wire)  = GPIO 1 = SCL  (clock)
  Pin 3 (red)         = 5V     (power)
  Pin 4 (black)       = GND    (ground)

IMPORTANT: Pins are REVERSED compared to what you might expect!
  Correct:  Wire.begin(2, 1)    -- SDA=GPIO2, SCL=GPIO1
  WRONG:    Wire.begin(1, 2)    -- devices will NOT be found!
```

We discovered this through an I2C bus scan -- the device at 0x31 only responds when using `Wire.begin(2, 1)`.

---

## Keyboard Controls

| Key | Action | Details |
|-----|--------|---------|
| **W** | Cycle waveform | Sine -> Triangle -> Square -> Sawtooth -> DC -> Sine |
| **H** | Frequency up | Through presets: 1, 5, 10, 50, 100, 500 Hz, 1, 2, 5, 10, 20, 50, 100, 200, 500 kHz, 1 MHz |
| **J** | Frequency down | Reverse through presets |
| **+** (=) | +10% | Fine frequency adjustment up |
| **-** | -10% | Fine frequency adjustment down |
| **U** | +1% | Ultra-fine frequency adjustment up |
| **I** | -1% | Ultra-fine frequency adjustment down |
| **P** | Phase +15 deg | Phase shift up (0-360 degrees) |
| **O** | Phase -15 deg | Phase shift down |
| **R** | Toggle output | Puts DDS into sleep mode (power saving) |
| **S** | Sweep mode | Auto frequency sweep (from 1/10 to x10 of current frequency) |
| **A** | Reset | Return to Sine 1 kHz, Phase 0, Output ON |
| **1-9** | Quick presets | 1=1Hz, 2=5Hz, 3=10Hz, 4=50Hz, 5=100Hz, 6=500Hz, 7=1kHz, 8=2kHz, 9=5kHz |

### Screen Layout:

```
+------------------------------------------+
| DDS GENERATOR v1  AD9833             ON  |  <- title + status
|                                          |
|             1 kHz                        |  <- current frequency (large)
|                                          |
| Sine                     Phase: 0 deg   |  <- waveform type + phase
|                                          |
| +----------------+  Out: SMA 0-0.6V     |  <- preview + info
| |  ~~~~~~~~~~~   |  I2C: 0x31 Grove     |
| +----------------+  Res: 28-bit         |
|                                          |
| W:wave H/J:freq +/-:10% U/I:1% P/O:pha |  <- key hints
| R:on/off  S:sweep  A:reset  1-9:preset  |
+------------------------------------------+
```

---

## Test Results

Tests performed using the oscilloscope ADV (external ILI9341 display, DMA ADC at 83 kS/s).

| Test | Frequency | Result | Vpp | Notes |
|------|-----------|--------|-----|-------|
| Sine | 1 kHz | Perfect | 0.60V | Clean sine wave |
| Sine | 100 Hz | Perfect | 0.60V | Low frequencies work great |
| Sine | 10 kHz | DDS ok | 0.59V | Oscilloscope shows coarse waveform: few samples per period (~8 samples/cycle at 83 kS/s) |
| Sine | 100 kHz | DDS ok | 0.59V | Beyond internal ADC capability (100 kHz > 83 kS/s, requires external ADC) |
| Triangle | 1 kHz | Perfect | 0.57V | Clean triangle wave |
| Square | 1 kHz | Perfect | 3.30V | Digital output! |
| Phase | any | Works | -- | Shift not visible without a second channel |
| Sweep | auto | Works | -- | Audible on speaker |

---

## Known Issues

### Bug: Noise After DC -> Sine Transition

**Symptom:** When cycling waveforms with W, after passing through DC mode back to Sine, the output produces noise instead of a clean sine wave.

**Cause:** Likely a bug in the STM32 firmware inside the DDS Unit. When transitioning from DC mode back to analog mode, the internal microcontroller doesn't properly reset the AD9833 state.

**What was tried:**
- `dds.reset()` before switching -- did not fix
- Explicit register writes (setFreqAndPhase + setMode + setCTRL) -- did not fix

**Workaround:** Press **A** (reset to defaults) -- instantly restores correct operation.

---

## Comparison: DDS Generator vs PWM Generator

| Parameter | PWM (signal_generator v2.2.0) | DDS (dds_generator v1.2.0) |
|-----------|-------------------------------|----------------------------|
| Chip | ESP32-S3 LEDC | AD9833 (via M5Stack DDS Unit) |
| Interface | GPIO directly | I2C (Grove cable) |
| Sine wave | Approximate (40kHz PWM + RC filter) | True analog (built-in DAC) |
| Triangle | Approximate (PWM + RC filter) | True analog |
| Square wave | Hardware LEDC | Digital output from AD9833 |
| Max freq (sine) | ~2 kHz (RC filter limitation) | ~1 MHz |
| Max freq (square) | ~100 kHz | Up to 1 MHz in this firmware (higher not tested) |
| Sine amplitude | 0-3.3V (after RC) | 0-0.6V |
| Square amplitude | 0-3.3V | 0-3.3V |
| Freq resolution | Coarse (LEDC dividers) | 28-bit (0.037 Hz!) |
| RC filter needed? | Yes (for sine/triangle) | No |
| Extra hardware | None (all on-board) | DDS Unit + Grove cable |
| Phase control | No | 0-360 degrees |
| Sweep mode | No | Yes |
| Square duty cycle | Adjustable (10-90%) | Fixed 50% |

**Conclusion:** The DDS generator is superior for sine and triangle waves. The PWM generator is better for square waves with adjustable duty cycle and doesn't require additional hardware.

---

## Project Structure

```
cardputer/dds_generator/
+-- platformio.ini              -- PlatformIO configuration
|     platform: espressif32@6.9.0
|     board: m5stack-stamps3
|     libs: M5Cardputer, M5Unified, M5GFX
|
+-- src/
|   +-- main.cpp                -- Main source code (574 lines)
|         setup()               -- Initialization
|         loop()                -- Main loop (30fps)
|         applyDDS()            -- Apply settings to DDS
|         stopDDS()             -- Turn off output (sleep)
|         handleSweep()         -- Auto-sweep logic
|         drawUI()              -- Render interface
|         drawWavePreview()     -- Waveform preview
|         handleKeyboard()      -- Key press handler
|         freqToString()        -- Frequency formatting
|
+-- lib/
    +-- Unit_DDS/               -- Embedded library (from M5Stack GitHub)
        +-- Unit_DDS.h          -- Header (54 lines)
        |     Unit_DDS class
        |     Constants: address 0x31, registers, MCLK
        |     Enum DDSmode: Sine, Triangle, Square, Sawtooth, DC
        |
        +-- Unit_DDS.cpp        -- Implementation (132 lines)
              begin()           -- Connect + verify ID "ad9833"
              setFreq()         -- Set frequency (28-bit)
              setPhase()        -- Set phase (12-bit)
              setFreqAndPhase() -- Set both in one command
              setMode()         -- Select waveform type
              quickOUT()        -- Quick setup all at once
              setSleep()        -- Sleep mode (1=quiet, 2=clock off)
              reset()           -- Reset AD9833
```

---

## Useful Formulas

### Frequency from Register:
```
freq_Hz = FREQ_REG * MCLK / 2^28
freq_Hz = FREQ_REG * 10000000 / 268435456
```

### Register from Frequency:
```
FREQ_REG = freq_Hz * 268435456 / 10000000
FREQ_REG = freq_Hz * 26.8435456
```

### Phase from Register:
```
phase_deg = PHASE_REG * 360 / 4096
```

### Register from Phase:
```
PHASE_REG = phase_deg * 4096 / 360
PHASE_REG = phase_deg * 11.378
```

### Minimum Frequency Step:
```
MCLK / 2^28 = 10000000 / 268435456 = 0.0373 Hz
```
This means you can set, for example, 440.0000 Hz (note A) with 0.04 Hz precision!

---

## Credits

- **AD9833 DDS chip** by Analog Devices
- **M5Stack DDS Unit** -- I2C wrapper module for AD9833
- **Unit_DDS library** -- originally by M5Stack ([GitHub](https://github.com/m5stack/M5Unit-DDS)), embedded in this project
- **M5Stack Cardputer** -- ESP32-S3 pocket computer

---

*Document created April 29, 2026. Project: cardputer/dds_generator v1.2.0*
