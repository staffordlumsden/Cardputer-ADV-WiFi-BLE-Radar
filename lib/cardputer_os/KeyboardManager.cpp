#include "KeyboardManager.h"

#include <M5Cardputer.h>

namespace cardputer_os {

void KeyboardManager::begin() {
    lastKeyTimeMs_ = 0;
    lastRepeatTimeMs_ = 0;
    hadKey_ = false;
}

void KeyboardManager::setRepeatTimings(uint32_t delayMs, uint32_t rateMs) {
    repeatDelayMs_ = delayMs;
    repeatRateMs_ = rateMs;
}

KeyEvent KeyboardManager::translateCurrentState() const {
    KeyEvent ev;
    const auto& keys = M5Cardputer.Keyboard.keysState();
    ev.fn = keys.fn;
    ev.ctrl = keys.ctrl;
    ev.shift = keys.shift;

    if (keys.enter) {
        ev.action = KeyAction::Enter;
        return ev;
    }
    if (keys.del) {
        ev.action = KeyAction::Back;
        return ev;
    }
    if (keys.tab) {
        ev.action = KeyAction::Tab;
        return ev;
    }

    if (!keys.word.empty()) {
        const char c = keys.word[0];
        if (keys.fn) {
            // M5Cardputer pushes value_second (:, >, <, ?, ~) when Ctrl/Shift/CapsLock
            // is held alongside Fn, so match both layers for reliable chords.
            if (c == ';' || c == ':') { ev.action = KeyAction::Up; return ev; }
            if (c == '.' || c == '>') { ev.action = KeyAction::Down; return ev; }
            if (c == ',' || c == '<') { ev.action = KeyAction::Left; return ev; }
            if (c == '/' || c == '?') { ev.action = KeyAction::Right; return ev; }
            if (c == '`' || c == '~') { ev.action = KeyAction::Esc; return ev; }
        }
        ev.action = KeyAction::Char;
        ev.ch = c;
    }
    return ev;
}

bool KeyboardManager::poll(KeyEvent& outEvent) {
    const uint32_t now = millis();
    // M5Cardputer's isChange() tracks the number of pressed keys. It must also
    // observe the transition back to zero; otherwise consecutive bare keys all
    // look like the same one-key state and adding Fn is the only visible change.
    const bool changed = M5Cardputer.Keyboard.isChange();
    const bool pressed = M5Cardputer.Keyboard.isPressed();

    if (!pressed) {
        hadKey_ = false;
        return false;
    }

    if (changed) {
        KeyEvent ev = translateCurrentState();
        if (ev.action != KeyAction::None) {
            lastEvent_ = ev;
            lastKeyTimeMs_ = now;
            lastRepeatTimeMs_ = now;
            hadKey_ = true;
            outEvent = ev;
            return true;
        }
        return false;
    }

    if (hadKey_ && lastEvent_.action != KeyAction::None) {
        const uint32_t heldMs = now - lastKeyTimeMs_;
        if (heldMs >= repeatDelayMs_ && (now - lastRepeatTimeMs_) >= repeatRateMs_) {
            lastRepeatTimeMs_ = now;
            outEvent = lastEvent_;
            outEvent.isRepeat = true;
            return true;
        }
    }

    return false;
}

}  // namespace cardputer_os
