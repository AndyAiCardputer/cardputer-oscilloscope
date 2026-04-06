/*
 * Pocket Oscilloscope ADV -- External Display Edition
 * Version: 1.3.0
 *
 * Single-channel oscilloscope for M5Stack Cardputer ADV
 * with external ILI9341 display (320x240).
 *
 * Input: GPIO 1 (Grove port G1), 0-3.3V range.
 * Display: External ILI9341 320x240 via SPI + sprite buffer.
 *
 * Controls:
 *   R       - Run / Stop
 *   A       - Auto-scale voltage
 *   C       - Calibrate (needs G1 connected to signal source)
 *   T/Y     - Trigger level up/down
 *   +/-     - Voltage scale (zoom Y)
 *   ;/.     - Time scale (zoom X)
 *   F       - Toggle frequency counter
 *   M       - Toggle trigger mode (Auto/Normal/Single)
 *   G       - Toggle test signal generator (G2)
 *   H/J     - Test signal frequency up/down
 *
 * Hardware:
 *   - M5Stack Cardputer ADV (ESP32-S3)
 *   - External ILI9341 display (320x240, 2.4")
 *   - Probe wire to G1 (GPIO 1) on Grove connector
 *
 * Andy + AI, April 2026
 */

#include <M5Cardputer.h>
#include <driver/gpio.h>
#include <driver/adc.h>
#include "pins.h"

#ifdef USE_EXTERNAL_DISPLAY
#include "external_display/LGFX_ILI9341.h"
#endif

// --- Display Layout (320x240 landscape) ---
#define SCREEN_W     320
#define SCREEN_H     240
#define GRAPH_X      30   // left margin for voltage labels
#define GRAPH_Y      4    // top margin
#define GRAPH_W      260  // waveform area width
#define GRAPH_H      190  // waveform area height
#define STATUS_Y     (GRAPH_Y + GRAPH_H + 6)

// --- ADC ---
#define ADC_MAX      4095
#define VREF         3.3f

// --- Sampling ---
#define SAMPLE_BUFFER_SIZE 800

// --- Colors ---
#define COL_BG        TFT_BLACK
#define COL_GRID      0x2945    // dark gray
#define COL_GRID_AXIS 0x4A49    // lighter gray
#define COL_WAVE      TFT_GREEN
#define COL_TRIGGER   TFT_RED
#define COL_TEXT       TFT_WHITE
#define COL_TEXT_DIM   0x7BEF
#define COL_HIGHLIGHT  TFT_YELLOW
#define COL_CYAN       TFT_CYAN

// --- Trigger Modes ---
enum TriggerMode { TRIG_AUTO, TRIG_NORMAL, TRIG_SINGLE };
const char* trigModeNames[] = { "Auto", "Norm", "Sngl" };

// --- Voltage Scale Steps (mV per division) ---
const int vScaleSteps[] = { 100, 200, 500, 1000, 1650 };
const int vScaleCount = sizeof(vScaleSteps) / sizeof(vScaleSteps[0]);

// --- Time Scale Steps (microseconds per division) ---
const int tScaleSteps[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 50000 };
const int tScaleCount = sizeof(tScaleSteps) / sizeof(tScaleSteps[0]);

// --- DMA ADC ---
#define DMA_MAX_RATE      83333   // ESP32-S3 max: 83333 Hz
#define DMA_MIN_RATE      611     // ESP32-S3 min: 611 Hz
#define DMA_FRAME_SIZE    256
#define DMA_RESULT_BYTES  4       // SOC_ADC_DIGI_RESULT_BYTES for ESP32-S3
static bool adcDmaReady = false;
static int  dmaCurRate = 0;       // current configured DMA sample rate

// --- Global State ---
#ifdef USE_EXTERNAL_DISPLAY
M5Canvas canvas(&externalDisplay);
#else
M5Canvas canvas(&M5Cardputer.Display);
#endif

