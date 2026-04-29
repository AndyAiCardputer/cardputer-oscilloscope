/*
 * DDS Signal Generator for M5Stack Cardputer v1.1
 * Version: 1.0.0
 *
 * Uses M5Stack DDS Unit (AD9833) connected via Grove Port (I2C).
 * Generates true analog waveforms: Sine, Triangle, Square, Sawtooth, DC.
 *
 * AD9833 features:
 *   - 28-bit frequency resolution (sub-Hz precision)
 *   - Range: 0 Hz to 1 MHz (sine/triangle), up to 12.5 MHz (square)
 *   - Output: SMA connector, 0-0.6V amplitude
 *   - Sawtooth is hardware-limited to ~13.6 kHz
 *
 * Hardware connection:
 *   Grove cable: Cardputer Port A (G2=SDA, G1=SCL) -> DDS Unit
 *   DDS output:  SMA -> wire -> oscilloscope input (G1)
 *
 * Controls:
 *   W         - Cycle waveform (Sine -> Triangle -> Square -> Sawtooth -> DC)
 *   H / J     - Frequency up / down (step through presets)
 *   +/- (=/-)  - Fine frequency adjust (+10% / -10%)
 *   U / I     - Fine frequency adjust (+1% / -1%)
 *   P / O     - Phase up / down (15 degree steps)
 *   R         - Toggle output on/off
 *   S         - Sweep mode on/off (auto-sweep from startFreq to endFreq)
 *   A         - Reset to defaults
 *   1-9       - Quick frequency presets
 *
 * Andy + AI, April 2026
 */

#include <M5Cardputer.h>
#include <Wire.h>
#include "Unit_DDS.h"

// --- Display ---
#define SCREEN_W 240
#define SCREEN_H 135

// --- Colors ---
#define COL_BG       0x0000
#define COL_PANEL    0x18E3
#define COL_GREEN    0x07E0
#define COL_YELLOW   0xFFE0
#define COL_RED      0xF800
#define COL_CYAN     0x07FF
#define COL_WHITE    0xFFFF
#define COL_GRAY     0x7BEF
#define COL_DARK     0x2945
#define COL_ORANGE   0xFD20
#define COL_MAGENTA  0xF81F

// --- Waveform Types ---
enum WaveType { WAVE_SINE, WAVE_TRIANGLE, WAVE_SQUARE, WAVE_SAW, WAVE_DC, WAVE_COUNT };
const char* waveNames[] = { "Sine", "Triangle", "Square", "Sawtooth", "DC" };
const uint16_t waveColors[] = { COL_CYAN, COL_YELLOW, COL_GREEN, COL_ORANGE, COL_MAGENTA };

Unit_DDS::DDSmode ddsMode[] = {
    Unit_DDS::kSINUSMode,
    Unit_DDS::kTRIANGLEMode,
    Unit_DDS::kSQUAREMode,
    Unit_DDS::kSAWTOOTHMode,
    Unit_DDS::kDCMode
};

// --- Frequency Presets ---
const uint32_t presetFreqs[] = {
    1, 5, 10, 50, 100, 500,
    1000, 2000, 5000, 10000, 20000, 50000,
    100000, 200000, 500000, 1000000
};
const int presetCount = sizeof(presetFreqs) / sizeof(presetFreqs[0]);

// --- State ---
Unit_DDS dds;
M5Canvas canvas(&M5Cardputer.Display);

uint32_t currentFreq = 1000;
int presetIdx = 6;
int phaseDeg = 0;
bool outputOn = true;
bool ddsConnected = false;
WaveType waveType = WAVE_SINE;

// Sweep mode
bool sweepActive = false;
uint32_t sweepStart = 100;
uint32_t sweepEnd = 10000;
uint32_t sweepStep = 100;
unsigned long sweepLastMs = 0;
int sweepDir = 1;

// --- Forward Declarations ---
void applyDDS();
void stopDDS();
void drawUI();
void drawWavePreview(int x, int y, int w, int h);
void handleKeyboard();
void handleSweep();
String freqToString(uint32_t freq);

