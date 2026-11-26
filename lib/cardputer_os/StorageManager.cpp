#include "StorageManager.h"

#include <algorithm>
#include <climits>
#include <cstring>

namespace cardputer_os {

SpiBusLock::SpiBusLock(SemaphoreHandle_t mutex, TickType_t timeoutTicks) : mutex_(mutex) {
    if (mutex_ != nullptr) {
        locked_ = (xSemaphoreTakeRecursive(mutex_, timeoutTicks) == pdTRUE);
    } else {
        locked_ = false;
    }
}

SpiBusLock::~SpiBusLock() {
    if (mutex_ != nullptr && locked_) {
        xSemaphoreGiveRecursive(mutex_);
    }
}

StorageManager::StorageManager()
    : spi_(HSPI), spiMutex_(xSemaphoreCreateRecursiveMutex()) {}

bool StorageManager::beginBus() {
    SpiBusLock lock(spiMutex_);
    if (!lock.locked()) {
        return false;
    }

    // Deselect the proposed external panel before touching the shared bus.
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    // Keep SD CS inactive before bus initialization.
    pinMode(kSdCsPin, OUTPUT);
    digitalWrite(kSdCsPin, HIGH);

    if (!busInitialized_) {
        spi_.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, -1);
        busInitialized_ = true;
    }

    return true;
}

bool StorageManager::begin(uint32_t spiFreqHz) {
    if (usbOwned_) {
        return false;
    }
    if (!beginBus()) {
        return false;
    }

    SpiBusLock lock(spiMutex_);
    if (!lock.locked()) {
        return false;
    }

    if (mounted_) {
        return true;
    }

    mountFrequencyHz_ = spiFreqHz;
    mounted_ = SD.begin(kSdCsPin, spi_, spiFreqHz);
    return mounted_;
}

void StorageManager::end() {
    SpiBusLock lock(spiMutex_);
    if (lock.locked() && mounted_ && !usbOwned_) {
        SD.end();
        mounted_ = false;
    }
}

uint64_t StorageManager::totalBytes() const {
    if (!mounted_ || usbOwned_) return 0;
    return SD.totalBytes();
}

uint64_t StorageManager::usedBytes() const {
    if (!mounted_ || usbOwned_) return 0;
    return SD.usedBytes();
}

bool StorageManager::beginUsbAccess() {
    SpiBusLock lock(spiMutex_);
    if (!lock.locked() || !mounted_ || usbOwned_) {
        return false;
    }

    const size_t sectorSize = SD.sectorSize();
    const size_t sectorCount = SD.numSectors();
    if (sectorSize == 0 || sectorSize > 512 || sectorCount == 0 ||
        sectorCount > UINT32_MAX) {
        return false;
    }

    usbSectorSize_ = static_cast<uint16_t>(sectorSize);
    usbSectorCount_ = static_cast<uint32_t>(sectorCount);
    usbOwned_ = true;
    return true;
}

bool StorageManager::endUsbAccess() {
    SpiBusLock lock(spiMutex_, pdMS_TO_TICKS(1500));
    if (!lock.locked()) {
        return false;
    }
    if (!usbOwned_) {
        return mounted_;
    }

    usbOwned_ = false;
    usbSectorCount_ = 0;
    usbSectorSize_ = 0;

    // Raw host writes bypass FatFs. Recreate the SD driver and mount so cached
    // allocation tables cannot survive after macOS ejects the card.
    if (mounted_) {
        SD.end();
        mounted_ = false;
    }
    // GPIO5 is the external-panel CS on this ADV build. Keeping it inactive is
    // also required for reliable Cardputer ADV SD remounts.
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);
    pinMode(kSdCsPin, OUTPUT);
    digitalWrite(kSdCsPin, HIGH);
    mounted_ = SD.begin(kSdCsPin, spi_, mountFrequencyHz_);
    return mounted_;
}