uint16_t sampleBuf[SAMPLE_BUFFER_SIZE];
bool running = true;
bool showFreq = true;

int vScaleIdx = 3;
int tScaleIdx = 6;   // 1000 us/div
int triggerLevel = 2048;
TriggerMode trigMode = TRIG_AUTO;
bool triggerArmed = true;
bool singleTriggered = false;

float measuredFreqHz = 0;
float measuredVpp = 0;
float measuredVmin = 0;
float measuredVmax = 0;
float actualUsPerSample = 10.0f;

bool testSignalOn = true;
int testFreqIdx = 3;
const int testFreqs[] = { 100, 500, 1000, 5000, 10000 };
const int testFreqCount = sizeof(testFreqs) / sizeof(testFreqs[0]);

// --- Calibration ---
bool calibrated = false;
int calAdcZero = 0;         // ADC reading at 0V (LOW)
int calAdcMax = 4095;       // ADC reading at 3.3V (HIGH)
float calFreqFactor = 1.0f; // frequency correction multiplier
float calVref = 3.3f;       // calibrated reference voltage
unsigned long calTimestamp = 0;
const char* calStatus = "";  // status message shown briefly after cal

// --- Forward Declarations ---
void setupFastADC();
void reconfigureDMA(int tScaleIdx);
void sampleADC();
void sampleADC_DMA();
int findTrigger();
void drawGrid();
void drawWaveform(int trigPos);
void drawTriggerLine();
void drawStatusBar();
void drawMeasurements();
void handleKeyboard();
void autoScale();
void runCalibration();
float adcToVolts(int raw);
int voltsToY(float v);
void setupTestSignal(int freqHz);

// =============================================
// Setup
// =============================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Pocket Oscilloscope ADV v1.3.0 ===");

#ifdef USE_EXTERNAL_DISPLAY
    // Step 0: Set all CS pins HIGH before any SPI activity
    pinMode(LCD_CS, OUTPUT);
    pinMode(LCD_DC, OUTPUT);
    pinMode(LCD_RST, OUTPUT);
    digitalWrite(LCD_CS, HIGH);
    digitalWrite(LCD_DC, HIGH);
    digitalWrite(LCD_RST, HIGH);
    Serial.println("CS pins set HIGH");

    // Step 1: Initialize external display (LGFX manages its own SPI bus)
    Serial.println("Initializing external display...");
    if (!externalDisplay.init()) {
        Serial.println("External display init FAILED!");
        while (1) delay(1000);
    }
    externalDisplay.setRotation(0);
    externalDisplay.setColorDepth(16);
    delay(50);
    externalDisplay.fillScreen(TFT_BLACK);
    Serial.printf("External display ready: %ldx%ld\n",
                  (long)externalDisplay.width(), (long)externalDisplay.height());
#endif

    // Step 2: Initialize M5Cardputer AFTER display
    auto cfg = M5.config();
    cfg.output_power = true;
    M5Cardputer.begin(cfg);
    M5Cardputer.Display.setBrightness(40);
    Serial.println("M5Cardputer initialized");

    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.setTextSize(1);

    // Step 3: Release Grove pins (GPIO 1,2) for ADC/signal use
    // Do NOT delete I2C drivers -- keyboard TCA8418 needs I2C on GPIO 8/9
    gpio_reset_pin(GPIO_NUM_1);
    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_pull_mode(GPIO_NUM_1, GPIO_FLOATING);
    gpio_set_pull_mode(GPIO_NUM_2, GPIO_FLOATING);

    // Step 3b: Initialize DMA ADC BEFORE any Arduino ADC calls
    setupFastADC();

    // Step 4: Test signal generator on G2
    gpio_reset_pin(GPIO_NUM_2);
    setupTestSignal(testFreqs[testFreqIdx]);

    Serial.printf("ADC: GPIO %d | Test sig: GPIO %d (%d Hz)\n",
                  ADC_PIN, TEST_SIG_PIN, testFreqs[testFreqIdx]);
    Serial.printf("Display: %dx%d | Graph: %dx%d\n", SCREEN_W, SCREEN_H, GRAPH_W, GRAPH_H);
    Serial.println("Ready! Connect G1 to G2 for self-test.");
}

