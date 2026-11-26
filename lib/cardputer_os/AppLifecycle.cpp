#include "AppLifecycle.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace cardputer_os {

bool AppLifecycle::commitCooperativeRecovery() {
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (!factory) {
        return false;
    }
    return esp_ota_set_boot_partition(factory) == ESP_OK;
}

void AppLifecycle::returnToLauncher() {
    commitCooperativeRecovery();
    delay(50);
    esp_restart();
    while (true) {
        delay(1000);
    }
}

bool AppLifecycle::launchOtaPartition(const char* partitionLabel) {
    if (!partitionLabel) {
        return false;
    }
    const esp_partition_t* target = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, partitionLabel);
    if (!target) {
        return false;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        return false;
    }
    delay(50);
    esp_restart();
    return true;
}

const char* AppLifecycle::runningPartitionLabel() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running ? running->label : "unknown";
}

}  // namespace cardputer_os
