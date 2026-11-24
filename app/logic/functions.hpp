// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file functions.hpp
 * @brief The channel functions and their choices, as ETS stores them
 *
 * Shared by the logic (app/logic) and the parameter declarations
 * (app/product.hpp). No KNstaX or hardware dependency: the logic is built and
 * tested on the host too.
 *
 * The values are what ETS writes into the parameter image. Never renumber one
 * without a new application version (docs/design/channel-functions.md).
 */

#pragma once

#include <cstdint>

namespace h8i8o {

inline constexpr unsigned kChannels = 8;

/// What an input does. Switch keeps the value 1 it had as the only function.
///
/// Several functions behave alike and differ only in the datapoint type ETS
/// shows for their object (an ETS condition tests one parameter, so the type
/// is part of the function): the contacts are Switch, EnergyCounter is
/// Counter, Percent is Value. behaviourOf() maps them.
enum class InputFunction : uint8_t {
    Disabled = 0,
    Switch = 1,         ///< an action per edge on object A (1.001)
    ShortLong = 2,      ///< short press on A, long press on B
    Dimming = 3,        ///< short press switches (A), long press dims (C)
    Shutter = 4,        ///< long press moves (A), short press stops or steps (B)
    Value = 5,          ///< a value on D when the contact closes (5.010)
    Scene = 6,          ///< recall (short) or store (long) a scene on D (18.001)
    Counter = 7,        ///< counts closings on E (12.001)
    Alarm = 8,          ///< Switch, A as 1.005
    WindowDoor = 9,     ///< Switch, A as 1.019
    Occupancy = 10,     ///< Switch, A as 1.018
    OpenClose = 11,     ///< Switch, A as 1.009
    EnergyCounter = 12, ///< Counter, E as 13.010 (Wh)
    Percent = 13,       ///< Value, D as 5.001 (the value is 0-100 %)
};

/// The function an input behaves as (see InputFunction).
constexpr InputFunction behaviourOf(InputFunction function)
{
    switch (function) {
        case InputFunction::Alarm:
        case InputFunction::WindowDoor:
        case InputFunction::Occupancy:
        case InputFunction::OpenClose:
            return InputFunction::Switch;
        case InputFunction::EnergyCounter:
            return InputFunction::Counter;
        case InputFunction::Percent:
            return InputFunction::Value;
        default:
            return function;
    }
}

/// What an edge or a press sends on a 1-bit object.
enum class EdgeAction : uint8_t { Nothing = 0, On = 1, Off = 2, Toggle = 3 };

/// Dimming and shutter push buttons: one button alternates, two buttons each
/// have their direction (Up = brighter or up, Down = darker or down).
enum class Direction : uint8_t { OneButton = 0, Up = 1, Down = 2 };

/// How the value of a value transmitter is sent on D.
enum class ValueType : uint8_t {
    Value = 0,        ///< 0-255 as it is (DPT 5.010)
    Percent = 1,      ///< 0-100 %, scaled to 0-255 (DPT 5.001)
};

/// What an output does. Switch keeps the value 1 that "Enabled" had.
enum class OutputFunction : uint8_t {
    Disabled = 0,
    Switch = 1,     ///< with on and off delays
    Staircase = 2,  ///< on for a time, with an optional warning
    Pulse = 3,      ///< on for a short time per on telegram
    Valve = 4,      ///< pulse width from a control value (DPT 5.001)
};

/// What a lock does to an output when it starts and when it ends.
enum class LockAction : uint8_t {
    Unchanged = 0,  ///< the output stays as it is
    On = 1,
    Off = 2,
    Last = 3,       ///< at the end only: what was commanded meanwhile
};

}  // namespace h8i8o