bool StorageManager::readUsb(uint32_t lba,
                             uint32_t offset,
                             void* buffer,
                             uint32_t size) {
    if (!buffer || size == 0) return size == 0;
    SpiBusLock lock(spiMutex_, pdMS_TO_TICKS(1000));
    if (!lock.locked() || !usbOwned_ || usbSectorSize_ == 0) return false;

    const uint64_t start = static_cast<uint64_t>(lba) * usbSectorSize_ + offset;
    const uint64_t capacity = static_cast<uint64_t>(usbSectorCount_) * usbSectorSize_;
    if (start + size > capacity) return false;

    uint8_t scratch[512];
    uint8_t* destination = static_cast<uint8_t*>(buffer);
    uint32_t sector = static_cast<uint32_t>(start / usbSectorSize_);
    uint32_t sectorOffset = static_cast<uint32_t>(start % usbSectorSize_);
    uint32_t remaining = size;
    while (remaining > 0) {
        const uint32_t chunk = std::min<uint32_t>(remaining, usbSectorSize_ - sectorOffset);
        if (sectorOffset == 0 && chunk == usbSectorSize_) {
            if (!SD.readRAW(destination, sector)) return false;
        } else {
            if (!SD.readRAW(scratch, sector)) return false;
            std::memcpy(destination, scratch + sectorOffset, chunk);
        }
        destination += chunk;
        remaining -= chunk;
        sector++;
        sectorOffset = 0;
    }
    return true;
}

bool StorageManager::writeUsb(uint32_t lba,
                              uint32_t offset,
                              const uint8_t* buffer,
                              uint32_t size) {
    if (!buffer || size == 0) return size == 0;
    SpiBusLock lock(spiMutex_, pdMS_TO_TICKS(1000));
    if (!lock.locked() || !usbOwned_ || usbSectorSize_ == 0) return false;

    const uint64_t start = static_cast<uint64_t>(lba) * usbSectorSize_ + offset;
    const uint64_t capacity = static_cast<uint64_t>(usbSectorCount_) * usbSectorSize_;
    if (start + size > capacity) return false;

    uint8_t scratch[512];
    const uint8_t* source = buffer;
    uint32_t sector = static_cast<uint32_t>(start / usbSectorSize_);
    uint32_t sectorOffset = static_cast<uint32_t>(start % usbSectorSize_);
    uint32_t remaining = size;
    while (remaining > 0) {
        const uint32_t chunk = std::min<uint32_t>(remaining, usbSectorSize_ - sectorOffset);
        if (sectorOffset == 0 && chunk == usbSectorSize_) {
            // Arduino's SD API predates const-correct raw writes.
            if (!SD.writeRAW(const_cast<uint8_t*>(source), sector)) return false;
        } else {
            if (!SD.readRAW(scratch, sector)) return false;
            std::memcpy(scratch + sectorOffset, source, chunk);
            if (!SD.writeRAW(scratch, sector)) return false;
        }
        source += chunk;
        remaining -= chunk;
        sector++;
        sectorOffset = 0;
    }
    return true;
}

bool StorageManager::isSafeRelativePath(const char* path) {
    if (!path || path[0] == '\0' || path[0] == '/') {
        return false;
    }
    if (std::strstr(path, "..") != nullptr) {
        return false;
    }
    if (std::strchr(path, '\\') != nullptr) {
        return false;
    }
    return true;
}

bool StorageManager::ensureAppDataDir(const char* appId, String& outDir) {
    if (!mounted_ || usbOwned_ || !isSafeRelativePath(appId)) {
        return false;
    }
    SpiBusLock lock(spiMutex_);
    if (!lock.locked()) {
        return false;
    }
    if (!SD.exists("/data")) {
        SD.mkdir("/data");
    }
    outDir = String("/data/") + appId;
    if (!SD.exists(outDir.c_str())) {
        return SD.mkdir(outDir.c_str());
    }
    return true;
}

}  // namespace cardputer_os