// =============================================
// Setup
// =============================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== DDS Signal Generator v1.2.0 ===");
    Serial.println("    AD9833 via I2C (Grove Port A)");

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(80);

    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    // Grove Port A on Cardputer v1.1: SDA=GPIO2, SCL=GPIO1
    Wire.begin(2, 1);
    delay(100);
    Serial.println("[I2C] Wire.begin(SDA=2, SCL=1) -- Grove Port A");

    // Initialize DDS Unit
    int result = dds.begin(&Wire);
    if (result == 0) {
        ddsConnected = true;
        Serial.println("[DDS] AD9833 connected at 0x31");
    } else {
        ddsConnected = false;
        Serial.printf("[DDS] ERROR: AD9833 not found (result=%d)\n", result);
    }

    if (ddsConnected) {
        applyDDS();
    }

    Serial.printf("Output: SMA | Freq: %lu Hz | Wave: %s\n",
                  currentFreq, waveNames[waveType]);
    Serial.println("Ready! Connect DDS SMA output -> oscilloscope input.");
}

// =============================================
// Main Loop
// =============================================
void loop() {
    M5Cardputer.update();
    handleKeyboard();
    if (sweepActive) handleSweep();
    drawUI();
    canvas.pushSprite(0, 0);
    delay(30);
}

// =============================================
// Apply DDS Output
// =============================================
void applyDDS() {
    if (!ddsConnected) return;

    if (!outputOn) {
        stopDDS();
        return;
    }

    uint32_t outFreq = currentFreq;

    if (waveType == WAVE_SAW) {
        outFreq = 13600;
    }

    if (waveType == WAVE_DC) {
        outFreq = 0;
    }

    // Always explicitly set all registers to avoid dirty state
    // (quickOUT skips freq/phase for Sawtooth/DC, causing noise on Sine after)
    dds.setFreqAndPhase(0, outFreq, 0, phaseDeg);
    delay(5);
    dds.setMode(ddsMode[waveType]);
    dds.setCTRL(0);

    Serial.printf("[DDS] %s %lu Hz Phase=%d deg\n",
                  waveNames[waveType], outFreq, phaseDeg);
}

// =============================================
// Stop DDS Output
// =============================================
void stopDDS() {
    if (!ddsConnected) return;
    dds.setSleep(2);
    Serial.println("[DDS] Output OFF (sleep mode)");
}

// =============================================
// Sweep Handler
// =============================================
void handleSweep() {
    if (!outputOn || !ddsConnected) return;
    if (waveType == WAVE_SAW || waveType == WAVE_DC) return;

    unsigned long now = millis();
    if (now - sweepLastMs < 50) return;
    sweepLastMs = now;

    currentFreq += sweepStep * sweepDir;

    if (currentFreq >= sweepEnd) {
        currentFreq = sweepEnd;
        sweepDir = -1;
    } else if (currentFreq <= sweepStart) {
        currentFreq = sweepStart;
        sweepDir = 1;
    }

    dds.quickOUT(ddsMode[waveType], currentFreq, phaseDeg);
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
    canvas.print("DDS GENERATOR v1  AD9833");

    // Connection status
    if (!ddsConnected) {
        canvas.setTextColor(COL_RED);
        canvas.setCursor(SCREEN_W - 48, 4);
        canvas.print("NO DDS");
    } else {
        canvas.setTextColor(outputOn ? COL_GREEN : COL_RED);
        canvas.setCursor(SCREEN_W - 30, 4);
        canvas.print(outputOn ? " ON" : "OFF");
    }

    // Frequency display
    uint16_t wCol = waveColors[waveType];
    canvas.setTextColor(wCol);
    canvas.setTextSize(3);
    uint32_t displayFreq = currentFreq;
    if (waveType == WAVE_SAW) displayFreq = 13600;
    if (waveType == WAVE_DC) displayFreq = 0;
    String freqStr = freqToString(displayFreq);
    int tw = freqStr.length() * 18;
    canvas.setCursor((SCREEN_W - tw) / 2, 20);
    canvas.print(freqStr);

    // Sawtooth frequency lock indicator
    if (waveType == WAVE_SAW) {
        canvas.setTextSize(1);
        canvas.setTextColor(COL_RED);
        canvas.setCursor((SCREEN_W + tw) / 2 + 4, 28);
        canvas.print("FIXED");
    }

    // Waveform name + phase
    canvas.setTextSize(2);
    canvas.setCursor(10, 50);
    canvas.setTextColor(wCol);
    canvas.print(waveNames[waveType]);

    canvas.setTextColor(COL_YELLOW);
    canvas.setTextSize(1);
    canvas.setCursor(150, 54);
    canvas.printf("Phase: %d", phaseDeg);
    canvas.print((char)247);  // degree symbol

    // Sweep indicator
    if (sweepActive) {
        canvas.setTextColor(COL_MAGENTA);
        canvas.setCursor(10, 64);
        canvas.printf("SWEEP %s-%s",
                      freqToString(sweepStart).c_str(),
                      freqToString(sweepEnd).c_str());
    }

    // Wave preview
    drawWavePreview(10, 72, 140, 36);

    // Info panel (right side)
    int rx = 158;
    canvas.setTextSize(1);
    canvas.setTextColor(COL_GRAY);
    canvas.setCursor(rx, 72);
    if (waveType == WAVE_SQUARE) {
        canvas.setTextColor(COL_RED);
        canvas.print("Out: SMA 0-3.3V!");
    } else {
        canvas.print("Out: SMA 0-0.6V");
    }
    canvas.setTextColor(COL_GRAY);
    canvas.setCursor(rx, 84);
    canvas.print("I2C: 0x31 Grove");
    canvas.setCursor(rx, 96);
    if (waveType == WAVE_SQUARE) {
        canvas.setTextColor(COL_RED);
        canvas.print("Digital output!");
    } else if (waveType == WAVE_SAW) {
        canvas.setTextColor(COL_ORANGE);
        canvas.print("Saw: 13.6kHz only");
    } else if (waveType == WAVE_DC) {
        canvas.print("DC: ~0.3V level");
    } else {
        canvas.printf("Res: 28-bit");
    }

    // Key hints
    canvas.fillRect(0, SCREEN_H - 24, SCREEN_W, 24, COL_PANEL);
    canvas.setTextColor(COL_WHITE);
    canvas.setTextSize(1);
    canvas.setCursor(4, SCREEN_H - 20);
    canvas.print("W:wave H/J:freq +/-:10% U/I:1% P/O:phase");
    canvas.setCursor(4, SCREEN_H - 10);
    canvas.print("R:on/off  S:sweep  A:reset  1-9:preset");
}

