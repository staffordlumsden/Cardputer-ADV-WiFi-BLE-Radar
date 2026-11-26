#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace cardputer_os {

class SpiBusLock {
public:
    explicit SpiBusLock(SemaphoreHandle_t mutex, TickType_t timeoutTicks = pdMS_TO_TICKS(500));
    ~SpiBusLock();

    SpiBusLock(const SpiBusLock&) = delete;
    SpiBusLock& operator=(const SpiBusLock&) = delete;

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_ = false;
};

class StorageManager {
public:
    static constexpr int8_t kSdCsPin = 12;
    static constexpr int8_t kSdSckPin = 40;
    static constexpr int8_t kSdMosiPin = 14;
    static constexpr int8_t kSdMisoPin = 39;

    StorageManager();

    // Configure the shared HSPI/SPI3 bus without mounting the SD card.
    bool beginBus();
    bool begin(uint32_t spiFreqHz = 16000000);
    void end();

    bool isMounted() const { return mounted_ && !usbOwned_; }
    bool isUsbOwned() const { return usbOwned_; }
    uint64_t totalBytes() const;
    uint64_t usedBytes() const;

    // Transfer exclusive block-level ownership to a USB MSC host. The FatFs
    // mount stays dormant while raw sectors are exported, then is fully
    // reinitialised when USB ownership ends so no stale FAT cache survives.
    bool beginUsbAccess();
    bool endUsbAccess();
    uint32_t usbSectorCount() const { return usbOwned_ ? usbSectorCount_ : 0; }
    uint16_t usbSectorSize() const { return usbOwned_ ? usbSectorSize_ : 0; }
    bool readUsb(uint32_t lba, uint32_t offset, void* buffer, uint32_t size);
    bool writeUsb(uint32_t lba, uint32_t offset, const uint8_t* buffer, uint32_t size);

    SemaphoreHandle_t spiMutex() const { return spiMutex_; }
    SPIClass& sharedSpi() { return spi_; }

    static bool isSafeRelativePath(const char* path);
    bool ensureAppDataDir(const char* appId, String& outDir);

private:
    SPIClass spi_;
    SemaphoreHandle_t spiMutex_;
    bool busInitialized_ = false;
    bool mounted_ = false;
    bool usbOwned_ = false;
    uint32_t mountFrequencyHz_ = 16000000;
    uint32_t usbSectorCount_ = 0;
    uint16_t usbSectorSize_ = 0;
};

}  // namespace cardputer_os
