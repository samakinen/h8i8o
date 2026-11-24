// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file output_channel.hpp
 * @brief One output's function: what it should be, given what it was told
 *
 * Two layers (docs/design/channel-functions.md §5):
 *
 * - the function's own state, which commands, scenes and control values
 *   drive: a switch with delays, a staircase timer, a pulse, a valve's duty
 *   cycle. It keeps running underneath a lock or forced operation;
 * - the overrides on top, highest first: forced operation, then the lock.
 *
 * on() is the result, as the switch object means it (the operating mode is
 * the glue's, at the pin). takeStatus() says when the status object is due:
 * after every command, as before, and whenever on() changes by itself.
 *
 * No KNstaX, no hardware: tested on the host (test/).
 */

#pragma once

#include "logic/functions.hpp"

#include <cstdint>

namespace h8i8o {

inline constexpr unsigned kScenesPerOutput = 4;

/// An output's parameters, decoded from the ETS image by the glue. Everything
/// defaults to zero, so the channels need no initial image in flash; the glue
/// sets every field, and configure() takes 0 for the times that cannot be.
struct OutputConfig {
    OutputFunction function{};
    uint16_t onDelayS{};         ///< Switch
    uint16_t offDelayS{};        ///< Switch
    uint16_t staircaseS{};       ///< Staircase (0 counts as 1)
    bool retrigger{};            ///< Staircase
    bool offWithZero{};          ///< Staircase
    uint8_t warningS{};          ///< Staircase: 0 = no warning
    uint8_t pulse100ms{};        ///< Pulse (0 counts as 1)
    bool lockEnabled{};
    LockAction lockStart{};
    LockAction lockEnd{};
    bool central{};              ///< the glue routes the central object here when set
    uint8_t sceneNumber[kScenesPerOutput]{};  ///< 0 = unused, 1-64
    bool sceneOn[kScenesPerOutput]{};
    uint8_t cycleMin{};          ///< Valve (0 counts as 1)
    uint8_t monitorMin{};        ///< Valve: 0 = no monitoring
    uint8_t safetyPercent{};     ///< Valve
};

class OutputChannel {
public:
    /// New parameters; the run-time state is kept (see InputChannel::configure).
    void configure(const OutputConfig& config, uint32_t now);
    const OutputConfig& config() const { return _config; }

    /// The switch object, the central object, or the stored value at start.
    void command(bool on, uint32_t now);
    /// A scene telegram (DPT 18.001, raw). Returns the scene slots a store
    /// asks to learn (bit i: slot i), which the glue writes as parameters
    /// with on() as their action.
    uint8_t scene(uint8_t raw, uint32_t now);
    /// The valve's control value (DPT 5.001, raw 0-255).
    void controlValue(uint8_t value, uint32_t now);
    void lock(bool locked, uint32_t now);
    /// DPT 2.001: control 1 forces the value, control 0 ends forcing.
    void forced(bool control, bool value, uint32_t now);

    /// Every millisecond: timers, the valve's cycle.
    void tick(uint32_t now);

    bool on() const { return _out; }
    /// The value the switch object stands at (what "as before" restores).
    bool switchValue() const { return _command; }
    bool locked() const { return _locked; }
    bool forcedActive() const { return _forced; }
    /// True once after a command or a change of on().
    bool takeStatus()
    {
        const bool due = _statusDue;
        _statusDue = false;
        return due;
    }

private:
    bool base(uint32_t now) const;
    void evaluate(uint32_t now);
    void setBase(bool on, uint32_t now);
    uint8_t valveDuty(uint32_t now) const;

    OutputConfig _config{};
    bool _out{false};
    bool _statusDue{false};
    bool _command{false};       ///< last command: the switch value

    // Switch
    bool _switchState{false};
    bool _pending{false};
    bool _pendingValue{false};
    uint32_t _pendingAt{0};

    // Staircase and pulse
    bool _timerOn{false};
    uint32_t _timerEnd{0};

    // Valve
    uint8_t _duty{0};
    bool _haveValue{false};
    uint32_t _valueAt{0};
    uint32_t _cycleStart{0};

    // Overrides
    bool _locked{false};
    bool _lockValue{false};
    bool _forced{false};
    bool _forcedValue{false};
};

}  // namespace h8i8o
