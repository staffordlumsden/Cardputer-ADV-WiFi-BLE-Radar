#pragma once

#include <cstddef>
#include <cstdint>

namespace radar_app {

enum class RadioKind : uint8_t { Wifi = 0, Ble = 1 };

struct SignalTarget {
    RadioKind kind = RadioKind::Wifi;
    char name[33] = {0};
    char address[18] = {0};
    int16_t rssi = -127;
    uint8_t channel = 0;
    bool secured = false;
    uint32_t lastSeenMs = 0;
};

int clampRssi(int rssi);
int signalPercent(int rssi);
float estimateDistanceMetres(int rssi, int measuredPower = -59);
void sortStrongestFirst(SignalTarget* targets, size_t count);

}  // namespace radar_app
