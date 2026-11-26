#pragma once

#include <cstdint>

namespace cardputer_os {

struct AppIdentity {
    const char* id = "unknown";
    const char* name = "Unknown App";
    const char* version = "0.1.0";
    uint16_t sdkVersion = 1;
};

class AppLifecycle {
public:
    // Cooperative prototype recovery: arms the factory partition as the next boot
    // target as early as possible in app startup.
    // Limitation: cannot recover automatically from a crash before this runs
    // or from non-cooperative third-party firmware; full non-cooperative
    // recovery requires bootloader-level one-shot boot support (Milestone 0.5).
    static bool commitCooperativeRecovery();

    // Sets factory as the next boot partition and restarts the device.
    [[noreturn]] static void returnToLauncher();

    // Launcher-side helper: selects the named OTA partition (e.g. "ota_0")
    // and restarts if validation succeeds.
    static bool launchOtaPartition(const char* partitionLabel);

    static const char* runningPartitionLabel();
};

}  // namespace cardputer_os
