#pragma once

#include <Arduino.h>
#include <M5GFX.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>

#include "DisplayProfile.h"
#include "Panel_ILI9341_Local.hpp"

namespace cardputer_os {

enum class DisplayTarget : uint8_t {
    Internal = 0,
    External = 1,
};

using ExternalRenderCallback = void (*)(LovyanGFX& display, void* context);

class ExternalLgfxDevice : public lgfx::LGFX_Device {
public:
    bool configureAndInit(const ExternalDisplayConfig& cfg);
    void releasePanel();

private:
    lgfx::Bus_SPI bus_{};
    lgfx::Panel_Device* panel_ = nullptr;
    bool initialized_ = false;
};

class DisplayManager {
public:
    bool begin(const DisplayProfile& profile, SemaphoreHandle_t sharedSpiMutex = nullptr);
    void setSharedSpiMutex(SemaphoreHandle_t mutex) { spiMutex_ = mutex; }

    LovyanGFX& internal() { return *internal_; }
    LovyanGFX* external() { return externalReady_ ? &externalDevice_ : nullptr; }

    bool isExternalEnabled() const { return profile_.external.enabled; }
    bool isExternalReady() const { return externalReady_; }
    const DisplayProfile& profile() const { return profile_; }
    const char* lastError() const { return lastError_; }

    bool applyProfile(const DisplayProfile& profile);
    void restoreInternalOnly();
    bool setExternalRotation(uint8_t rotation);

    // Run an external renderer while the shared SD/display SPI mutex is held.
    // UI code must use this instead of drawing through external() directly.
    bool renderExternal(ExternalRenderCallback renderer, void* context = nullptr);

    DisplayTarget activeTarget() const { return activeTarget_; }
    bool setActiveTarget(DisplayTarget target);
    bool toggleActiveTarget();

    // Renders colour bars, geometry, text, and rotation info on the external
    // panel without allocating a full-screen framebuffer.
    bool renderExternalTestPattern();

    uint16_t internalWidth() const;
    uint16_t internalHeight() const;
    uint16_t externalWidth() const;
    uint16_t externalHeight() const;

private:
    LovyanGFX* internal_ = nullptr;
    ExternalLgfxDevice externalDevice_{};
    DisplayProfile profile_{};
    SemaphoreHandle_t spiMutex_ = nullptr;
    bool externalReady_ = false;
    DisplayTarget activeTarget_ = DisplayTarget::Internal;
    char lastError_[96] = {0};
};

}  // namespace cardputer_os
