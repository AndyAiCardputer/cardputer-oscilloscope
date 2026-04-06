#ifndef LGFX_ILI9341_H
#define LGFX_ILI9341_H

#ifdef USE_EXTERNAL_DISPLAY

#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_LCD.hpp>
#include "../pins.h"

struct Panel_ILI9341_Local : public lgfx::v1::Panel_LCD {
    Panel_ILI9341_Local(void) {
        _cfg.memory_width  = _cfg.panel_width  = 240;
        _cfg.memory_height = _cfg.panel_height = 320;
    }

    void setColorDepth_impl(lgfx::v1::color_depth_t depth) override {
        (void)depth;
        _write_depth = lgfx::v1::color_depth_t::rgb565_2Byte;
        _read_depth  = lgfx::v1::color_depth_t::rgb565_2Byte;
    }

protected:
    static constexpr uint8_t CMD_FRMCTR1 = 0xB1;
    static constexpr uint8_t CMD_INVCTR  = 0xB4;
    static constexpr uint8_t CMD_DFUNCTR = 0xB6;
    static constexpr uint8_t CMD_PWCTR1  = 0xC0;
    static constexpr uint8_t CMD_PWCTR2  = 0xC1;
    static constexpr uint8_t CMD_VMCTR1  = 0xC5;
    static constexpr uint8_t CMD_VMCTR2  = 0xC7;
    static constexpr uint8_t CMD_GMCTRP1 = 0xE0;
    static constexpr uint8_t CMD_GMCTRN1 = 0xE1;

    const uint8_t* getInitCommands(uint8_t listno) const override {
        static constexpr uint8_t list0[] = {
            0xEF       , 3, 0x03,0x80,0x02,
            0xCF       , 3, 0x00,0xC1,0x30,
            0xED       , 4, 0x64,0x03,0x12,0x81,
            0xE8       , 3, 0x85,0x00,0x78,
            0xCB       , 5, 0x39,0x2C,0x00,0x34,0x02,
            0xF7       , 1, 0x20,
            0xEA       , 2, 0x00,0x00,
            CMD_PWCTR1,  1, 0x23,
            CMD_PWCTR2,  1, 0x10,
            CMD_VMCTR1,  2, 0x3e,0x28,
            CMD_VMCTR2,  1, 0x86,
            CMD_FRMCTR1, 2, 0x00,0x13,
            0xF2       , 1, 0x00,
            CMD_GAMMASET,1, 0x01,
            CMD_GMCTRP1,15, 0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00,
            CMD_GMCTRN1,15, 0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F,
            CMD_DFUNCTR, 3, 0x08,0xC2,0x27,
            CMD_SLPOUT , 0+CMD_INIT_DELAY, 120,
            CMD_IDMOFF , 0,
            CMD_DISPON , 0+CMD_INIT_DELAY, 100,
            0xFF,0xFF,
        };
        switch (listno) {
        case 0: return list0;
        default: return nullptr;
        }
    }
};

class LGFX_ILI9341 : public lgfx::v1::LGFX_Device {
    Panel_ILI9341_Local panel;
    lgfx::v1::Bus_SPI   bus;

public:
    LGFX_ILI9341() {
        pinMode(LCD_CS, OUTPUT);
        digitalWrite(LCD_CS, HIGH);
        pinMode(LCD_DC, OUTPUT);
        digitalWrite(LCD_DC, HIGH);
        pinMode(LCD_RST, OUTPUT);
        digitalWrite(LCD_RST, HIGH);

        auto b = bus.config();
        b.spi_host   = SPI3_HOST;
        b.spi_mode   = 0;
        b.freq_write = 20000000;
        b.freq_read  = 16000000;
        b.spi_3wire  = true;
        b.use_lock   = true;
        b.dma_channel = 0;
        b.pin_sclk = PIN_SCK;
        b.pin_mosi = PIN_MOSI;
        b.pin_miso = -1;
        b.pin_dc   = LCD_DC;
        bus.config(b);
        panel.setBus(&bus);

        auto p = panel.config();
        p.pin_cs    = LCD_CS;
        p.pin_rst   = LCD_RST;
        p.bus_shared = true;
        p.readable   = false;
        p.invert     = false;
        p.rgb_order  = false;
        p.dlen_16bit = false;
        p.memory_width  = 240;
        p.memory_height = 320;
        p.panel_width   = 240;
        p.panel_height  = 320;
        p.offset_x = 0;
        p.offset_y = 0;
        p.offset_rotation = 1;  // landscape: 320x240
        p.dummy_read_pixel = 8;
        p.dummy_read_bits = 1;
        panel.config(p);

        setPanel(&panel);
    }
};

extern LGFX_ILI9341 externalDisplay;

#endif
#endif
