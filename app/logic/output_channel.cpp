// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file output_channel.cpp
 * @brief One output's function (docs/design/channel-functions.md §5)
 */

#include "logic/output_channel.hpp"

namespace h8i8o {

namespace {

/// A deadline has passed. Deadlines lie less than 2^31 ms ahead (the longest
/// is 65,535 s), so the difference tells it across the counter's wrap.
bool due(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

constexpr uint32_t kWarningBlinkMs = 1000u;
constexpr uint8_t kValveShutBelow = 5u;    // about 2 %: no pulse shorter than that
constexpr uint8_t kValveOpenAbove = 250u;  // about 98 %

uint8_t percentToRaw(uint8_t percent)
{
    const uint32_t p = percent > 100u ? 100u : percent;
    return static_cast<uint8_t>((p * 255u + 50u) / 100u);
}

}  // namespace

void OutputChannel::configure(const OutputConfig& config, uint32_t now)
{
    const bool functionChanged = config.function != _config.function;
    _config = config;
    if (_config.staircaseS == 0u) _config.staircaseS = 1u;
    if (_config.pulse100ms == 0u) _config.pulse100ms = 1u;
    if (_config.cycleMin == 0u) _config.cycleMin = 1u;
    if (_config.lockStart == LockAction::Last) _config.lockStart = LockAction::Unchanged;
    if (functionChanged) {
        _switchState = false;
        _pending = false;
        _timerOn = false;
        _haveValue = false;
        _valueAt = now;
        _cycleStart = now;
    }
    if (!_config.lockEnabled) _locked = false;
    evaluate(now);
}

uint8_t OutputChannel::valveDuty(uint32_t now) const
{
    const uint32_t monitorMs = static_cast<uint32_t>(_config.monitorMin) * 60000u;
    if (monitorMs != 0u && now - _valueAt >= monitorMs) return percentToRaw(_config.safetyPercent);
    return _haveValue ? _duty : 0u;
}

bool OutputChannel::base(uint32_t now) const
{
    switch (_config.function) {
        case OutputFunction::Disabled:
            return false;
        case OutputFunction::Switch:
            return _switchState;
        case OutputFunction::Staircase: {
            if (!_timerOn) return false;
            const uint32_t warningMs = static_cast<uint32_t>(_config.warningS) * 1000u;
            const uint32_t timeMs = static_cast<uint32_t>(_config.staircaseS) * 1000u;
            if (warningMs == 0u || warningMs >= timeMs) return true;
            // Off for a second, the warning time before the end.
            const uint32_t remaining = _timerEnd - now;
            return !(remaining <= warningMs && remaining > warningMs - kWarningBlinkMs);
        }
        case OutputFunction::Pulse:
            return _timerOn;
        case OutputFunction::Valve: {
            const uint8_t duty = valveDuty(now);
            if (duty < kValveShutBelow) return false;
            if (duty > kValveOpenAbove) return true;
            const uint32_t cycleMs = static_cast<uint32_t>(_config.cycleMin) * 60000u;
            const uint32_t onMs = cycleMs / 255u * duty;
            return now - _cycleStart < onMs;
        }
    }
    return false;
}

void OutputChannel::evaluate(uint32_t now)
{
    bool value = base(now);
    if (_locked) value = _lockValue;
    if (_forced) value = _forcedValue;
    if (_config.function == OutputFunction::Disabled) value = false;  // whatever the objects say
    if (value != _out) {
        _out = value;
        _statusDue = true;
    }
}

void OutputChannel::setBase(bool on, uint32_t now)
{
    switch (_config.function) {
        case OutputFunction::Switch:
            _switchState = on;
            _pending = false;
            break;
        case OutputFunction::Staircase:
            _timerOn = on;
            _timerEnd = now + static_cast<uint32_t>(_config.staircaseS) * 1000u;
            break;
        case OutputFunction::Pulse:
            _timerOn = on;
            _timerEnd = now + static_cast<uint32_t>(_config.pulse100ms) * 100u;
            break;
        default:
            return;
    }
    _command = on;
}

void OutputChannel::command(bool on, uint32_t now)
{
    switch (_config.function) {
        case OutputFunction::Disabled:
        case OutputFunction::Valve:
            return;
        case OutputFunction::Switch: {
            const uint32_t delayS = on ? _config.onDelayS : _config.offDelayS;
            if (on == _switchState) {
                _pending = false;  // cancels a pending change the other way
            } else if (delayS == 0u) {
                _switchState = on;
                _pending = false;
            } else if (!_pending || _pendingValue != on) {
                // A repeat of the pending command keeps its deadline.
                _pending = true;
                _pendingValue = on;
                _pendingAt = now + delayS * 1000u;
            }
            break;
        }
        case OutputFunction::Staircase:
            if (on) {
                if (!_timerOn || _config.retrigger) setBase(true, now);
            } else if (_config.offWithZero) {
                _timerOn = false;
            }
            break;
        case OutputFunction::Pulse:
            if (!on) return;  // an off telegram is not a command for a pulse
            setBase(true, now);
            break;
    }
    _command = on;
    _statusDue = true;
    evaluate(now);
}

uint8_t OutputChannel::scene(uint8_t raw, uint32_t now)
{
    if (_config.function == OutputFunction::Disabled || _config.function == OutputFunction::Valve) return 0u;
    const uint8_t number = static_cast<uint8_t>((raw & 0x3Fu) + 1u);
    const bool learn = (raw & 0x80u) != 0u;
    uint8_t slots = 0;
    for (unsigned i = 0; i < kScenesPerOutput; ++i) {
        if (_config.sceneNumber[i] != number) continue;
        if (!learn) {
            command(_config.sceneOn[i], now);
            return 0u;
        }
        slots = static_cast<uint8_t>(slots | (1u << i));
    }
    return slots;
}

void OutputChannel::controlValue(uint8_t value, uint32_t now)
{
    if (_config.function != OutputFunction::Valve) return;
    _duty = value;
    _haveValue = true;
    _valueAt = now;
    evaluate(now);
}

void OutputChannel::lock(bool locked, uint32_t now)
{
    if (!_config.lockEnabled || locked == _locked) return;
    if (locked) {
        _lockValue = _config.lockStart == LockAction::On    ? true
                     : _config.lockStart == LockAction::Off ? false
                                                            : _out;
        _locked = true;
    } else {
        _locked = false;
        switch (_config.lockEnd) {
            case LockAction::Unchanged: setBase(_lockValue, now); break;
            case LockAction::On: setBase(true, now); break;
            case LockAction::Off: setBase(false, now); break;
            case LockAction::Last: break;  // what was commanded meanwhile takes over
        }
    }
    evaluate(now);
}

void OutputChannel::forced(bool control, bool value, uint32_t now)
{
    _forced = control;
    _forcedValue = value;
    evaluate(now);
}

void OutputChannel::tick(uint32_t now)
{
    switch (_config.function) {
        case OutputFunction::Switch:
            if (_pending && due(now, _pendingAt)) {
                _switchState = _pendingValue;
                _pending = false;
            }
            break;
        case OutputFunction::Staircase:
        case OutputFunction::Pulse:
            if (_timerOn && due(now, _timerEnd)) _timerOn = false;
            break;
        case OutputFunction::Valve: {
            const uint32_t cycleMs = static_cast<uint32_t>(_config.cycleMin) * 60000u;
            while (now - _cycleStart >= cycleMs) _cycleStart += cycleMs;
            break;
        }
        default:
            break;
    }
    evaluate(now);
}

}  // namespace h8i8o
