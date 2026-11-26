#include "RadarModel.h"

#include <algorithm>
#include <cmath>

namespace radar_app {

int clampRssi(int rssi) {
    return std::max(-100, std::min(-30, rssi));
}

int signalPercent(int rssi) {
    return ((clampRssi(rssi) + 100) * 100) / 70;
}

float estimateDistanceMetres(int rssi, int measuredPower) {
    if (rssi >= 0) return -1.0f;
    const float ratio = static_cast<float>(rssi) / static_cast<float>(measuredPower);
    if (ratio < 1.0f) return std::pow(ratio, 10.0f);
    return 0.89976f * std::pow(ratio, 7.7095f) + 0.111f;
}

void sortStrongestFirst(SignalTarget* targets, size_t count) {
    if (!targets || count < 2) return;
    std::sort(targets, targets + count,
              [](const SignalTarget& lhs, const SignalTarget& rhs) {
                  if (lhs.rssi != rhs.rssi) return lhs.rssi > rhs.rssi;
                  return lhs.lastSeenMs > rhs.lastSeenMs;
              });
}

}  // namespace radar_app