// =============================================
// Main Loop
// =============================================
void loop() {
    M5Cardputer.update();
    handleKeyboard();

    if (running) {
        sampleADC();
    }

    int trigPos = findTrigger();

    canvas.fillSprite(COL_BG);
    drawGrid();
    drawTriggerLine();
    drawWaveform(trigPos);
    drawMeasurements();
    drawStatusBar();

#ifdef USE_EXTERNAL_DISPLAY
    canvas.pushSprite(&externalDisplay, 0, 0);
#else
    canvas.pushSprite(0, 0);
#endif

    if (trigMode == TRIG_SINGLE && singleTriggered && trigPos >= 0) {
        running = false;
    }

    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 2000) {
        lastDebug = millis();
        Serial.printf("[DBG] Vpp=%.3f Freq=%.0fHz us/s=%.1f DMA:%d/%dHz\n",
                      measuredVpp, measuredFreqHz, actualUsPerSample,
                      adcDmaReady ? 1 : 0, dmaCurRate);
    }
}

// =============================================
// DMA ADC Setup
// =============================================
void setupFastADC() {
    adc_digi_init_config_t init_cfg = {};
    init_cfg.max_store_buf_size = 8192;
    init_cfg.conv_num_each_intr = DMA_FRAME_SIZE;
    init_cfg.adc1_chan_mask = BIT(0);  // ADC1_CHANNEL_0 (GPIO 1)
    init_cfg.adc2_chan_mask = 0;

    esp_err_t err = adc_digi_initialize(&init_cfg);
    if (err != ESP_OK) {
        Serial.printf("[ADC] DMA init failed: %s\n", esp_err_to_name(err));
        adcDmaReady = false;
        return;
    }

    // Configure for max speed initially
    reconfigureDMA(tScaleIdx);
    if (!adcDmaReady) return;

    Serial.printf("[ADC] DMA ready: %d Hz (~%.1f us/sample)\n",
                  dmaCurRate, 1000000.0f / dmaCurRate);
}

// =============================================
// Reconfigure DMA sample rate for current time scale
// =============================================
void reconfigureDMA(int tsIdx) {
    int usPerDiv = tScaleSteps[tsIdx];
    int totalUs = usPerDiv * 5;
    float desiredUsPerSample = (float)totalUs / GRAPH_W;
    int sampleRate = (int)(1000000.0f / desiredUsPerSample);

    if (sampleRate > DMA_MAX_RATE) sampleRate = DMA_MAX_RATE;
    if (sampleRate < DMA_MIN_RATE) sampleRate = DMA_MIN_RATE;

    if (sampleRate == dmaCurRate) return;

    adc_digi_pattern_config_t adc_pattern[1] = {};
    adc_pattern[0].atten = ADC_ATTEN_DB_12;
    adc_pattern[0].channel = 0;         // ADC_CHANNEL_0 -> GPIO 1
    adc_pattern[0].unit = 0;            // ADC_UNIT_1
    adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;  // 12 for ESP32-S3

    adc_digi_configuration_t dig_cfg = {};
    dig_cfg.conv_limit_en = false;
    dig_cfg.conv_limit_num = 0;
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = adc_pattern;
    dig_cfg.sample_freq_hz = sampleRate;
    dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

    esp_err_t err = adc_digi_controller_configure(&dig_cfg);
    if (err != ESP_OK) {
        Serial.printf("[ADC] DMA reconfig to %d Hz failed: %s\n", sampleRate, esp_err_to_name(err));
        if (dmaCurRate == 0) {
            adc_digi_deinitialize();
            adcDmaReady = false;
        }
        return;
    }

    dmaCurRate = sampleRate;
    adcDmaReady = true;
    Serial.printf("[ADC] Rate: %d Hz (%.1f us/s) for %d us/div\n",
                  sampleRate, 1000000.0f / sampleRate, usPerDiv);
}

