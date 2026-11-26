#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <WiFi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "AppLifecycle.h"
#include "ConfigStore.h"
#include "DisplayManager.h"
#include "DisplayProfile.h"
#include "KeyboardManager.h"
#include "RadarModel.h"
#include "StorageManager.h"

using namespace cardputer_os;
using namespace radar_app;

namespace {

namespace int_colour {
constexpr uint16_t Peak = 0xFE6C;
constexpr uint16_t Main = 0xFD80;
constexpr uint16_t Mid = 0xC3C3;
constexpr uint16_t Dim = 0x7A21;
constexpr uint16_t Grid = 0x3900;
constexpr uint16_t Off = 0x0820;
constexpr uint16_t Black = 0x0000;
}  // namespace int_colour

// The attached ILI9341 needs the same warmer, red-dominant calibration used by
// the accepted launcher and Weather app.
namespace ext_colour {
constexpr uint16_t Peak = 0xFB00;
constexpr uint16_t Main = 0xF900;
constexpr uint16_t Mid = 0xBA40;
constexpr uint16_t Dim = 0x7100;
constexpr uint16_t Grid = 0x3000;
constexpr uint16_t Off = 0x0800;
constexpr uint16_t Black = 0x0000;
}  // namespace ext_colour

constexpr size_t kMaxTargets = 40;
constexpr uint32_t kRenderIntervalMs = 200;

struct RadioSnapshot {
    SignalTarget wifi[kMaxTargets]{};
    SignalTarget ble[kMaxTargets]{};
    size_t wifiCount = 0;
    size_t bleCount = 0;
    bool scanningWifi = true;
    uint32_t revision = 0;
};

enum class View : uint8_t { List = 0, Target };

ConfigStore gConfig;
StorageManager gStorage;
DisplayManager gDisplay;
KeyboardManager gKeyboard;
DisplayProfile gProfile;
SemaphoreHandle_t gSnapshotMutex = nullptr;
RadioSnapshot gShared{};
RadioSnapshot gView{};
TaskHandle_t gScanTask = nullptr;
View gCurrentView = View::List;
RadioKind gSelectedKind = RadioKind::Wifi;
size_t gSelection = 0;
uint32_t gRenderedRevision = UINT32_MAX;
uint32_t gLastRenderMs = 0;
float gSweepAngle = 0.0f;
char gStatus[64] = "STARTING PASSIVE SCAN";
uint32_t gStatusUntilMs = 0;

void copyText(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0) return;
    std::strncpy(destination, source ? source : "", capacity - 1);
    destination[capacity - 1] = '\0';
}

void setStatus(const char* status, uint32_t durationMs = 4000) {
    copyText(gStatus, sizeof(gStatus), status);
    gStatusUntilMs = millis() + durationMs;
    gRenderedRevision = UINT32_MAX;
}

void fillTarget(SignalTarget& target,
                RadioKind kind,
                const char* name,
                const char* address,
                int rssi,
                uint8_t channel,
                bool secured) {
    target.kind = kind;
    copyText(target.name, sizeof(target.name), name && name[0] ? name : "(unnamed)");
    copyText(target.address, sizeof(target.address), address ? address : "--");
    target.rssi = static_cast<int16_t>(rssi);
    target.channel = channel;
    target.secured = secured;
    target.lastSeenMs = millis();
}

