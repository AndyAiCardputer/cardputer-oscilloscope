/*
 * Signal Generator for M5Stack Cardputer v1.1
 * Version: 2.1.0
 *
 * Generates square, sine, triangle and sawtooth waves on GPIO 2 (Grove G2).
 * Square wave: hardware LEDC/PWM at signal frequency.
 * Sine/Triangle/Sawtooth: fast PWM carrier (40kHz) + software duty modulation.
 *   Use with RC low-pass filter (1kOhm + 100nF) for clean analog output.
 *
 * Connect G2 -> [1kOhm] -> output + [100nF to GND] -> oscilloscope G1.
 *
 * Controls:
 *   W         - Cycle waveform (Square -> Sine -> Triangle -> Sawtooth)
 *   H / J     - Frequency up / down (step through presets)
 *   +/- (=/-)  - Fine frequency adjust (+10% / -10%)
 *   T / Y     - Duty cycle up / down (10% steps, square only)
 *   R         - Toggle output on/off
 *   A         - Reset to defaults
 *   1-9       - Quick frequency presets
 *
 * Andy + AI, April 2026
 */

#include <M5Cardputer.h>
#include <driver/gpio.h>
#include <driver/i2c.h>
#include <Wire.h>

#define SIG_PIN 2

// --- Display ---
#define SCREEN_W 240
#define SCREEN_H 135

// --- Colors ---
#define COL_BG       0x0000
#define COL_PANEL    0x18E3   // dark blue-gray
#define COL_GREEN    0x07E0
#define COL_YELLOW   0xFFE0
#define COL_RED      0xF800
#define COL_CYAN     0x07FF
#define COL_WHITE    0xFFFF
#define COL_GRAY     0x7BEF
#define COL_DARK     0x2945

// --- Waveform Types ---
enum WaveType { WAVE_SQUARE, WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_COUNT };
const char* waveNames[] = { "Square", "Sine", "Triangle", "Saw" };
const uint16_t waveColors[] = { COL_GREEN, COL_CYAN, COL_YELLOW, 0xFD20 }; // orange for saw

// --- Waveform Table ---
#define WAVE_TABLE_SIZE 64
#define WAVE_PWM_FREQ   40000   // 40 kHz carrier for analog waveforms
#define WAVE_PWM_BITS   8       // 256 duty levels
#define WAVE_MAX_FREQ   2000    // max freq for non-square waveforms
#define MIN_PERIOD_US   25      // minimum timer period

uint8_t waveTable[WAVE_TABLE_SIZE];
volatile int waveIdx = 0;
volatile int waveStep = 1;
volatile int wavePeriodUs = 100;
volatile bool waveTaskActive = false;
TaskHandle_t waveTaskHandle = NULL;

// --- Frequency Presets ---
const int presetFreqs[] = {
    1, 2, 5, 10, 20, 50, 100, 200, 500,
    1000, 2000, 5000, 10000, 20000, 50000, 100000
};
const int presetCount = sizeof(presetFreqs) / sizeof(presetFreqs[0]);

// --- State ---
M5Canvas canvas(&M5Cardputer.Display);
int currentFreq = 1000;
int presetIdx = 9;          // index of 1000 Hz
int dutyPercent = 50;
bool outputOn = true;
int ledcBits = 10;
WaveType waveType = WAVE_SQUARE;

// --- Forward Declarations ---
void applySignal();
void stopSignal();
void fillWaveTable(WaveType type);
void waveformTask(void *param);
void startWaveTask();
void stopWaveTask();
void drawUI();
void drawWavePreview(int x, int y, int w, int h);
void handleKeyboard();
String freqToString(int freq);

// =============================================
// Setup
// =============================================
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== Signal Generator v2.1.0 ===");

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(80);

    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    // Release I2C from Grove pins (safe on v1.1 -- keyboard is GPIO-based)
    Wire.end();
    i2c_driver_delete(I2C_NUM_0);
    i2c_driver_delete(I2C_NUM_1);
    gpio_reset_pin(GPIO_NUM_1);
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_pull_mode(GPIO_NUM_1, GPIO_FLOATING);
    gpio_set_pull_mode(GPIO_NUM_2, GPIO_FLOATING);

    fillWaveTable(WAVE_SINE);

    // Create waveform generation task on core 0 (main loop runs on core 1)
    xTaskCreatePinnedToCore(waveformTask, "wave", 4096, NULL, 5, &waveTaskHandle, 0);

    applySignal();

    Serial.printf("Output: GPIO %d | Freq: %d Hz | Wave: %s\n",
                  SIG_PIN, currentFreq, waveNames[waveType]);
    Serial.println("Ready! Connect G2 -> [1k] -> out+[100nF to GND] -> oscilloscope G1.");
}