// =============================================
// ADC Sampling via DMA
// =============================================
void sampleADC_DMA() {
    if (!adcDmaReady) return;

    adc_digi_start();

    uint8_t frameBuf[DMA_FRAME_SIZE];
    uint32_t bytesRead = 0;
    int idx = 0;

    unsigned long t0 = micros();

    while (idx < SAMPLE_BUFFER_SIZE) {
        esp_err_t ret = adc_digi_read_bytes(frameBuf, DMA_FRAME_SIZE, &bytesRead, 100);
        if (ret == ESP_OK) {
            for (int i = 0; i < (int)bytesRead && idx < SAMPLE_BUFFER_SIZE; i += DMA_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&frameBuf[i];
                if (p->type2.channel < ADC_CHANNEL_MAX) {
                    sampleBuf[idx++] = p->type2.data;
                }
            }
        }
    }

    unsigned long elapsed = micros() - t0;
    actualUsPerSample = (float)elapsed / SAMPLE_BUFFER_SIZE;

    adc_digi_stop();
}

// =============================================
// ADC Sampling (with auto DMA rate)
// =============================================
void sampleADC() {
    if (adcDmaReady) {
        reconfigureDMA(tScaleIdx);
        sampleADC_DMA();
    }
}

// =============================================
// Find Trigger Point (rising edge)
// =============================================
int findTrigger() {
    int searchEnd = SAMPLE_BUFFER_SIZE - GRAPH_W - 1;
    if (searchEnd < 1) searchEnd = 1;

    for (int i = 1; i < searchEnd; i++) {
        if (sampleBuf[i - 1] < triggerLevel && sampleBuf[i] >= triggerLevel) {
            if (trigMode == TRIG_SINGLE) singleTriggered = true;
            return i;
        }
    }

    if (trigMode == TRIG_AUTO) return 0;
    return -1;
}

// =============================================
// Draw Grid
// =============================================
void drawGrid() {
    int divX = GRAPH_W / 5;
    int divY = GRAPH_H / 4;

    for (int i = 0; i <= 5; i++) {
        int x = GRAPH_X + i * divX;
        uint16_t col = (i == 0 || i == 5) ? COL_GRID_AXIS : COL_GRID;
        canvas.drawFastVLine(x, GRAPH_Y, GRAPH_H, col);
    }

    for (int i = 0; i <= 4; i++) {
        int y = GRAPH_Y + i * divY;
        uint16_t col = (i == 2) ? COL_GRID_AXIS : COL_GRID;
        canvas.drawFastHLine(GRAPH_X, y, GRAPH_W, col);
    }

    // Dotted center cross
    for (int x = GRAPH_X; x < GRAPH_X + GRAPH_W; x += 4) {
        canvas.drawPixel(x, GRAPH_Y + GRAPH_H / 2, COL_GRID_AXIS);
    }
    for (int y = GRAPH_Y; y < GRAPH_Y + GRAPH_H; y += 4) {
        canvas.drawPixel(GRAPH_X + GRAPH_W / 2, y, COL_GRID_AXIS);
    }
}

