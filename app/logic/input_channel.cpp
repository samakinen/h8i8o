// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file input_channel.cpp
 * @brief One input's function (docs/design/channel-functions.md §4)
 */

#include "logic/input_channel.hpp"

namespace h8i8o {

namespace {

constexpr uint8_t kDimStep = 1u;  // DPT 3.007 step code 1: the whole range, stopped on release

uint32_t dimming(bool up, uint8_t step) { return (up ? 0x8u : 0x0u) | step; }

}  // namespace

void InputChannel::configure(const InputConfig& config)
{
    // The functions that differ only in what ETS shows behave as their kind.
    InputConfig decoded = config;
    if (config.function == InputFunction::Percent) decoded.valueType = ValueType::Percent;
    decoded.function = behaviourOf(config.function);

    const bool functionChanged = decoded.function != _config.function;
    _config = decoded;
    if (_config.longMs == 0u) _config.longMs = 1u;
    if (_config.pulsesPerUnit == 0u) _config.pulsesPerUnit = 1u;
    if (_config.sendDelta == 0u) _config.sendDelta = 1u;
    if (_config.scene < 1u) _config.scene = 1u;
    if (_config.scene > 64u) _config.scene = 64u;
    if (!_config.lockEnabled) _locked = false;
    if (functionChanged) {
        // A press of another function must not finish as one of this one.
        _pressed = false;
        _long = false;
    }
    _sentUnits = units();
}

void InputChannel::writeLock(bool locked)
{
    if (!_config.lockEnabled) return;
    _locked = locked;
    if (locked) {
        // A press in progress is dropped: its release must not send.
        _pressed = false;
        _long = false;
    }
}

void InputChannel::writeCount(uint32_t units)
{
    _pulses = units * _config.pulsesPerUnit;
    _sentUnits = units;
}

void InputChannel::restorePulses(uint32_t pulses)
{
    _pulses = pulses;
    _sentUnits = units();
}

uint32_t InputChannel::units() const { return _pulses / _config.pulsesPerUnit; }

void InputChannel::apply(EdgeAction action, InObject object, bool& state, InputSink& sink)
{
    switch (action) {
        case EdgeAction::Nothing: return;
        case EdgeAction::On: state = true; break;
        case EdgeAction::Off: state = false; break;
        case EdgeAction::Toggle: state = !state; break;
    }
    send(object, state ? 1u : 0u, sink);
}

uint8_t InputChannel::encodedValue() const
{
    const uint8_t value = _config.value;
    switch (_config.valueType) {
        case ValueType::Value: return value;
        case ValueType::Percent: {
            const uint32_t percent = value > 100u ? 100u : value;
            return static_cast<uint8_t>((percent * 255u + 50u) / 100u);
        }
    }
    return value;
}

void InputChannel::edge(bool active, uint32_t now, InputSink& sink)
{
    if (_config.function == InputFunction::Counter) {
        // A counter measures: it counts while locked, it only does not send.
        if (active) count(sink);
        return;
    }
    if (_locked || _config.function == InputFunction::Disabled) return;
    if (_config.function == InputFunction::Switch) {
        apply(active ? _config.onClose : _config.onOpen, InObject::A, _a, sink);
        return;
    }
    if (_config.function == InputFunction::Value) {
        if (active) send(InObject::D, encodedValue(), sink);
        return;
    }
    if (active) {
        press(now, sink);
    } else {
        release(sink);
    }
}

void InputChannel::press(uint32_t now, InputSink& /*sink*/)
{
    _pressed = true;
    _long = false;
    _pressedAt = now;
}

void InputChannel::tick(uint32_t now, InputSink& sink)
{
    if (!_pressed || _long || now - _pressedAt < _config.longMs) return;
    _long = true;
    longPress(sink);
}

void InputChannel::longPress(InputSink& sink)
{
    switch (_config.function) {
        case InputFunction::ShortLong:
            apply(_config.longAction, InObject::B, _b, sink);
            break;
        case InputFunction::Dimming:
            // One button: up from off, otherwise the other way from last time.
            if (_config.dimDirection == Direction::OneButton) {
                _dimDown = _a && !_dimDown;
            } else {
                _dimDown = _config.dimDirection == Direction::Down;
            }
            send(InObject::C, dimming(!_dimDown, kDimStep), sink);
            break;
        case InputFunction::Shutter:
            if (_config.shutterDirection == Direction::OneButton) {
                _shutterUp = !_shutterUp;
            } else {
                _shutterUp = _config.shutterDirection == Direction::Up;
            }
            send(InObject::A, _shutterUp ? 0u : 1u, sink);
            break;
        case InputFunction::Scene:
            send(InObject::D, 0x80u | static_cast<uint8_t>(_config.scene - 1u), sink);
            break;
        default:
            break;
    }
}

void InputChannel::release(InputSink& sink)
{
    if (!_pressed) return;
    _pressed = false;
    if (_long) {
        _long = false;
        if (_config.function == InputFunction::Dimming) send(InObject::C, dimming(!_dimDown, 0u), sink);
        return;
    }
    switch (_config.function) {
        case InputFunction::ShortLong:
            apply(_config.shortAction, InObject::A, _a, sink);
            break;
        case InputFunction::Dimming: {
            const EdgeAction action = _config.dimDirection == Direction::Up     ? EdgeAction::On
                                      : _config.dimDirection == Direction::Down ? EdgeAction::Off
                                                                                : EdgeAction::Toggle;
            apply(action, InObject::A, _a, sink);
            break;
        }
        case InputFunction::Shutter: {
            // Stop or step in the direction of the last move; two buttons in their own.
            bool down = !_shutterUp;
            if (_config.shutterDirection != Direction::OneButton) {
                down = _config.shutterDirection == Direction::Down;
            }
            send(InObject::B, down ? 1u : 0u, sink);
            break;
        }
        case InputFunction::Scene:
            send(InObject::D, static_cast<uint8_t>(_config.scene - 1u), sink);
            break;
        default:
            break;
    }
}

void InputChannel::count(InputSink& sink)
{
    ++_pulses;
    const uint32_t now = units();
    if (_locked || now - _sentUnits < _config.sendDelta) return;
    _sentUnits = now;
    send(InObject::E, now, sink);
}

void InputChannel::sendAfterStart(bool active, InputSink& sink)
{
    if (!_config.sendAfterStart || _locked) return;
    if (_config.function == InputFunction::Switch) {
        const EdgeAction action = active ? _config.onClose : _config.onOpen;
        if (action == EdgeAction::On || action == EdgeAction::Off) apply(action, InObject::A, _a, sink);
    } else if (_config.function == InputFunction::Counter) {
        _sentUnits = units();
        send(InObject::E, _sentUnits, sink);
    }
}

}  // namespace h8i8o