// =============================================
// Draw Wave Preview
// =============================================
void drawWavePreview(int x, int y, int w, int h) {
    canvas.drawRect(x, y, w, h, COL_DARK);

    if (!outputOn || !ddsConnected) {
        canvas.drawFastHLine(x + 2, y + h / 2, w - 4, COL_RED);
        canvas.setTextColor(COL_RED);
        canvas.setTextSize(1);
        canvas.setCursor(x + w / 2 - 10, y + h / 2 - 4);
        canvas.print(ddsConnected ? "OFF" : "N/C");
        return;
    }

    uint16_t col = waveColors[waveType];
    int hi = y + 3;
    int lo = y + h - 4;
    int range = lo - hi;
    int drawW = w - 4;
    int periods = 3;

    switch (waveType) {
    case WAVE_SINE: {
        int prevPx = -1, prevPy = -1;
        for (int px = 0; px < drawW; px++) {
            float phase = (float)px / drawW * periods * 2.0f * M_PI;
            int py = (hi + lo) / 2 - (int)(range / 2.0f * sinf(phase));
            int sx = x + 2 + px;
            if (prevPx >= 0) canvas.drawLine(prevPx, prevPy, sx, py, col);
            prevPx = sx;
            prevPy = py;
        }
        break;
    }
    case WAVE_TRIANGLE: {
        int prevPx = -1, prevPy = -1;
        for (int px = 0; px < drawW; px++) {
            float t = fmodf((float)px / drawW * periods, 1.0f);
            float val = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
            int py = lo - (int)(val * range);
            int sx = x + 2 + px;
            if (prevPx >= 0) canvas.drawLine(prevPx, prevPy, sx, py, col);
            prevPx = sx;
            prevPy = py;
        }
        break;
    }
    case WAVE_SQUARE: {
        int pxPerPeriod = drawW / periods;
        int cx = x + 2;
        for (int p = 0; p < periods; p++) {
            int halfW = pxPerPeriod / 2;
            canvas.drawFastVLine(cx, hi, range, col);
            canvas.drawFastHLine(cx, hi, halfW, col);
            cx += halfW;
            canvas.drawFastVLine(cx, hi, range, col);
            canvas.drawFastHLine(cx, lo, pxPerPeriod - halfW, col);
            cx += pxPerPeriod - halfW;
        }
        break;
    }
    case WAVE_SAW: {
        int prevPx = -1, prevPy = -1;
        for (int px = 0; px < drawW; px++) {
            float t = fmodf((float)px / drawW * periods, 1.0f);
            int py = lo - (int)(t * range);
            int sx = x + 2 + px;
            if (prevPx >= 0 && abs(py - prevPy) < range / 2) {
                canvas.drawLine(prevPx, prevPy, sx, py, col);
            } else if (prevPx >= 0) {
                canvas.drawFastVLine(sx, hi, range, col);
            }
            prevPx = sx;
            prevPy = py;
        }
        break;
    }
    case WAVE_DC: {
        int midY = (hi + lo) / 2;
        canvas.drawFastHLine(x + 2, midY, drawW, col);
        canvas.setTextColor(col);
        canvas.setTextSize(1);
        canvas.setCursor(x + drawW / 2 - 6, midY - 10);
        canvas.print("DC");
        break;
    }
    default:
        break;
    }
}