// =============================================
// Draw Waveform
// =============================================
void drawWaveform(int trigPos) {
    if (trigPos < 0) return;

    float mvPerDiv = vScaleSteps[vScaleIdx];
    float totalMv = mvPerDiv * 4;
    float vMin = 3.3f, vMax = 0.0f;

    int prevX = -1, prevY = -1;

    for (int i = 0; i < GRAPH_W; i++) {
        int sIdx = trigPos + i;
        if (sIdx >= SAMPLE_BUFFER_SIZE) break;

        float v = adcToVolts(sampleBuf[sIdx]);
        if (v < vMin) vMin = v;
        if (v > vMax) vMax = v;

        int y = voltsToY(v);
        if (y < GRAPH_Y) y = GRAPH_Y;
        if (y > GRAPH_Y + GRAPH_H - 1) y = GRAPH_Y + GRAPH_H - 1;

        int x = GRAPH_X + i;

        if (prevX >= 0) {
            canvas.drawLine(prevX, prevY, x, y, COL_WAVE);
        }
        prevX = x;
        prevY = y;
    }

    measuredVmin = vMin;
    measuredVmax = vMax;
    measuredVpp = vMax - vMin;

    // Frequency measurement
    if (showFreq && trigPos >= 0) {
        float midV = (vMin + vMax) / 2.0f;
        int midRaw = (int)(midV / VREF * ADC_MAX);
        int edgeCount = 0;
        int lastEdgeIdx = 0, firstEdgeIdx = 0;

        for (int i = 1; i < GRAPH_W; i++) {
            int sIdx = trigPos + i;
            if (sIdx >= SAMPLE_BUFFER_SIZE) break;
            if (sampleBuf[sIdx - 1] < midRaw && sampleBuf[sIdx] >= midRaw) {
                if (edgeCount == 0) firstEdgeIdx = i;
                lastEdgeIdx = i;
                edgeCount++;
            }
        }

        if (edgeCount >= 2) {
            float timeBetweenEdges = (lastEdgeIdx - firstEdgeIdx) * actualUsPerSample;
            measuredFreqHz = (edgeCount - 1) / (timeBetweenEdges / 1000000.0f);
            measuredFreqHz *= calFreqFactor;
        } else {
            measuredFreqHz = 0;
        }
    }
}

// =============================================
// Draw Trigger Level Line
// =============================================
void drawTriggerLine() {
    float trigV = adcToVolts(triggerLevel);
    int y = voltsToY(trigV);
    if (y >= GRAPH_Y && y <= GRAPH_Y + GRAPH_H) {
        for (int x = GRAPH_X; x < GRAPH_X + GRAPH_W; x += 6) {
            canvas.drawFastHLine(x, y, 3, COL_TRIGGER);
        }
        canvas.fillTriangle(GRAPH_X - 7, y, GRAPH_X - 1, y - 4, GRAPH_X - 1, y + 4, COL_TRIGGER);
    }
}

