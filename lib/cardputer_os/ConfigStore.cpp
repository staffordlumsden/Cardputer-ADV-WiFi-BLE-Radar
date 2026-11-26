#include "ConfigStore.h"

#include <ArduinoJson.h>
#include <Preferences.h>

namespace cardputer_os {

bool ConfigStore::loadFromNvs(DisplayProfile& outProfile) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        outProfile = defaultDisplayProfile();
        return false;
    }

    DisplayProfile loaded = defaultDisplayProfile();
    loaded.schemaVersion = prefs.getUShort("disp_ver", 1);
    loaded.external.enabled = prefs.getBool("ext_en", false);
    String ctrl = prefs.getString("ext_ctrl", "ILI9341");
    std::strncpy(loaded.external.controller, ctrl.c_str(), sizeof(loaded.external.controller) - 1);
    loaded.external.controller[sizeof(loaded.external.controller) - 1] = '\0';
    loaded.external.width = prefs.getUShort("ext_w", 320);
    loaded.external.height = prefs.getUShort("ext_h", 240);
    loaded.external.rotation = prefs.getUChar("ext_rot", 1);
    loaded.external.frequency = prefs.getULong("ext_freq", 40000000);
    loaded.external.pins.sck = prefs.getChar("pin_sck", 40);
    loaded.external.pins.mosi = prefs.getChar("pin_mosi", 14);
    loaded.external.pins.miso = prefs.getChar("pin_miso", -1);
    loaded.external.pins.cs = prefs.getChar("pin_cs", 5);
    loaded.external.pins.dc = prefs.getChar("pin_dc", 6);
    loaded.external.pins.rst = prefs.getChar("pin_rst", 3);
    loaded.mode = static_cast<DisplayMode>(prefs.getUChar("disp_mode", 0));
    loaded.internalRole = static_cast<InternalDisplayRole>(prefs.getUChar("int_role", 0));
    prefs.end();

    ValidationResult v = validateDisplayProfile(loaded);
    if (!v.ok) {
        outProfile = defaultDisplayProfile();
        return false;
    }
    outProfile = loaded;
    return true;
}

bool ConfigStore::saveToNvs(const DisplayProfile& profile) {
    ValidationResult v = validateDisplayProfile(profile);
    if (!v.ok) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        return false;
    }

    prefs.putUShort("disp_ver", profile.schemaVersion);
    prefs.putBool("ext_en", profile.external.enabled);
    prefs.putString("ext_ctrl", profile.external.controller);
    prefs.putUShort("ext_w", profile.external.width);
    prefs.putUShort("ext_h", profile.external.height);
    prefs.putUChar("ext_rot", profile.external.rotation);
    prefs.putULong("ext_freq", profile.external.frequency);
    prefs.putChar("pin_sck", profile.external.pins.sck);
    prefs.putChar("pin_mosi", profile.external.pins.mosi);
    prefs.putChar("pin_miso", profile.external.pins.miso);
    prefs.putChar("pin_cs", profile.external.pins.cs);
    prefs.putChar("pin_dc", profile.external.pins.dc);
    prefs.putChar("pin_rst", profile.external.pins.rst);
    prefs.putUChar("disp_mode", static_cast<uint8_t>(profile.mode));
    prefs.putUChar("int_role", static_cast<uint8_t>(profile.internalRole));
    prefs.end();
    return true;
}

bool ConfigStore::resetNvsToDefault() {
    return saveToNvs(defaultDisplayProfile());
}

ValidationResult ConfigStore::parseJson(const char* jsonText, DisplayProfile& outProfile) {
    ValidationResult res;
    if (!jsonText) {
        std::strncpy(res.error, "Null JSON input", sizeof(res.error) - 1);
        return res;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonText);
    if (err) {
        std::snprintf(res.error, sizeof(res.error), "JSON parse error: %s", err.c_str());
        return res;
    }

    DisplayProfile candidate = defaultDisplayProfile();
    candidate.schemaVersion = doc["schema_version"] | 1;

    JsonObject ext = doc["external_display"];
    if (!ext.isNull()) {
        candidate.external.enabled = ext["enabled"] | false;
        const char* ctrl = ext["controller"] | "ILI9341";
        std::strncpy(candidate.external.controller, ctrl, sizeof(candidate.external.controller) - 1);
        candidate.external.controller[sizeof(candidate.external.controller) - 1] = '\0';
        candidate.external.width = ext["width"] | 320;
        candidate.external.height = ext["height"] | 240;
        candidate.external.rotation = ext["rotation"] | 1;
        candidate.external.frequency = ext["frequency"] | 40000000UL;

        JsonObject pins = ext["pins"];
        if (!pins.isNull()) {
            candidate.external.pins.sck = pins["sck"] | 40;
            candidate.external.pins.mosi = pins["mosi"] | 14;
            candidate.external.pins.miso = pins["miso"] | -1;
            candidate.external.pins.cs = pins["cs"] | 5;
            candidate.external.pins.dc = pins["dc"] | 6;
            candidate.external.pins.rst = pins["rst"] | 3;
        }
    }

    const char* modeStr = doc["display_mode"] | "internal_only";
    if (!parseDisplayMode(modeStr, candidate.mode)) {
        std::strncpy(res.error, "Unknown display_mode", sizeof(res.error) - 1);
        return res;
    }

    const char* roleStr = doc["internal_display"] | "primary";
    if (!parseInternalRole(roleStr, candidate.internalRole)) {
        std::strncpy(res.error, "Unknown internal_display role", sizeof(res.error) - 1);
        return res;
    }

    res = validateDisplayProfile(candidate);
    if (res.ok) {
        outProfile = candidate;
    }
    return res;
}

String ConfigStore::serializeJson(const DisplayProfile& profile) {
    JsonDocument doc;
    doc["schema_version"] = profile.schemaVersion;

    JsonObject ext = doc["external_display"].to<JsonObject>();
    ext["enabled"] = profile.external.enabled;
    ext["controller"] = profile.external.controller;
    ext["width"] = profile.external.width;
    ext["height"] = profile.external.height;
    ext["rotation"] = profile.external.rotation;
    ext["frequency"] = profile.external.frequency;

    JsonObject pins = ext["pins"].to<JsonObject>();
    pins["sck"] = profile.external.pins.sck;
    pins["mosi"] = profile.external.pins.mosi;
    pins["miso"] = profile.external.pins.miso;
    pins["cs"] = profile.external.pins.cs;
    pins["dc"] = profile.external.pins.dc;
    pins["rst"] = profile.external.pins.rst;

    doc["display_mode"] = displayModeToString(profile.mode);
    doc["internal_display"] = internalRoleToString(profile.internalRole);

    String out;
    serializeJsonPretty(doc, out);
    return out;
}

}  // namespace cardputer_os