void publishWifi(const SignalTarget* targets, size_t count) {
    if (xSemaphoreTake(gSnapshotMutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
    gShared.wifiCount = std::min(count, kMaxTargets);
    for (size_t i = 0; i < gShared.wifiCount; ++i) gShared.wifi[i] = targets[i];
    gShared.scanningWifi = false;
    ++gShared.revision;
    xSemaphoreGive(gSnapshotMutex);
}

void publishBle(const SignalTarget* targets, size_t count) {
    if (xSemaphoreTake(gSnapshotMutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
    gShared.bleCount = std::min(count, kMaxTargets);
    for (size_t i = 0; i < gShared.bleCount; ++i) gShared.ble[i] = targets[i];
    gShared.scanningWifi = true;
    ++gShared.revision;
    xSemaphoreGive(gSnapshotMutex);
}

void scanWorker(void*) {
    BLEDevice::init("");
    BLEScan* bleScan = BLEDevice::getScan();
    bleScan->setActiveScan(false);
    bleScan->setInterval(120);
    bleScan->setWindow(80);

    SignalTarget wifiTargets[kMaxTargets]{};
    SignalTarget bleTargets[kMaxTargets]{};

    for (;;) {
        if (xSemaphoreTake(gSnapshotMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            gShared.scanningWifi = true;
            ++gShared.revision;
            xSemaphoreGive(gSnapshotMutex);
        }

        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false, false);
        const int wifiFound = WiFi.scanNetworks(false, true, false, 240, 0);
        size_t wifiCount = 0;
        if (wifiFound > 0) {
            wifiCount = std::min(static_cast<size_t>(wifiFound), kMaxTargets);
            for (size_t i = 0; i < wifiCount; ++i) {
                const wifi_auth_mode_t encryption = WiFi.encryptionType(static_cast<int>(i));
                fillTarget(wifiTargets[i], RadioKind::Wifi,
                           WiFi.SSID(static_cast<int>(i)).c_str(),
                           WiFi.BSSIDstr(static_cast<int>(i)).c_str(),
                           WiFi.RSSI(static_cast<int>(i)),
                           static_cast<uint8_t>(WiFi.channel(static_cast<int>(i))),
                           encryption != WIFI_AUTH_OPEN);
            }
            sortStrongestFirst(wifiTargets, wifiCount);
        }
        WiFi.scanDelete();
        publishWifi(wifiTargets, wifiCount);

        BLEScanResults results = bleScan->start(2, false);
        const int bleFound = results.getCount();
        size_t bleCount = bleFound > 0
                              ? std::min(static_cast<size_t>(bleFound), kMaxTargets)
                              : 0;
        for (size_t i = 0; i < bleCount; ++i) {
            BLEAdvertisedDevice device = results.getDevice(static_cast<int>(i));
            const std::string deviceName = device.haveName() ? device.getName() : "";
            const std::string address = device.getAddress().toString();
            fillTarget(bleTargets[i], RadioKind::Ble, deviceName.c_str(), address.c_str(),
                       device.getRSSI(), 0, false);
        }
        sortStrongestFirst(bleTargets, bleCount);
        bleScan->clearResults();
        publishBle(bleTargets, bleCount);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void refreshSnapshot() {
    if (xSemaphoreTake(gSnapshotMutex, 0) != pdTRUE) return;
    if (gView.revision != gShared.revision) gView = gShared;
    xSemaphoreGive(gSnapshotMutex);
    const size_t count = gSelectedKind == RadioKind::Wifi ? gView.wifiCount : gView.bleCount;
    if (count == 0) gSelection = 0;
    else if (gSelection >= count) gSelection = count - 1;
}

const SignalTarget* selectedTarget() {
    const SignalTarget* list = gSelectedKind == RadioKind::Wifi ? gView.wifi : gView.ble;
    const size_t count = gSelectedKind == RadioKind::Wifi ? gView.wifiCount : gView.bleCount;
    return gSelection < count ? &list[gSelection] : nullptr;
}

void drawSignalBar(LovyanGFX& d, int x, int y, int width, int height, int rssi,
                   uint16_t active, uint16_t inactive) {
    const int segments = 12;
    const int gap = 2;
    const int segmentWidth = std::max(1, (width - (segments - 1) * gap) / segments);
    const int filled = (signalPercent(rssi) * segments + 99) / 100;
    for (int i = 0; i < segments; ++i) {
        d.fillRect(x + i * (segmentWidth + gap), y, segmentWidth, height,
                   i < filled ? active : inactive);
    }
}

void drawInternal() {
    auto& d = gDisplay.internal();
    d.fillScreen(int_colour::Off);
    d.drawRect(0, 0, d.width(), d.height(), int_colour::Peak);
    d.setTextColor(int_colour::Peak, int_colour::Off);
    d.setTextSize(2);
    d.setCursor(6, 5);
    d.print("RADAR");
    d.setTextSize(1);
    d.setTextColor(int_colour::Mid, int_colour::Off);
    d.setCursor(158, 8);
    d.printf("W%u B%u", static_cast<unsigned>(gView.wifiCount),
             static_cast<unsigned>(gView.bleCount));
    d.drawFastHLine(1, 22, d.width() - 2, int_colour::Mid);

    const SignalTarget* target = selectedTarget();
    d.setTextColor(int_colour::Main, int_colour::Off);
    d.setTextSize(2);
    d.setCursor(6, 31);
    d.print(gSelectedKind == RadioKind::Wifi ? "WI-FI" : "BLE");
    d.setTextSize(1);
    d.setCursor(6, 56);
    d.print(target ? target->name : "SCANNING...");
    if (target) {
        d.setCursor(6, 73);
        d.printf("%d dBm  %d%%", target->rssi, signalPercent(target->rssi));
        drawSignalBar(d, 6, 89, 226, 9, target->rssi,
                      int_colour::Peak, int_colour::Grid);
    }
    d.setTextColor(int_colour::Mid, int_colour::Off);
    d.setCursor(6, 113);
    d.print(gStatus[0] ? gStatus :
            (gCurrentView == View::List ? "</> RADIO  ENTER TRACK" : "ESC LIST  Q HOME"));
}

void drawHeader(LovyanGFX& d, const char* mode) {
    d.fillScreen(ext_colour::Off);
    d.drawRect(0, 0, d.width(), d.height(), ext_colour::Peak);
    d.setTextSize(1);
    d.setTextColor(ext_colour::Peak, ext_colour::Off);
    d.setCursor(8, 6);
    d.print("WI-FI + BLE RADAR");
    d.setTextColor(ext_colour::Mid, ext_colour::Off);
    d.setCursor(246, 6);
    d.print(mode);
    d.drawFastHLine(1, 18, d.width() - 2, ext_colour::Mid);
}

void drawListExternal(LovyanGFX& d) {
    drawHeader(d, gView.scanningWifi ? "SCAN WIFI" : "SCAN BLE");
    const SignalTarget* targets = gSelectedKind == RadioKind::Wifi ? gView.wifi : gView.ble;
    const size_t count = gSelectedKind == RadioKind::Wifi ? gView.wifiCount : gView.bleCount;

    d.setTextSize(1);
    d.setTextColor(gSelectedKind == RadioKind::Wifi ? ext_colour::Black : ext_colour::Mid,
                   gSelectedKind == RadioKind::Wifi ? ext_colour::Main : ext_colour::Off);
    d.fillRect(8, 25, 72, 14,
               gSelectedKind == RadioKind::Wifi ? ext_colour::Main : ext_colour::Off);
    d.drawRect(8, 25, 72, 14, ext_colour::Mid);
    d.setCursor(17, 29);
    d.printf("WI-FI %u", static_cast<unsigned>(gView.wifiCount));
    d.setTextColor(gSelectedKind == RadioKind::Ble ? ext_colour::Black : ext_colour::Mid,
                   gSelectedKind == RadioKind::Ble ? ext_colour::Main : ext_colour::Off);
    d.fillRect(84, 25, 72, 14,
               gSelectedKind == RadioKind::Ble ? ext_colour::Main : ext_colour::Off);
    d.drawRect(84, 25, 72, 14, ext_colour::Mid);
    d.setCursor(102, 29);
    d.printf("BLE %u", static_cast<unsigned>(gView.bleCount));

    d.setTextColor(ext_colour::Dim, ext_colour::Off);
    d.setCursor(166, 29);
    d.print("PASSIVE DISCOVERY");

    const size_t first = gSelection >= 12 ? gSelection - 11 : 0;
    for (size_t row = 0; row < 12 && first + row < count; ++row) {
        const size_t index = first + row;
        const SignalTarget& target = targets[index];
        const int y = 47 + static_cast<int>(row) * 13;
        const bool active = index == gSelection;
        if (active) d.fillRect(7, y - 2, 306, 12, ext_colour::Main);
        d.setTextColor(active ? ext_colour::Black : ext_colour::Main,
                       active ? ext_colour::Main : ext_colour::Off);
        d.setCursor(10, y);
        char name[23] = {0};
        copyText(name, sizeof(name), target.name);
        d.printf("%c %-22s %4d", active ? '>' : ' ', name, target.rssi);
        d.setCursor(244, y);
        if (target.kind == RadioKind::Wifi) {
            d.printf("CH%02u %s", target.channel, target.secured ? "SEC" : "OPEN");
        } else {
            d.printf("%3d%% BLE", signalPercent(target.rssi));
        }
    }

    if (count == 0) {
        d.setTextSize(2);
        d.setTextColor(ext_colour::Mid, ext_colour::Off);
        d.setCursor(76, 105);
        d.print("SCANNING...");
    }

    d.drawFastHLine(1, 222, d.width() - 2, ext_colour::Mid);
    d.setTextSize(1);
    d.setTextColor(ext_colour::Main, ext_colour::Off);
    d.setCursor(8, 228);
    d.print(gStatus[0] ? gStatus : "</> RADIO  ;/. SELECT  ENTER TRACK  L LOG  Q HOME");
}

void drawRadarExternal(LovyanGFX& d) {
    drawHeader(d, "TARGET");
    const SignalTarget* target = selectedTarget();
    if (!target) {
        d.setTextColor(ext_colour::Mid, ext_colour::Off);
        d.setTextSize(2);
        d.setCursor(70, 105);
        d.print("TARGET LOST");
        return;
    }

    const int cx = 102;
    const int cy = 118;
    const int radius = 78;
    for (int r = 20; r <= radius; r += 20) d.drawCircle(cx, cy, r, ext_colour::Grid);
    d.drawFastHLine(cx - radius, cy, radius * 2, ext_colour::Grid);
    d.drawFastVLine(cx, cy - radius, radius * 2, ext_colour::Grid);
    const float radians = gSweepAngle * 0.0174532925f;
    const int sx = cx + static_cast<int>(std::cos(radians) * radius);
    const int sy = cy + static_cast<int>(std::sin(radians) * radius);
    d.drawLine(cx, cy, sx, sy, ext_colour::Dim);

    const int strength = signalPercent(target->rssi);
    const int blipRadius = std::max(8, radius - (strength * 68 / 100));
    const int bx = cx + static_cast<int>(std::cos(radians) * blipRadius);
    const int by = cy + static_cast<int>(std::sin(radians) * blipRadius);
    d.fillCircle(bx, by, 3, ext_colour::Peak);

    d.drawRect(195, 34, 117, 157, ext_colour::Mid);
    d.setTextColor(ext_colour::Peak, ext_colour::Off);
    d.setTextSize(1);
    d.setCursor(202, 43);
    d.print(target->kind == RadioKind::Wifi ? "WI-FI TARGET" : "BLE TARGET");
    d.setTextColor(ext_colour::Main, ext_colour::Off);
    d.setCursor(202, 61);
    char name[18] = {0};
    copyText(name, sizeof(name), target->name);
    d.print(name);
    d.setTextColor(ext_colour::Mid, ext_colour::Off);
    d.setCursor(202, 78);
    d.print(target->address);
    d.setTextColor(ext_colour::Peak, ext_colour::Off);
    d.setTextSize(3);
    d.setCursor(202, 101);
    d.printf("%d", target->rssi);
    d.setTextSize(1);
    d.setCursor(278, 116);
    d.print("dBm");
    drawSignalBar(d, 202, 137, 102, 10, target->rssi,
                  ext_colour::Peak, ext_colour::Grid);
    const float distance = estimateDistanceMetres(target->rssi);
    d.setTextColor(ext_colour::Main, ext_colour::Off);
    d.setCursor(202, 157);
    if (distance >= 0.0f && distance < 100.0f) d.printf("EST %.1f m", distance);
    else d.print("EST -- m");
    d.setTextColor(ext_colour::Dim, ext_colour::Off);
    d.setCursor(202, 175);
    d.print("PROXIMITY ONLY");

    d.setTextColor(ext_colour::Mid, ext_colour::Off);
    d.setCursor(18, 205);
    d.print("SWEEP IS VISUAL; SINGLE ANTENNA CANNOT MEASURE BEARING");
    d.drawFastHLine(1, 222, d.width() - 2, ext_colour::Mid);
    d.setTextColor(ext_colour::Main, ext_colour::Off);
    d.setCursor(8, 228);
    d.print(gStatus[0] ? gStatus : "ESC LIST   ;/. TARGET   L LOG   Q HOME");
}

void externalRenderer(LovyanGFX& d, void*) {
    if (gCurrentView == View::List) drawListExternal(d);
    else drawRadarExternal(d);
}

void renderNow() {
    drawInternal();
    if (gDisplay.isExternalReady()) gDisplay.renderExternal(externalRenderer);
    gRenderedRevision = gView.revision;
    gLastRenderMs = millis();
}

bool writeCsvLog() {
    if (!gStorage.isMounted() && !gStorage.begin()) return false;
    SpiBusLock lock(gStorage.spiMutex(), pdMS_TO_TICKS(1500));
    if (!lock.locked()) return false;
    File file = SD.open("/radar-log.csv", FILE_WRITE);
    if (!file) return false;
    file.println("radio,name,address,rssi,channel,security,last_seen_ms");
    for (size_t i = 0; i < gView.wifiCount; ++i) {
        const SignalTarget& t = gView.wifi[i];
        file.printf("wifi,\"%s\",%s,%d,%u,%s,%lu\n", t.name, t.address, t.rssi,
                    t.channel, t.secured ? "secured" : "open",
                    static_cast<unsigned long>(t.lastSeenMs));
    }
    for (size_t i = 0; i < gView.bleCount; ++i) {
        const SignalTarget& t = gView.ble[i];
        file.printf("ble,\"%s\",%s,%d,,, %lu\n", t.name, t.address, t.rssi,
                    static_cast<unsigned long>(t.lastSeenMs));
    }
    file.close();
    return true;
}

void moveSelection(int direction) {
    const size_t count = gSelectedKind == RadioKind::Wifi ? gView.wifiCount : gView.bleCount;
    if (count == 0) return;
    if (direction < 0) gSelection = gSelection == 0 ? count - 1 : gSelection - 1;
    else gSelection = (gSelection + 1) % count;
    gRenderedRevision = UINT32_MAX;
}

void handleInput(const KeyEvent& event) {
    if (event.action == KeyAction::Tab) {
        gDisplay.toggleActiveTarget();
        setStatus(gDisplay.activeTarget() == DisplayTarget::External
                      ? "KEYBOARD FOCUS: EXTERNAL" : "KEYBOARD FOCUS: INTERNAL", 1800);
        return;
    }
    if (event.action == KeyAction::Esc || event.action == KeyAction::Back) {
        if (gCurrentView == View::Target) {
            gCurrentView = View::List;
            gRenderedRevision = UINT32_MAX;
        } else {
            AppLifecycle::returnToLauncher();
        }
        return;
    }

    const bool moveLeft = event.action == KeyAction::Left ||
                          (event.action == KeyAction::Char && event.ch == ',');
    const bool moveRight = event.action == KeyAction::Right ||
                           (event.action == KeyAction::Char && event.ch == '/');
    const bool moveUp = event.action == KeyAction::Up ||
                        (event.action == KeyAction::Char && event.ch == ';');
    const bool moveDown = event.action == KeyAction::Down ||
                          (event.action == KeyAction::Char && event.ch == '.');
    if (moveLeft || moveRight) {
        gSelectedKind = gSelectedKind == RadioKind::Wifi ? RadioKind::Ble : RadioKind::Wifi;
        gSelection = 0;
        gRenderedRevision = UINT32_MAX;
    } else if (moveUp) {
        moveSelection(-1);
    } else if (moveDown) {
        moveSelection(1);
    } else if (event.action == KeyAction::Enter && selectedTarget()) {
        gCurrentView = View::Target;
        gRenderedRevision = UINT32_MAX;
    } else if (event.action == KeyAction::Char) {
        if (event.ch == 'q' || event.ch == 'Q') {
            AppLifecycle::returnToLauncher();
        } else if (event.ch == 'l' || event.ch == 'L') {
            setStatus(writeCsvLog() ? "LOGGED /radar-log.csv" : "LOG FAILED: CHECK SD");
        }
    }
}

}  // namespace

void setup() {
    AppLifecycle::commitCooperativeRecovery();
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    gKeyboard.begin();
    gConfig.loadFromNvs(gProfile);
    gProfile.external.enabled = true;
    gProfile.external.frequency = 40000000;
    gProfile.external.pins.dc = 6;
    gProfile.external.pins.rst = 3;
    gProfile.external.rotation = 1;
    gProfile.mode = DisplayMode::Independent;
    gProfile.internalRole = InternalDisplayRole::Status;
    gStorage.beginBus();
    gDisplay.begin(gProfile, gStorage.spiMutex());
    gStorage.begin();

    gSnapshotMutex = xSemaphoreCreateMutex();
    if (!gSnapshotMutex) {
        setStatus("SCAN ERROR: NO MUTEX", 60000);
        renderNow();
        return;
    }
    xTaskCreatePinnedToCore(scanWorker, "radar-scan", 12288, nullptr, 1, &gScanTask, 0);
    renderNow();
}

void loop() {
    M5Cardputer.update();
    KeyEvent event;
    if (gKeyboard.poll(event)) handleInput(event);

    refreshSnapshot();
    const uint32_t now = millis();
    if (gStatus[0] && static_cast<int32_t>(now - gStatusUntilMs) >= 0) {
        gStatus[0] = '\0';
        gRenderedRevision = UINT32_MAX;
    }
    if (gCurrentView == View::Target && now - gLastRenderMs >= kRenderIntervalMs) {
        gSweepAngle += 11.0f;
        if (gSweepAngle >= 360.0f) gSweepAngle -= 360.0f;
        renderNow();
    } else if (gRenderedRevision != gView.revision) {
        renderNow();
    }
    delay(12);
}