// =============================================
// Draw Measurements (right panel)
// =============================================
void drawMeasurements() {
    int x = GRAPH_X + GRAPH_W + 5;
    int y = GRAPH_Y + 2;

    canvas.setTextSize(1);

    // Vpp
    canvas.setTextColor(COL_HIGHLIGHT);
    canvas.setCursor(x, y);
    canvas.printf("Vpp");
    y += 12;
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(x, y);
    canvas.printf("%.2fV", measuredVpp);

    // Vmax
    y += 18;
    canvas.setTextColor(COL_TEXT_DIM);
    canvas.setCursor(x, y);
    canvas.printf("Hi %.2f", measuredVmax);

    // Vmin
    y += 12;
    canvas.setCursor(x, y);
    canvas.printf("Lo %.2f", measuredVmin);

    // Frequency
    if (showFreq) {
        y += 20;
        canvas.setTextColor(COL_CYAN);
        canvas.setCursor(x, y);
        canvas.print("Freq");
        y += 12;
        if (measuredFreqHz >= 1000) {
            canvas.printf("%.1fkHz", measuredFreqHz / 1000.0f);
        } else if (measuredFreqHz > 0) {
            canvas.printf("%.0f Hz", measuredFreqHz);
        } else {
            canvas.print("--- Hz");
        }
    }

    // Test signal
    if (testSignalOn) {
        y += 20;
        canvas.setTextColor(COL_HIGHLIGHT);
        canvas.setCursor(x, y);
        canvas.print("Gen");
        y += 12;
        int tf = testFreqs[testFreqIdx];
        if (tf >= 1000) canvas.printf("%dkHz", tf / 1000);
        else canvas.printf("%dHz", tf);
    }

    // Calibration status
    if (calibrated) {
        y += 20;
        canvas.setTextColor(COL_WAVE);
        canvas.setCursor(x, y);
        canvas.print("CAL");
        y += 12;
        canvas.setTextColor(COL_TEXT_DIM);
        canvas.setCursor(x, y);
        canvas.printf("x%.3f", calFreqFactor);
    }

    // Flash calibration result message
    if (calTimestamp > 0 && millis() - calTimestamp < 3000) {
        canvas.setTextSize(2);
        int cx = GRAPH_X + GRAPH_W / 2 - 40;
        int cy = GRAPH_Y + GRAPH_H / 2 - 8;
        bool isOk = (calStatus[4] == 'O');
        canvas.setTextColor(isOk ? COL_WAVE : COL_TRIGGER);
        canvas.setCursor(cx, cy);
        canvas.print(calStatus);
        canvas.setTextSize(1);
    }

    // Voltage labels on left (Y axis)
    float mvPerDiv = vScaleSteps[vScaleIdx];
    canvas.setTextColor(COL_TEXT_DIM);
    canvas.setTextSize(1);

    for (int i = 0; i <= 4; i++) {
        float v = (mvPerDiv * 4 - i * mvPerDiv) / 1000.0f;
        int ly = GRAPH_Y + i * (GRAPH_H / 4) - 3;
        canvas.setCursor(0, ly);
        if (v >= 1.0f) canvas.printf("%.1fV", v);
        else canvas.printf("%dmV", (int)(v * 1000));
    }
}

// =============================================
// Draw Status Bar
// =============================================
void drawStatusBar() {
    int y = STATUS_Y;
    canvas.setTextSize(1);

    // Run/Stop
    if (running) {
        canvas.setTextColor(COL_WAVE);
        canvas.setCursor(0, y);
        canvas.print("RUN");
    } else {
        canvas.setTextColor(COL_TRIGGER);
        canvas.setCursor(0, y);
        canvas.print("STOP");
    }

    // Time scale
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(40, y);
    int usPerDiv = tScaleSteps[tScaleIdx];
    if (usPerDiv >= 1000) canvas.printf("T: %d ms/div", usPerDiv / 1000);
    else canvas.printf("T: %d us/div", usPerDiv);

    // Voltage scale
    canvas.setCursor(145, y);
    int mvPerDiv = vScaleSteps[vScaleIdx];
    if (mvPerDiv >= 1000) canvas.printf("V: %.1f V/div", mvPerDiv / 1000.0f);
    else canvas.printf("V: %d mV/div", mvPerDiv);

    // DMA rate indicator
    canvas.setCursor(225, y);
    if (adcDmaReady) {
        canvas.setTextColor(COL_WAVE);
        if (dmaCurRate >= 1000) canvas.printf("%dkS", dmaCurRate / 1000);
        else canvas.printf("%dS", dmaCurRate);
    } else {
        canvas.setTextColor(COL_TRIGGER);
        canvas.print("noADC");
    }

    // Trigger mode & level
    canvas.setCursor(265, y);
    canvas.setTextColor(COL_TRIGGER);
    canvas.printf("TR:%s", trigModeNames[trigMode]);

    // Key hints
    y += 14;
    canvas.setTextColor(COL_TEXT_DIM);
    canvas.setCursor(0, y);
    canvas.print("R:run  ;.T  +-V  A:auto  C:cal  G:sig  H/J:Hz  M:mode");
}