// =============================================
// Main Loop
// =============================================
void loop() {
    M5Cardputer.update();
    handleKeyboard();
    drawUI();
    canvas.pushSprite(0, 0);
    delay(30);
}

// =============================================
// Waveform Table Generation
// =============================================
void fillWaveTable(WaveType type) {
    for (int i = 0; i < WAVE_TABLE_SIZE; i++) {
        switch (type) {
        case WAVE_SINE:
            waveTable[i] = (uint8_t)(127.5f + 127.5f * sinf(2.0f * M_PI * i / WAVE_TABLE_SIZE));
            break;
        case WAVE_TRIANGLE:
            if (i < WAVE_TABLE_SIZE / 2)
                waveTable[i] = (uint8_t)(255 * i * 2 / WAVE_TABLE_SIZE);
            else
                waveTable[i] = (uint8_t)(255 * (WAVE_TABLE_SIZE - i) * 2 / WAVE_TABLE_SIZE);
            break;
        case WAVE_SAW:
            waveTable[i] = (uint8_t)(255 * i / WAVE_TABLE_SIZE);
            break;
        default:
            waveTable[i] = (i < WAVE_TABLE_SIZE / 2) ? 255 : 0;
            break;
        }
    }
}

// =============================================
// Waveform Task (runs on core 0, modulates PWM duty)
// =============================================
void waveformTask(void *param) {
    // Disable Watchdog for IDLE task on core 0 --
    // our tight timing loop prevents IDLE from running,
    // which would trigger WDT reset otherwise
    disableCore0WDT();

    while (true) {
        if (waveTaskActive) {
            unsigned long t0 = micros();
            ledcWrite(0, waveTable[waveIdx]);
            waveIdx = (waveIdx + waveStep) % WAVE_TABLE_SIZE;
            unsigned long elapsed = micros() - t0;
            if ((int)elapsed < wavePeriodUs) {
                delayMicroseconds(wavePeriodUs - elapsed);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void startWaveTask() {
    // Calculate timing: period per sample step
    waveStep = 1;
    wavePeriodUs = 1000000 / (currentFreq * WAVE_TABLE_SIZE);
    while (wavePeriodUs < MIN_PERIOD_US && waveStep < WAVE_TABLE_SIZE / 4) {
        waveStep *= 2;
        wavePeriodUs = 1000000 * waveStep / (currentFreq * WAVE_TABLE_SIZE);
    }
    waveIdx = 0;

    int samplesPerCycle = WAVE_TABLE_SIZE / waveStep;
    Serial.printf("[WAV] %s %dHz: %d samples/cycle, %d us/step\n",
                  waveNames[waveType], currentFreq, samplesPerCycle, wavePeriodUs);

    // Set up high-frequency PWM carrier
    ledcSetup(0, WAVE_PWM_FREQ, WAVE_PWM_BITS);
    ledcAttachPin(SIG_PIN, 0);
    ledcWrite(0, 0);

    waveTaskActive = true;
}

void stopWaveTask() {
    waveTaskActive = false;
    vTaskDelay(pdMS_TO_TICKS(5));
}

// =============================================
// Apply Signal
// =============================================
void applySignal() {
    if (!outputOn) {
        stopSignal();
        return;
    }

    stopWaveTask();

    if (waveType == WAVE_SQUARE) {
        // Direct LEDC at signal frequency
        if (currentFreq >= 50000) ledcBits = 6;
        else if (currentFreq >= 10000) ledcBits = 8;
        else if (currentFreq >= 1000) ledcBits = 10;
        else if (currentFreq >= 100) ledcBits = 12;
        else if (currentFreq >= 10) ledcBits = 14;
        else ledcBits = 14;

        int maxDuty = (1 << ledcBits) - 1;
        int dutyVal = maxDuty * dutyPercent / 100;

        ledcSetup(0, currentFreq, ledcBits);
        ledcAttachPin(SIG_PIN, 0);
        ledcWrite(0, dutyVal);

        Serial.printf("[SIG] Square %dHz  Duty=%d%%  Bits=%d\n",
                      currentFreq, dutyPercent, ledcBits);
    } else {
        // Waveform mode: fast PWM + duty modulation
        if (currentFreq > WAVE_MAX_FREQ) {
            Serial.printf("[SIG] %dHz too high for %s, max %dHz\n",
                          currentFreq, waveNames[waveType], WAVE_MAX_FREQ);
        }
        int effectiveFreq = (currentFreq > WAVE_MAX_FREQ) ? WAVE_MAX_FREQ : currentFreq;
        int savedFreq = currentFreq;
        currentFreq = effectiveFreq;

        fillWaveTable(waveType);
        startWaveTask();

        currentFreq = savedFreq;
    }
}

void stopSignal() {
    stopWaveTask();
    ledcDetachPin(SIG_PIN);
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_pull_mode(GPIO_NUM_2, GPIO_FLOATING);
    digitalWrite(SIG_PIN, LOW);
    Serial.println("[SIG] Output OFF");
}

// =============================================
// Draw UI
// =============================================
void drawUI() {
    canvas.fillSprite(COL_BG);

    // Title bar
    canvas.fillRect(0, 0, SCREEN_W, 16, COL_PANEL);
    canvas.setTextColor(COL_CYAN);
    canvas.setTextSize(1);
    canvas.setCursor(4, 4);
    canvas.print("SIGNAL GENERATOR v2");
    canvas.setTextColor(outputOn ? COL_GREEN : COL_RED);
    canvas.setCursor(SCREEN_W - 30, 4);
    canvas.print(outputOn ? " ON" : "OFF");

    // Big frequency display
    uint16_t wCol = waveColors[waveType];
    canvas.setTextColor(wCol);
    canvas.setTextSize(3);
    String freqStr = freqToString(currentFreq);
    int tw = freqStr.length() * 18;
    canvas.setCursor((SCREEN_W - tw) / 2, 22);
    canvas.print(freqStr);

    // Waveform type + duty (for square)
    canvas.setTextSize(2);
    canvas.setCursor(10, 52);
    canvas.setTextColor(wCol);
    canvas.print(waveNames[waveType]);

    if (waveType == WAVE_SQUARE) {
        canvas.setTextColor(COL_YELLOW);
        char dutyStr[16];
        snprintf(dutyStr, sizeof(dutyStr), "  Duty:%d%%", dutyPercent);
        canvas.print(dutyStr);
    } else if (currentFreq > WAVE_MAX_FREQ) {
        canvas.setTextColor(COL_RED);
        canvas.setTextSize(1);
        canvas.setCursor(130, 56);
        canvas.printf("max %dHz!", WAVE_MAX_FREQ);
    }

    // Wave preview
    drawWavePreview(10, 72, 140, 36);

    // Info panel (right side)
    int rx = 158;
    canvas.setTextSize(1);
    canvas.setTextColor(COL_GRAY);
    canvas.setCursor(rx, 72);
    canvas.printf("Pin: G2 (GPIO%d)", SIG_PIN);
    canvas.setCursor(rx, 84);
    if (waveType == WAVE_SQUARE) {
        canvas.printf("LEDC: %d-bit", ledcBits);
    } else {
        canvas.printf("PWM: 40kHz 8-bit");
    }
    canvas.setCursor(rx, 96);
    canvas.printf("Vout: 0-3.3V");

    // Key hints
    canvas.fillRect(0, SCREEN_H - 24, SCREEN_W, 24, COL_PANEL);
    canvas.setTextColor(COL_WHITE);
    canvas.setTextSize(1);
    canvas.setCursor(4, SCREEN_H - 20);
    canvas.print("W:wave  H/J:freq  +/-:fine  T/Y:duty");
    canvas.setCursor(4, SCREEN_H - 10);
    canvas.print("R:on/off  A:reset  1-9:preset");
}

// =============================================
// Draw Wave Preview
// =============================================
void drawWavePreview(int x, int y, int w, int h) {
    canvas.drawRect(x, y, w, h, COL_DARK);

    if (!outputOn) {
        canvas.drawFastHLine(x + 2, y + h / 2, w - 4, COL_RED);
        canvas.setTextColor(COL_RED);
        canvas.setTextSize(1);
        canvas.setCursor(x + w / 2 - 10, y + h / 2 - 4);
        canvas.print("OFF");
        return;
    }

    uint16_t col = waveColors[waveType];
    int hi = y + 3;
    int lo = y + h - 4;
    int range = lo - hi;
    int drawW = w - 4;

    if (waveType == WAVE_SQUARE) {
        int periods = 3;
        float dutyFrac = dutyPercent / 100.0f;
        int pxPerPeriod = drawW / periods;
        int cx = x + 2;
        for (int p = 0; p < periods; p++) {
            int hiWidth = (int)(pxPerPeriod * dutyFrac);
            int loWidth = pxPerPeriod - hiWidth;
            if (hiWidth < 1) hiWidth = 1;
            if (loWidth < 1) loWidth = 1;
            canvas.drawFastVLine(cx, hi, range, col);
            canvas.drawFastHLine(cx, hi, hiWidth, col);
            cx += hiWidth;
            canvas.drawFastVLine(cx, hi, range, col);
            canvas.drawFastHLine(cx, lo, loWidth, col);
            cx += loWidth;
        }
    } else {
        // Draw from wave table
        int prevPx = -1, prevPy = -1;
        int periods = 3;
        for (int px = 0; px < drawW; px++) {
            float phase = (float)px / drawW * periods * WAVE_TABLE_SIZE;
            int idx = (int)phase % WAVE_TABLE_SIZE;
            int py = lo - (waveTable[idx] * range / 255);
            int sx = x + 2 + px;
            if (prevPx >= 0) {
                canvas.drawLine(prevPx, prevPy, sx, py, col);
            }
            prevPx = sx;
            prevPy = py;
        }
    }
}

// =============================================
// Keyboard Handler
// =============================================
void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    if (!M5Cardputer.Keyboard.isPressed()) return;

    bool changed = false;

    // W - cycle waveform type
    if (M5Cardputer.Keyboard.isKeyPressed('w')) {
        waveType = (WaveType)((waveType + 1) % WAVE_COUNT);
        if (waveType != WAVE_SQUARE) fillWaveTable(waveType);
        changed = true;
        Serial.printf("[SIG] Waveform: %s\n", waveNames[waveType]);
    }

    // H - frequency up (next preset)
    if (M5Cardputer.Keyboard.isKeyPressed('h')) {
        if (presetIdx < presetCount - 1) {
            presetIdx++;
            currentFreq = presetFreqs[presetIdx];
            changed = true;
        }
    }

    // J - frequency down (prev preset)
    if (M5Cardputer.Keyboard.isKeyPressed('j')) {
        if (presetIdx > 0) {
            presetIdx--;
            currentFreq = presetFreqs[presetIdx];
            changed = true;
        }
    }

    // + (=) - fine freq up (+10%)
    if (M5Cardputer.Keyboard.isKeyPressed('=')) {
        currentFreq = currentFreq + currentFreq / 10;
        if (currentFreq > 200000) currentFreq = 200000;
        changed = true;
    }

    // - fine freq down (-10%)
    if (M5Cardputer.Keyboard.isKeyPressed('-')) {
        currentFreq = currentFreq - currentFreq / 10;
        if (currentFreq < 1) currentFreq = 1;
        changed = true;
    }

    // T - duty up
    if (M5Cardputer.Keyboard.isKeyPressed('t')) {
        dutyPercent += 10;
        if (dutyPercent > 90) dutyPercent = 90;
        changed = true;
    }

    // Y - duty down
    if (M5Cardputer.Keyboard.isKeyPressed('y')) {
        dutyPercent -= 10;
        if (dutyPercent < 10) dutyPercent = 10;
        changed = true;
    }

    // R - toggle output
    if (M5Cardputer.Keyboard.isKeyPressed('r')) {
        outputOn = !outputOn;
        changed = true;
    }

    // A - reset defaults
    if (M5Cardputer.Keyboard.isKeyPressed('a')) {
        currentFreq = 1000;
        presetIdx = 9;
        dutyPercent = 50;
        outputOn = true;
        changed = true;
    }

    // 1-9 quick presets
    for (char c = '1'; c <= '9'; c++) {
        if (M5Cardputer.Keyboard.isKeyPressed(c)) {
            int idx = c - '1';
            if (idx < presetCount) {
                presetIdx = idx;
                currentFreq = presetFreqs[presetIdx];
                outputOn = true;
                changed = true;
            }
        }
    }

    if (changed) {
        applySignal();
    }
}

// =============================================
// Helpers
// =============================================
String freqToString(int freq) {
    if (freq >= 1000000) {
        float mhz = freq / 1000000.0f;
        return String(mhz, 1) + " MHz";
    } else if (freq >= 1000) {
        float khz = freq / 1000.0f;
        if (khz == (int)khz)
            return String((int)khz) + " kHz";
        else
            return String(khz, 1) + " kHz";
    } else {
        return String(freq) + " Hz";
    }
}
