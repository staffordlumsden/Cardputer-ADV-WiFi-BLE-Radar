#pragma once

#include <lgfx/v1/panel/Panel_LCD.hpp>

namespace cardputer_os {

// Local ILI9341 panel definition inheriting M5GFX's lgfx::Panel_LCD,
// because M5GFX strips non-M5Stack LovyanGFX panel headers.
struct Panel_ILI9341_Local : public lgfx::Panel_LCD {
    Panel_ILI9341_Local() {
        _cfg.memory_width  = _cfg.panel_width  = 240;
        _cfg.memory_height = _cfg.panel_height = 320;
    }

    // Match the known-working Cardputer ADV external-screen reference.  The
    // generic Panel_LCD base does not know the ILI9341 wire format, so make the
    // write/read depths explicit before the bus starts sending pixel data.
    void setColorDepth_impl(lgfx::color_depth_t depth) override {
        (void)depth;
        _write_depth = lgfx::rgb565_2Byte;
        _read_depth = lgfx::rgb888_3Byte;
    }

protected:
    const uint8_t* getInitCommands(uint8_t listno) const override {
        static constexpr uint8_t list0[] = {
            0xC0, 1, 0x23,              // Power control VRH[5:0]
            0xC1, 1, 0x10,              // Power control SAP[2:0];BT[3:0]
            0xC5, 2, 0x3E, 0x28,        // VCM control
            0xC7, 1, 0x86,              // VCM control2
            0x3A, 1, 0x55,              // Pixel Format: 16-bit RGB565
            0xB1, 2, 0x00, 0x18,        // Frame rate control
            0xB6, 3, 0x08, 0x82, 0x27,  // Display Function Control
            0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
            0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
            CMD_SLPOUT, 0 + CMD_INIT_DELAY, 120,
            CMD_IDMOFF, 0,
            CMD_DISPON, 0 + CMD_INIT_DELAY, 100,
            0xFF, 0xFF,
        };
        return (listno == 0) ? list0 : nullptr;
    }
};

}  // namespace cardputer_os
