#pragma once

#include <Arduino.h>
#include <cstdint>

namespace cardputer_os {

enum class KeyAction : uint8_t {
    None = 0,
    Char,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Back,
    Esc,
    Tab,
};

struct KeyEvent {
    KeyAction action = KeyAction::None;
    char ch = '\0';
    bool fn = false;
    bool ctrl = false;
    bool shift = false;
    bool isRepeat = false;
};

class KeyboardManager {
public:
    void begin();
    bool poll(KeyEvent& outEvent);

    void setRepeatTimings(uint32_t delayMs, uint32_t rateMs);

private:
    KeyEvent translateCurrentState() const;

    uint32_t repeatDelayMs_ = 380;
    uint32_t repeatRateMs_ = 85;
    uint32_t lastKeyTimeMs_ = 0;
    uint32_t lastRepeatTimeMs_ = 0;
    bool hadKey_ = false;
    KeyEvent lastEvent_{};
};

}  // namespace cardputer_os
