#ifndef PINS_H
#define PINS_H

// SD card pins (Cardputer-Adv)
#define SD_SCK  40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS   12

// External display ILI9341 pins (EXT 2.54-14P connector)
#define PIN_SCK   40  // SCK  -> EXT PIN 7 (shared with SD)
#define PIN_MOSI  14  // MOSI -> EXT PIN 9 (shared with SD)
#define PIN_MISO  39  // MISO (not used for display)
#define LCD_CS    5   // CS   -> EXT PIN 13
#define LCD_DC    6   // DC   -> EXT PIN 5
#define LCD_RST   3   // RST  -> EXT PIN 1

// ADC input (Grove Port A)
#define ADC_PIN      1   // GPIO 1 = Grove G1 (ADC1_CH0)
#define TEST_SIG_PIN 2   // GPIO 2 = Grove G2 (test signal output)

#endif