// =============================================
// Keyboard Handler
// =============================================
void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    if (!M5Cardputer.Keyboard.isPressed()) return;

    bool changed = false;

    // W - cycle waveform
    if (M5Cardputer.Keyboard.isKeyPressed('w')) {
        waveType = (WaveType)((waveType + 1) % WAVE_COUNT);
        changed = true;
        Serial.printf("[DDS] Waveform: %s\n", waveNames[waveType]);
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
        if (currentFreq > 1000000) currentFreq = 1000000;
        for (int i = 0; i < presetCount - 1; i++) {
            if (currentFreq <= presetFreqs[i]) { presetIdx = i; break; }
            presetIdx = i + 1;
        }
        changed = true;
    }

    // - fine freq down (-10%)
    if (M5Cardputer.Keyboard.isKeyPressed('-')) {
        currentFreq = currentFreq - currentFreq / 10;
        if (currentFreq < 1) currentFreq = 1;
        for (int i = presetCount - 1; i >= 0; i--) {
            if (currentFreq >= presetFreqs[i]) { presetIdx = i; break; }
            presetIdx = 0;
        }
        changed = true;
    }

    // U - ultra-fine freq up (+1%)
    if (M5Cardputer.Keyboard.isKeyPressed('u')) {
        currentFreq = currentFreq + currentFreq / 100;
        if (currentFreq < 1) currentFreq = 1;
        if (currentFreq > 1000000) currentFreq = 1000000;
        changed = true;
    }

    // I - ultra-fine freq down (-1%)
    if (M5Cardputer.Keyboard.isKeyPressed('i')) {
        currentFreq = currentFreq - currentFreq / 100;
        if (currentFreq < 1) currentFreq = 1;
        changed = true;
    }

    // P - phase up (+15 deg)
    if (M5Cardputer.Keyboard.isKeyPressed('p')) {
        phaseDeg += 15;
        if (phaseDeg >= 360) phaseDeg -= 360;
        changed = true;
    }

    // O - phase down (-15 deg)
    if (M5Cardputer.Keyboard.isKeyPressed('o')) {
        phaseDeg -= 15;
        if (phaseDeg < 0) phaseDeg += 360;
        changed = true;
    }

    // R - toggle output
    if (M5Cardputer.Keyboard.isKeyPressed('r')) {
        outputOn = !outputOn;
        changed = true;
    }

    // S - toggle sweep mode
    if (M5Cardputer.Keyboard.isKeyPressed('s')) {
        sweepActive = !sweepActive;
        if (sweepActive) {
            sweepStart = currentFreq / 10;
            if (sweepStart < 1) sweepStart = 1;
            sweepEnd = currentFreq * 10;
            if (sweepEnd > 1000000) sweepEnd = 1000000;
            sweepStep = (sweepEnd - sweepStart) / 100;
            if (sweepStep < 1) sweepStep = 1;
            sweepDir = 1;
            currentFreq = sweepStart;
            Serial.printf("[DDS] Sweep ON: %lu - %lu Hz\n", sweepStart, sweepEnd);
        } else {
            Serial.println("[DDS] Sweep OFF");
        }
        changed = true;
    }

    // A - reset defaults
    if (M5Cardputer.Keyboard.isKeyPressed('a')) {
        currentFreq = 1000;
        presetIdx = 6;
        phaseDeg = 0;
        outputOn = true;
        waveType = WAVE_SINE;
        sweepActive = false;
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
                sweepActive = false;
                changed = true;
            }
        }
    }

    if (changed) {
        applyDDS();
    }
}

// =============================================
// Helpers
// =============================================
String freqToString(uint32_t freq) {
    if (freq >= 1000000) {
        float mhz = freq / 1000000.0f;
        return String(mhz, 2) + " MHz";
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
