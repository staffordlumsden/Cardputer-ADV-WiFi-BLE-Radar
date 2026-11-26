#include "DisplayManager.h"

#include <M5Cardputer.h>
#include <cstring>

#include "StorageManager.h"

namespace cardputer_os {

bool ExternalLgfxDevice::configureAndInit(const ExternalDisplayConfig& cfg) {
    releasePanel();

    // Keep external CS inactive before bus setup.
    pinMode(cfg.pins.cs, OUTPUT);
    digitalWrite(cfg.pins.cs, HIGH);

    auto busCfg = bus_.config();
    busCfg.spi_host = SPI3_HOST;  // HSPI host used by the working 2.8-inch XL firmware
    busCfg.spi_mode = 0;
    busCfg.freq_write = cfg.frequency;
    busCfg.freq_read = 16000000;
    busCfg.spi_3wire = (cfg.pins.miso < 0);
    busCfg.use_lock = true;
    busCfg.dma_channel = SPI_DMA_CH_AUTO;
    busCfg.pin_sclk = cfg.pins.sck;
    busCfg.pin_mosi = cfg.pins.mosi;
    busCfg.pin_miso = cfg.pins.miso;
    busCfg.pin_dc = cfg.pins.dc;
    bus_.config(busCfg);

    if (std::strcmp(cfg.controller, "ILI9341") == 0) {
        panel_ = new Panel_ILI9341_Local();
    } else if (std::strcmp(cfg.controller, "ST7789") == 0) {
        panel_ = new lgfx::Panel_ST7789();
    } else {
        return false;
    }

    panel_->setBus(&bus_);

    auto panelCfg = panel_->config();
    panelCfg.pin_cs = cfg.pins.cs;
    panelCfg.pin_rst = cfg.pins.rst;
    panelCfg.pin_busy = -1;
    // Preserve native portrait panel/memory geometry for ILI9341 (240x320);
    // rotation (e.g. 1 -> 320x240) is applied via setRotation().
    if (std::strcmp(cfg.controller, "ILI9341") != 0) {
        panelCfg.panel_width = cfg.width;
        panelCfg.panel_height = cfg.height;
        panelCfg.memory_width = cfg.width;
        panelCfg.memory_height = cfg.height;
    }
    panelCfg.offset_x = 0;
    panelCfg.offset_y = 0;
    panelCfg.offset_rotation = 4;
    panelCfg.dummy_read_pixel = 8;
    panelCfg.dummy_read_bits = 1;
    panelCfg.readable = (cfg.pins.miso >= 0);
    panelCfg.invert = false;
    panelCfg.rgb_order = false;
    panelCfg.dlen_16bit = false;
    panelCfg.bus_shared = true;  // Critical: shares SPI bus with SD
    panel_->config(panelCfg);

    setPanel(panel_);
    if (!init()) {
        releasePanel();
        return false;
    }

    setRotation(cfg.rotation);
    initialized_ = true;
    return true;
}

void ExternalLgfxDevice::releasePanel() {
    if (panel_ != nullptr) {
        setPanel(nullptr);
        delete panel_;
        panel_ = nullptr;
    }
    initialized_ = false;
}

bool DisplayManager::begin(const DisplayProfile& profile, SemaphoreHandle_t sharedSpiMutex) {
    spiMutex_ = sharedSpiMutex;
    internal_ = &M5Cardputer.Display;
    internal_->setRotation(1);
    M5Cardputer.Display.setBrightness(128);
    internal_->fillScreen(TFT_BLACK);

    return applyProfile(profile);
}

bool DisplayManager::applyProfile(const DisplayProfile& profile) {
    lastError_[0] = '\0';
    ValidationResult v = validateDisplayProfile(profile);
    if (!v.ok) {
        std::strncpy(lastError_, v.error, sizeof(lastError_) - 1);
        lastError_[sizeof(lastError_) - 1] = '\0';
        restoreInternalOnly();
        return false;
    }

    profile_ = profile;
    externalReady_ = false;
    externalDevice_.releasePanel();

    if (!profile_.external.enabled || profile_.mode == DisplayMode::InternalOnly) {
        return true;
    }

    SpiBusLock lock(spiMutex_);
    if (!lock.locked()) {
        std::strncpy(lastError_, "SPI bus mutex timeout during external init", sizeof(lastError_) - 1);
        restoreInternalOnly();
        return false;
    }

    // Allow the external module's supply and controller reset to settle.
    delay(100);
    if (!externalDevice_.configureAndInit(profile_.external)) {
        std::strncpy(lastError_, "External panel init failed; fell back to internal", sizeof(lastError_) - 1);
        restoreInternalOnly();
        return false;
    }

    externalReady_ = true;
    return true;
}

void DisplayManager::restoreInternalOnly() {
    externalDevice_.releasePanel();
    externalReady_ = false;
    profile_.external.enabled = false;
    profile_.mode = DisplayMode::InternalOnly;
    profile_.internalRole = InternalDisplayRole::Primary;
    activeTarget_ = DisplayTarget::Internal;
}

bool DisplayManager::setExternalRotation(uint8_t rotation) {
    if (!externalReady_ || rotation > 7) {
        return false;
    }
    SpiBusLock lock(spiMutex_);
    if (!lock.locked()) {
        return false;
    }
    profile_.external.rotation = rotation;
    externalDevice_.setRotation(rotation);
    return true;
}

bool DisplayManager::renderExternal(ExternalRenderCallback renderer, void* context) {
    if (!externalReady_ || renderer == nullptr) {
        return false;
    }
    SpiBusLock lock(spiMutex_);
    if (!lock.locked()) {
        return false;
    }
    externalDevice_.startWrite();
    renderer(externalDevice_, context);
    externalDevice_.endWrite();
    return true;
}

bool DisplayManager::setActiveTarget(DisplayTarget target) {
    if (target == DisplayTarget::External && !externalReady_) {
        activeTarget_ = DisplayTarget::Internal;
        return false;
    }
    activeTarget_ = target;
    return true;
}

bool DisplayManager::toggleActiveTarget() {
    return setActiveTarget(activeTarget_ == DisplayTarget::Internal
                               ? DisplayTarget::External
                               : DisplayTarget::Internal);
}

bool DisplayManager::renderExternalTestPattern() {
    if (!externalReady_) {
        return false;
    }
    SpiBusLock lock(spiMutex_);
    if (!lock.locked()) {
        return false;
    }

    const int w = externalDevice_.width();
    const int h = externalDevice_.height();

    externalDevice_.startWrite();
    externalDevice_.fillScreen(TFT_BLACK);

    // White perimeter and centre lines expose clipping, offsets and rotation.
    externalDevice_.drawRect(0, 0, w, h, TFT_WHITE);
    externalDevice_.fillRect(w / 2, 30, 1, h - 38, TFT_DARKGREY);
    externalDevice_.fillRect(4, h / 2, w - 8, 1, TFT_DARKGREY);

    // Header bar
    externalDevice_.fillRect(0, 0, w, 28, TFT_NAVY);
    externalDevice_.setTextColor(TFT_WHITE, TFT_NAVY);
    externalDevice_.setTextSize(1);
    externalDevice_.setCursor(8, 8);
    externalDevice_.printf("ILI9341  %dx%d  ROT:%u",
                           w, h, profile_.external.rotation);

    // Eight labelled RGB565 patches make colour order and missing channels
    // easy to inspect without allocating a full-screen framebuffer.
    const uint16_t colors[] = {
        TFT_RED, TFT_GREEN, TFT_BLUE, TFT_CYAN,
        TFT_MAGENTA, TFT_YELLOW, TFT_WHITE, TFT_BLACK
    };
    const char* labels[] = {"R", "G", "B", "C", "M", "Y", "W", "K"};
    const int count = sizeof(colors) / sizeof(colors[0]);
    const int barY = 38;
    const int barH = 46;
    for (int i = 0; i < count; ++i) {
        const int x0 = (i * w) / count;
        const int x1 = ((i + 1) * w) / count;
        externalDevice_.fillRect(x0 + 1, barY, x1 - x0 - 2, barH, colors[i]);
        externalDevice_.setTextColor(TFT_WHITE, TFT_BLACK);
        externalDevice_.setCursor(x0 + 3, barY + barH + 4);
        externalDevice_.print(labels[i]);
    }

    // Distinct corner markers expose orientation and address-window errors.
    const int marker = 12;
    externalDevice_.fillRect(4, 32, marker, marker, TFT_RED);
    externalDevice_.fillRect(w - marker - 4, 32, marker, marker, TFT_GREEN);
    externalDevice_.fillRect(4, h - marker - 32, marker, marker, TFT_BLUE);
    externalDevice_.fillRect(w - marker - 4, h - marker - 32, marker, marker, TFT_YELLOW);

    externalDevice_.setTextColor(TFT_GREEN, TFT_BLACK);
    externalDevice_.setCursor(12, h - 24);
    externalDevice_.printf("SPI %lu MHz  SDO OFF",
                           static_cast<unsigned long>(profile_.external.frequency / 1000000UL));
    externalDevice_.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    externalDevice_.setCursor(12, h - 12);
    externalDevice_.print("TFT test pattern");
    externalDevice_.endWrite();
    return true;
}

uint16_t DisplayManager::internalWidth() const {
    return internal_ ? internal_->width() : 240;
}

uint16_t DisplayManager::internalHeight() const {
    return internal_ ? internal_->height() : 135;
}

uint16_t DisplayManager::externalWidth() const {
    return externalReady_ ? externalDevice_.width() : 0;
}

uint16_t DisplayManager::externalHeight() const {
    return externalReady_ ? externalDevice_.height() : 0;
}

}  // namespace cardputer_os