// =============================================
// Keyboard Handler
// =============================================
void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    if (!M5Cardputer.Keyboard.isPressed()) return;

    if (M5Cardputer.Keyboard.isKeyPressed('r')) {
        running = !running;
        if (running && trigMode == TRIG_SINGLE) {
            singleTriggered = false;
            triggerArmed = true;
        }
    }

    if (M5Cardputer.Keyboard.isKeyPressed('a')) autoScale();

    if (M5Cardputer.Keyboard.isKeyPressed('t')) {
        triggerLevel += 200;
        if (triggerLevel > ADC_MAX) triggerLevel = ADC_MAX;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('y')) {
        triggerLevel -= 200;
        if (triggerLevel < 0) triggerLevel = 0;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('=')) {
        if (vScaleIdx > 0) vScaleIdx--;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('-')) {
        if (vScaleIdx < vScaleCount - 1) vScaleIdx++;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        if (tScaleIdx > 0) tScaleIdx--;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        if (tScaleIdx < tScaleCount - 1) tScaleIdx++;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('f')) showFreq = !showFreq;

    if (M5Cardputer.Keyboard.isKeyPressed('m')) {
        trigMode = (TriggerMode)((trigMode + 1) % 3);
        if (trigMode == TRIG_SINGLE) {
            singleTriggered = false;
            triggerArmed = true;
        }
    }

    if (M5Cardputer.Keyboard.isKeyPressed('c')) {
        runCalibration();
    }

    if (M5Cardputer.Keyboard.isKeyPressed('g')) {
        testSignalOn = !testSignalOn;
        if (testSignalOn) {
            setupTestSignal(testFreqs[testFreqIdx]);
        } else {
            ledcDetachPin(TEST_SIG_PIN);
            gpio_reset_pin(GPIO_NUM_2);
            gpio_set_pull_mode(GPIO_NUM_2, GPIO_FLOATING);
        }
    }

    if (M5Cardputer.Keyboard.isKeyPressed('h')) {
        if (testFreqIdx < testFreqCount - 1) testFreqIdx++;
        if (testSignalOn) setupTestSignal(testFreqs[testFreqIdx]);
    }

    if (M5Cardputer.Keyboard.isKeyPressed('j')) {
        if (testFreqIdx > 0) testFreqIdx--;
        if (testSignalOn) setupTestSignal(testFreqs[testFreqIdx]);
    }
}

