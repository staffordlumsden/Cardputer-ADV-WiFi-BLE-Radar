#pragma once

#include "DisplayProfile.h"
#include <Arduino.h>

namespace cardputer_os {

class ConfigStore {
public:
    static constexpr const char* kNvsNamespace = "cardputer_os";
    static constexpr const char* kDisplayJsonPath = "/config/display.json";

    bool loadFromNvs(DisplayProfile& outProfile);
    bool saveToNvs(const DisplayProfile& profile);
    bool resetNvsToDefault();

    ValidationResult parseJson(const char* jsonText, DisplayProfile& outProfile);
    String serializeJson(const DisplayProfile& profile);
};

}  // namespace cardputer_os
