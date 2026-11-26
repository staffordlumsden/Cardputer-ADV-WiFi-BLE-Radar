#include "DisplayProfile.h"

namespace cardputer_os {

namespace {

bool isReservedAdvPin(int8_t pin) {
    // Built-in ST7789V2 (33..38), TCA8418 I2C/INT (8, 9, 11), Battery ADC (10),
    // IR TX (44), Audio I2S (41, 42, 43, 46), and SD CS (12).
    switch (pin) {
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 33:
        case 34:
        case 35:
        case 36:
        case 37:
        case 38:
        case 41:
        case 42:
        case 43:
        case 44:
        case 46:
            return true;
        default:
            return false;
    }
}

bool isValidEsp32S3Gpio(int8_t pin) {
    // ESP32-S3FN8 valid GPIOs (excluding USB D-/D+ 19,20 and flash SPI 26..32)
    if (pin < 0 || pin > 48) return false;
    if (pin == 19 || pin == 20) return false;
    if (pin >= 26 && pin <= 32) return false;
    return true;
}

bool isExposedControlPin(int8_t pin) {
    return pin == 3 || pin == 4 || pin == 5 || pin == 6 || pin == 13 || pin == 15;
}

void setError(ValidationResult& res, const char* msg) {
    res.ok = false;
    std::strncpy(res.error, msg, sizeof(res.error) - 1);
    res.error[sizeof(res.error) - 1] = '\0';
}

}  // namespace

const char* displayModeToString(DisplayMode mode) {
    switch (mode) {
        case DisplayMode::InternalOnly: return "internal_only";
        case DisplayMode::ExternalPrimary: return "external_primary";
        case DisplayMode::Independent: return "independent";
        case DisplayMode::Mirror: return "mirror";
        default: return "internal_only";
    }
}

bool parseDisplayMode(const char* str, DisplayMode& out) {
    if (!str) return false;
    if (std::strcmp(str, "internal_only") == 0) { out = DisplayMode::InternalOnly; return true; }
    if (std::strcmp(str, "external_primary") == 0) { out = DisplayMode::ExternalPrimary; return true; }
    if (std::strcmp(str, "independent") == 0) { out = DisplayMode::Independent; return true; }
    if (std::strcmp(str, "mirror") == 0) { out = DisplayMode::Mirror; return true; }
    return false;
}

const char* internalRoleToString(InternalDisplayRole role) {
    switch (role) {
        case InternalDisplayRole::Primary: return "primary";
        case InternalDisplayRole::Status: return "status";
        case InternalDisplayRole::Off: return "off";
        default: return "primary";
    }
}

bool parseInternalRole(const char* str, InternalDisplayRole& out) {
    if (!str) return false;
    if (std::strcmp(str, "primary") == 0) { out = InternalDisplayRole::Primary; return true; }
    if (std::strcmp(str, "status") == 0) { out = InternalDisplayRole::Status; return true; }
    if (std::strcmp(str, "off") == 0) { out = InternalDisplayRole::Off; return true; }
    return false;
}

bool isSupportedController(const char* controller) {
    if (!controller) return false;
    return std::strcmp(controller, "ILI9341") == 0 ||
           std::strcmp(controller, "ILI9488") == 0 ||
           std::strcmp(controller, "ST7789") == 0 ||
           std::strcmp(controller, "ST7796") == 0;
}

ValidationResult validateDisplayProfile(const DisplayProfile& profile) {
    ValidationResult res;
    if (profile.schemaVersion != 1) {
        setError(res, "Unsupported schema_version (expected 1)");
        return res;
    }

    if (profile.mode != DisplayMode::InternalOnly && profile.mode != DisplayMode::Independent) {
        setError(res, "Display mode not implemented in prototype");
        return res;
    }
    if (profile.internalRole != InternalDisplayRole::Primary && profile.internalRole != InternalDisplayRole::Status) {
        setError(res, "Internal display must remain available");
        return res;
    }

    if (!profile.external.enabled) {
        res.ok = true;
        return res;
    }

    if (std::strcmp(profile.external.controller, "ILI9341") != 0) {
        setError(res, "Unsupported external display controller");
        return res;
    }
    if (profile.external.width == 0 || profile.external.width > 800 ||
        profile.external.height == 0 || profile.external.height > 800) {
        setError(res, "External display dimensions out of range (1..800)");
        return res;
    }
    if (profile.external.rotation > 7) {
        setError(res, "External display rotation must be 0..7");
        return res;
    }
    if (profile.external.frequency < 1000000UL || profile.external.frequency > 40000000UL) {
        setError(res, "External SPI frequency must be 1 MHz..40 MHz");
        return res;
    }

    const auto& p = profile.external.pins;
    if (p.sck != 40 || p.mosi != 14 || (p.miso != 39 && p.miso != -1)) {
        setError(res, "External SPI must use the onboard SD bus pins");
        return res;
    }
    const bool xlWiring = p.dc == 6 && p.rst == 3;
    const bool guicmgWiring = p.dc == 15 && p.rst == 13;
    if (p.cs != 5 || (!xlWiring && !guicmgWiring) ||
        !isExposedControlPin(p.dc) || !isExposedControlPin(p.rst)) {
        setError(res, "Use a supported CS/DC/RST wiring profile");
        return res;
    }
    if (p.cs == 40 || p.cs == 14 || p.cs == 39 || p.dc == 40 || p.dc == 14 || p.dc == 39 || p.rst == 40 || p.rst == 14 || p.rst == 39) {
        setError(res, "Control pin conflicts with shared SD bus");
        return res;
    }
    if (!isValidEsp32S3Gpio(p.sck) || !isValidEsp32S3Gpio(p.mosi) ||
        !isValidEsp32S3Gpio(p.cs) || !isValidEsp32S3Gpio(p.dc)) {
        setError(res, "Invalid ESP32-S3 GPIO for SCK/MOSI/CS/DC");
        return res;
    }
    if (p.miso != -1 && !isValidEsp32S3Gpio(p.miso)) {
        setError(res, "Invalid ESP32-S3 GPIO for MISO");
        return res;
    }
    if (p.rst != -1 && !isValidEsp32S3Gpio(p.rst)) {
        setError(res, "Invalid ESP32-S3 GPIO for RST");
        return res;
    }
    if (isReservedAdvPin(p.cs) || isReservedAdvPin(p.dc) || (p.rst != -1 && isReservedAdvPin(p.rst))) {
        setError(res, "CS/DC/RST conflicts with reserved Cardputer ADV pin");
        return res;
    }
    if (p.cs == p.dc || (p.rst != -1 && (p.rst == p.cs || p.rst == p.dc))) {
        setError(res, "CS, DC, and RST pins must be distinct");
        return res;
    }
    if (p.cs == kSdCsPin || p.dc == kSdCsPin || p.rst == kSdCsPin) {
        setError(res, "Pin conflicts with onboard SD CS (GPIO12)");
        return res;
    }

    res.ok = true;
    return res;
}

DisplayProfile defaultDisplayProfile() {
    DisplayProfile profile;
    profile.schemaVersion = 1;
    profile.external.enabled = false;
    std::strncpy(profile.external.controller, "ILI9341", sizeof(profile.external.controller) - 1);
    profile.external.width = 320;
    profile.external.height = 240;
    profile.external.rotation = 1;
    profile.external.frequency = 40000000;
    profile.external.pins.sck = 40;
    profile.external.pins.mosi = 14;
    profile.external.pins.miso = -1;
    profile.external.pins.cs = 5;
    profile.external.pins.dc = 6;
    profile.external.pins.rst = 3;
    profile.mode = DisplayMode::InternalOnly;
    profile.internalRole = InternalDisplayRole::Primary;
    return profile;
}

}  // namespace cardputer_os