// =============================================
// Calibration
// =============================================
void runCalibration() {
    Serial.println("\n[CAL] === Starting calibration ===");

    // Save and set time scale to 1ms/div for 1kHz signal
    int savedTscale = tScaleIdx;
    tScaleIdx = 6;  // 1000 us/div (index 6 in new array)

    // Collect multiple frames for averaging
    const int calFrames = 8;
    long sumMin = 0, sumMax = 0;
    float sumFreq = 0;
    int validFreqFrames = 0;

    for (int frame = 0; frame < calFrames; frame++) {
        sampleADC();

        int fMin = ADC_MAX, fMax = 0;
        for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
            if (sampleBuf[i] < fMin) fMin = sampleBuf[i];
            if (sampleBuf[i] > fMax) fMax = sampleBuf[i];
        }
        sumMin += fMin;
        sumMax += fMax;

        // Measure frequency (raw, uncalibrated)
        int mid = (fMin + fMax) / 2;
        int edgeCount = 0;
        int firstEdge = 0, lastEdge = 0;
        for (int i = 1; i < SAMPLE_BUFFER_SIZE; i++) {
            if (sampleBuf[i - 1] < mid && sampleBuf[i] >= mid) {
                if (edgeCount == 0) firstEdge = i;
                lastEdge = i;
                edgeCount++;
            }
        }
        if (edgeCount >= 2) {
            float dt = (lastEdge - firstEdge) * actualUsPerSample;
            float freq = (edgeCount - 1) / (dt / 1000000.0f);
            sumFreq += freq;
            validFreqFrames++;
        }

        delay(20);
    }

    int avgMin = sumMin / calFrames;
    int avgMax = sumMax / calFrames;
    int vpp = avgMax - avgMin;

    Serial.printf("[CAL] ADC min=%d  max=%d  span=%d\n", avgMin, avgMax, vpp);

    if (vpp < 500) {
        calStatus = "CAL FAIL: no signal";
        calTimestamp = millis();
        tScaleIdx = savedTscale;
        Serial.println("[CAL] FAILED - no signal (Vpp too low). Connect G1 to signal source!");
        return;
    }

    // Voltage calibration
    calAdcZero = avgMin;
    calAdcMax = avgMax;
    calVref = 3.3f;
    Serial.printf("[CAL] Voltage: zero=%d  max=%d  vref=%.2fV\n",
                  calAdcZero, calAdcMax, calVref);

    // Frequency calibration: snap measured freq to nearest standard value
    if (validFreqFrames > 0) {
        float avgFreq = sumFreq / validFreqFrames;
        const float stdFreqs[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};
        float expectedFreq = 0;
        float bestDist = 999999;
        for (int i = 0; i < 12; i++) {
            float dist = fabs(avgFreq - stdFreqs[i]) / stdFreqs[i];
            if (dist < bestDist) {
                bestDist = dist;
                expectedFreq = stdFreqs[i];
            }
        }

        Serial.printf("[CAL] Frequency: raw=%.1f Hz  nearest=%.0f Hz  error=%.1f%%\n",
                      avgFreq, expectedFreq, bestDist * 100);

        if (bestDist < 0.10f) {
            calFreqFactor = expectedFreq / avgFreq;
            Serial.printf("[CAL] Freq calibration OK: factor=%.4f\n", calFreqFactor);
        } else {
            Serial.printf("[CAL] Freq too far from standard (%.0f%%), factor unchanged\n",
                          bestDist * 100);
        }
    }

    calibrated = true;
    calStatus = "CAL OK";
    calTimestamp = millis();
    tScaleIdx = savedTscale;

    Serial.printf("[CAL] === Calibration complete ===\n");
    Serial.printf("[CAL] ADC: %d-%d | FreqCal: x%.4f\n",
                  calAdcZero, calAdcMax, calFreqFactor);
}

// =============================================
// Auto-Scale
// =============================================
void autoScale() {
    int minVal = ADC_MAX, maxVal = 0;
    for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++) {
        if (sampleBuf[i] < minVal) minVal = sampleBuf[i];
        if (sampleBuf[i] > maxVal) maxVal = sampleBuf[i];
    }

    float vMin = adcToVolts(minVal);
    float vMax = adcToVolts(maxVal);
    float vRange = (vMax - vMin) * 1000.0f;
    float needed = vRange * 1.2f / 4.0f;

    for (int i = 0; i < vScaleCount; i++) {
        if (vScaleSteps[i] >= needed) {
            vScaleIdx = i;
            break;
        }
    }

    triggerLevel = (minVal + maxVal) / 2;
}

// =============================================
// Helpers
// =============================================
float adcToVolts(int raw) {
    if (calibrated) {
        int span = calAdcMax - calAdcZero;
        if (span < 100) span = 4095;
        return (float)(raw - calAdcZero) / span * calVref;
    }
    return (float)raw / ADC_MAX * VREF;
}

int voltsToY(float v) {
    float mvPerDiv = vScaleSteps[vScaleIdx];
    float totalV = mvPerDiv * 4.0f / 1000.0f;
    float ratio = v / totalV;
    return GRAPH_Y + GRAPH_H - 1 - (int)(ratio * (GRAPH_H - 1));
}

void setupTestSignal(int freqHz) {
    int bits = 10;
    if (freqHz >= 5000) bits = 8;
    else if (freqHz >= 1000) bits = 10;
    else bits = 14;

    ledcSetup(0, freqHz, bits);
    ledcAttachPin(TEST_SIG_PIN, 0);
    ledcWrite(0, 1 << (bits - 1));
}
