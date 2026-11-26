#pragma once

#include <cstdint>
#include <cstring>

namespace cardputer_os {

enum class DisplayMode : uint8_t {
    InternalOnly = 0,
    ExternalPrimary = 1,
    Independent = 2,
    Mirror = 3,
};

enum class InternalDisplayRole : uint8_t {
    Primary = 0,
    Status = 1,
    Off = 2,
};

struct ExternalSpiPins {
    int8_t sck = 40;
    int8_t mosi = 14;
    // External test is write-only; MISO is shared with the onboard SD socket.
    int8_t miso = -1;
    int8_t cs = 5;
    int8_t dc = 6;
    int8_t rst = 3;
};

struct ExternalDisplayConfig {
    bool enabled = false;
    char controller[16] = "ILI9341";
    uint16_t width = 320;
    uint16_t height = 240;
    uint8_t rotation = 1;
    uint32_t frequency = 40000000;
    ExternalSpiPins pins{};
};

struct DisplayProfile {
    uint16_t schemaVersion = 1;
    ExternalDisplayConfig external{};
    DisplayMode mode = DisplayMode::InternalOnly;
    InternalDisplayRole internalRole = InternalDisplayRole::Primary;
};

struct ValidationResult {
    bool ok = false;
    char error[96] = {0};
};

// Cardputer ADV reserved pins that must not be reassigned to external TFT control.
static constexpr int8_t kSdCsPin = 12;
static constexpr int8_t kStrappingPinGpio3 = 3;

const char* displayModeToString(DisplayMode mode);
bool parseDisplayMode(const char* str, DisplayMode& out);

const char* internalRoleToString(InternalDisplayRole role);
bool parseInternalRole(const char* str, InternalDisplayRole& out);

bool isSupportedController(const char* controller);
ValidationResult validateDisplayProfile(const DisplayProfile& profile);

DisplayProfile defaultDisplayProfile();

}  // namespace cardputer_os
