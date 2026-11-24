// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file product.hpp
 * @brief H8i8o product: 8 inputs and 8 outputs, each with a function
 *
 * The single source of truth for the firmware and the ETS product file
 * (tools/knxprod). No hardware or OS dependencies here.
 *
 * docs/design/channel-functions.md is the design; docs/application.md
 * describes the result for ETS users. In short:
 *
 * - Objects. Each channel has a fixed set of object slots, one per size class
 *   its functions need (§3.2): an input A, B (1 bit), C (4 bit), D (1 octet),
 *   E (4 octets) and L (lock); an output S (switch), T (status), K (lock),
 *   F (2-bit forced operation) and V (1 octet: scene or control value). Then
 *   the device's central switch and "in operation". A slot whose functions a
 *   build variant leaves out is not declared.
 * - Object views (kObjectViews) tell ETS which slots a channel's function uses
 *   and with which datapoint type. They are export data: the firmware never
 *   reads them.
 * - Parameters: a device block, an input block ×8, an output block ×8.
 *
 * The build variant (a file under variants/, cmake/h8i8o_variant.cmake) decides
 * the functions (CONFIG_H8I8O_*), Data Secure (KNX_SECURE_ENABLED) and the
 * product identity, so the firmware and the product file of a variant agree.
 *
 * Bump PersistencePolicy::schemaVersion whenever a port is added, removed or
 * reordered: the stored layout is positional. Raise applicationVersion when
 * objects or parameters change.
 */

#pragma once

#include "h8i8o_config.h"
#include "logic/functions.hpp"
#include "logic/output_channel.hpp"

#include "knx/product/commissioned_product.hpp"
#include "knx/product/object_views.hpp"

#include <array>
#include <cstddef>

namespace h8i8o {

using namespace knx;
using namespace knx::product;

// ── What this variant has ────────────────────────────────────────────────────

#ifdef CONFIG_H8I8O_INPUT_DIMMING
#define H8I8O_INPUT_DIMMING 1
#else
#define H8I8O_INPUT_DIMMING 0
#endif
#ifdef CONFIG_H8I8O_INPUT_SHUTTER
#define H8I8O_INPUT_SHUTTER 1
#else
#define H8I8O_INPUT_SHUTTER 0
#endif
#ifdef CONFIG_H8I8O_INPUT_VALUE
#define H8I8O_INPUT_VALUE 1
#else
#define H8I8O_INPUT_VALUE 0
#endif
#ifdef CONFIG_H8I8O_INPUT_COUNTER
#define H8I8O_INPUT_COUNTER 1
#else
#define H8I8O_INPUT_COUNTER 0
#endif
#ifdef CONFIG_H8I8O_OUTPUT_FORCED
#define H8I8O_OUTPUT_FORCED 1
#else
#define H8I8O_OUTPUT_FORCED 0
#endif
#ifdef CONFIG_H8I8O_OUTPUT_SCENES
#define H8I8O_OUTPUT_SCENES 1
#else
#define H8I8O_OUTPUT_SCENES 0
#endif
#ifdef CONFIG_H8I8O_OUTPUT_VALVE
#define H8I8O_OUTPUT_VALVE 1
#else
#define H8I8O_OUTPUT_VALVE 0
#endif
#define H8I8O_OUTPUT_V (H8I8O_OUTPUT_SCENES || H8I8O_OUTPUT_VALVE)

inline constexpr bool kInputDimming = H8I8O_INPUT_DIMMING;
inline constexpr bool kInputShutter = H8I8O_INPUT_SHUTTER;
inline constexpr bool kInputValue = H8I8O_INPUT_VALUE;
inline constexpr bool kInputCounter = H8I8O_INPUT_COUNTER;
inline constexpr bool kOutputForced = H8I8O_OUTPUT_FORCED;
inline constexpr bool kOutputScenes = H8I8O_OUTPUT_SCENES;
inline constexpr bool kOutputValve = H8I8O_OUTPUT_VALVE;

// ── Object slots and numbers ─────────────────────────────────────────────────

enum class InSlot : uint8_t { A, B, C, D, E, L };
enum class OutSlot : uint8_t { S, T, K, F, V };

constexpr bool present(InSlot slot)
{
    switch (slot) {
        case InSlot::C: return kInputDimming;
        case InSlot::D: return kInputValue;
        case InSlot::E: return kInputCounter;
        default: return true;
    }
}
constexpr bool present(OutSlot slot)
{
    switch (slot) {
        case OutSlot::F: return kOutputForced;
        case OutSlot::V: return kOutputScenes || kOutputValve;
        default: return true;
    }
}

template <typename Slot>
constexpr unsigned slotOffset(Slot slot)
{
    unsigned offset = 0;
    for (unsigned s = 0; s < static_cast<unsigned>(slot); ++s) offset += present(static_cast<Slot>(s)) ? 1u : 0u;
    return offset;
}

inline constexpr unsigned kInSlots = slotOffset(InSlot::L) + 1u;
inline constexpr unsigned kOutSlots = slotOffset(OutSlot::V) + (present(OutSlot::V) ? 1u : 0u);
inline constexpr unsigned kOutputBase = kChannels * kInSlots;
inline constexpr unsigned kDeviceBase = kOutputBase + kChannels * kOutSlots;

/// A port's logical id is its object number.
enum class Port : uint16_t {};

constexpr Port inPort(unsigned channel, InSlot slot)
{
    return static_cast<Port>(channel * kInSlots + slotOffset(slot));
}
constexpr Port outPort(unsigned channel, OutSlot slot)
{
    return static_cast<Port>(kOutputBase + channel * kOutSlots + slotOffset(slot));
}
inline constexpr Port kCentralPort = static_cast<Port>(kDeviceBase);
inline constexpr Port kInOperationPort = static_cast<Port>(kDeviceBase + 1u);

// The port kinds. Nothing is persisted by the stack: inputs mirror hardware,
// and the outputs keep their own record (app/main.cpp).
template <Port P, FixedString Key, FixedString Name>
using InA = StateInOutPort<P, bool, Key, Name, application::dptids::Switch, false>;
template <Port P, FixedString Key, FixedString Name>
using InB = StatePort<P, bool, Key, Name, application::dptids::Switch, false>;
template <Port P, FixedString Key, FixedString Name>
using InC = StatePort<P, application::Dpt3Value, Key, Name, application::dptids::Dimming, false>;
template <Port P, FixedString Key, FixedString Name>
using InD = StatePort<P, uint8_t, Key, Name, application::dptids::Level, false>;
template <Port P, FixedString Key, FixedString Name>
using InE = StateInOutPort<P, uint32_t, Key, Name, application::dptids::Unsigned32, false>;
template <Port P, FixedString Key, FixedString Name>
using LockPort = CommandPort<P, bool, Key, Name, application::dptids::Switch>;
template <Port P, FixedString Key, FixedString Name>
using OutS = CommandPort<P, bool, Key, Name, application::dptids::Switch>;
template <Port P, FixedString Key, FixedString Name>
using OutT = StatePort<P, bool, Key, Name, application::dptids::Switch, false>;
template <Port P, FixedString Key, FixedString Name>
using OutF = CommandPort<P, application::Dpt2Value, Key, Name, application::dptids::Controlled1Bit>;
template <Port P, FixedString Key, FixedString Name>
using OutV = CommandPort<P, uint8_t, Key, Name, application::dptids::Level>;

// One channel's ports, in object number order. n is the 1-based channel.
#if H8I8O_INPUT_DIMMING
#define H8I8O_IN_C(n) InC<inPort(n - 1, InSlot::C), "in" #n "_c", "Input " #n>,
#else
#define H8I8O_IN_C(n)
#endif
#if H8I8O_INPUT_VALUE
#define H8I8O_IN_D(n) InD<inPort(n - 1, InSlot::D), "in" #n "_d", "Input " #n>,
#else
#define H8I8O_IN_D(n)
#endif
#if H8I8O_INPUT_COUNTER
#define H8I8O_IN_E(n) InE<inPort(n - 1, InSlot::E), "in" #n "_e", "Input " #n>,
#else
#define H8I8O_IN_E(n)
#endif
#if H8I8O_OUTPUT_FORCED
#define H8I8O_OUT_F(n) OutF<outPort(n - 1, OutSlot::F), "out" #n "_forced", "Output " #n>,
#else
#define H8I8O_OUT_F(n)
#endif
#if H8I8O_OUTPUT_V
#define H8I8O_OUT_V(n) OutV<outPort(n - 1, OutSlot::V), "out" #n "_v", "Output " #n>,
#else
#define H8I8O_OUT_V(n)
#endif

#define H8I8O_INPUT_PORTS(n)                                   \
    InA<inPort(n - 1, InSlot::A), "in" #n "_a", "Input " #n>,  \
    InB<inPort(n - 1, InSlot::B), "in" #n "_b", "Input " #n>,  \
    H8I8O_IN_C(n) H8I8O_IN_D(n) H8I8O_IN_E(n)                  \
    LockPort<inPort(n - 1, InSlot::L), "in" #n "_lock", "Input " #n>,

#define H8I8O_OUTPUT_PORTS(n)                                      \
    OutS<outPort(n - 1, OutSlot::S), "out" #n "_s", "Output " #n>,   \
    OutT<outPort(n - 1, OutSlot::T), "out" #n "_status", "Output " #n>, \
    LockPort<outPort(n - 1, OutSlot::K), "out" #n "_lock", "Output " #n>, \
    H8I8O_OUT_F(n) H8I8O_OUT_V(n)

// ── Parameters ──────────────────────────────────────────────────────────────
// Enum values are what ETS stores; do not renumber them without a new
// application version. Conditions are for ETS only: the firmware reads the
// function, never whether a parameter is shown.

/// The value is the time in milliseconds.
enum class DebounceTime : uint8_t { Ms10 = 10, Ms20 = 20, Ms30 = 30, Ms50 = 50, Ms100 = 100, Ms200 = 200 };
enum class ContactType : uint8_t { NormallyOpen = 0, NormallyClosed = 1 };
enum class OperatingMode : uint8_t { NormallyOpen = 0, NormallyClosed = 1 };
enum class StatusObject : uint8_t { Disabled = 0, OnChange = 1 };
enum class StartState : uint8_t { Released = 0, Restore = 1 };
enum class Use : uint8_t { Disabled = 0, Enabled = 1 };

// The function lists offer what this variant has, in the order ETS shows.
inline constexpr auto kInputFunctionOptions = params::options<InputFunction>({
    {InputFunction::Disabled, "Disabled"},
    {InputFunction::Switch, "Switch"},
    {InputFunction::Alarm, "Alarm contact (1.005)"},
    {InputFunction::WindowDoor, "Window or door contact (1.019)"},
    {InputFunction::Occupancy, "Presence contact (1.018)"},
    {InputFunction::OpenClose, "Open/close contact (1.009)"},
    {InputFunction::ShortLong, "Short and long press"},
#if H8I8O_INPUT_DIMMING
    {InputFunction::Dimming, "Dimming"},
#endif
#if H8I8O_INPUT_SHUTTER
    {InputFunction::Shutter, "Shutter"},
#endif
#if H8I8O_INPUT_VALUE
    {InputFunction::Value, "Value (5.010)"},
    {InputFunction::Percent, "Percentage (5.001)"},
    {InputFunction::Scene, "Scene"},
#endif
#if H8I8O_INPUT_COUNTER
    {InputFunction::Counter, "Pulse counter (12.001)"},
    {InputFunction::EnergyCounter, "Energy meter, Wh (13.010)"},
#endif
});
inline constexpr auto kContactTypeOptions = params::options<ContactType>(
    {{ContactType::NormallyOpen, "Normally open"}, {ContactType::NormallyClosed, "Normally closed"}});
inline constexpr auto kDebounceTimeOptions = params::options<DebounceTime>(
    {{DebounceTime::Ms10, "10 ms"}, {DebounceTime::Ms20, "20 ms"}, {DebounceTime::Ms30, "30 ms"},
     {DebounceTime::Ms50, "50 ms"}, {DebounceTime::Ms100, "100 ms"}, {DebounceTime::Ms200, "200 ms"}});
inline constexpr auto kEdgeActionOptions = params::options<EdgeAction>(
    {{EdgeAction::Nothing, "Nothing"}, {EdgeAction::On, "On"}, {EdgeAction::Off, "Off"},
     {EdgeAction::Toggle, "Toggle"}});
inline constexpr auto kDimDirectionOptions = params::options<Direction>(
    {{Direction::OneButton, "One button (alternating)"}, {Direction::Up, "Brighter (short press: on)"},
     {Direction::Down, "Darker (short press: off)"}});
inline constexpr auto kShutterDirectionOptions = params::options<Direction>(
    {{Direction::OneButton, "One button (alternating)"}, {Direction::Up, "Up"}, {Direction::Down, "Down"}});
inline constexpr auto kUseOptions = params::options<Use>({{Use::Disabled, "Disabled"}, {Use::Enabled, "Enabled"}});
inline constexpr auto kNoYesOptions = params::options<bool>({{false, "No"}, {true, "Yes"}});

inline constexpr auto kOutputFunctionOptions = params::options<OutputFunction>({
    {OutputFunction::Disabled, "Disabled"},
    {OutputFunction::Switch, "Switch"},
    {OutputFunction::Staircase, "Staircase light"},
    {OutputFunction::Pulse, "Pulse"},
#if H8I8O_OUTPUT_VALVE
    {OutputFunction::Valve, "Heating valve (PWM)"},
#endif
});
inline constexpr auto kOperatingModeOptions = params::options<OperatingMode>(
    {{OperatingMode::NormallyOpen, "Normally open (1 = energise)"},
     {OperatingMode::NormallyClosed, "Normally closed (1 = de-energise)"}});
inline constexpr auto kStatusObjectOptions = params::options<StatusObject>(
    {{StatusObject::Disabled, "Disabled"}, {StatusObject::OnChange, "Send on change"}});
inline constexpr auto kStartStateOptions = params::options<StartState>(
    {{StartState::Released, "Released"}, {StartState::Restore, "As before the restart or bus power failure"}});
inline constexpr auto kLockStartOptions = params::options<LockAction>(
    {{LockAction::Unchanged, "Unchanged"}, {LockAction::On, "On"}, {LockAction::Off, "Off"}});
inline constexpr auto kLockEndOptions = params::options<LockAction>(
    {{LockAction::Unchanged, "Unchanged"}, {LockAction::On, "On"}, {LockAction::Off, "Off"},
     {LockAction::Last, "As commanded meanwhile"}});
inline constexpr auto kSceneActionOptions = params::options<bool>({{false, "Off"}, {true, "On"}});

enum class Dev : uint8_t { StartDelay, Heartbeat, RateLimit };
enum class In : uint8_t {
    Function, Contact, Debounce, OnClose, OnOpen, ShortAction, LongAction, LongTime, DimDirection,
    ShutterDirection, Value, Scene, PulsesPerUnit, SendDelta, Lock, SendAfterStart, Cyclic,
};
enum class Out : uint8_t {
    Function, Mode, Status, Start, OnDelay, OffDelay, StairTime, Retrigger, OffWithZero, Warning,
    PulseLength, Lock, LockStart, LockEnd, Central,
    Scene1, Scene1On, Scene2, Scene2On, Scene3, Scene3On, Scene4, Scene4On,
    Cycle, Monitor, Safety,
};

inline constexpr auto kDeviceParameters = params::parameterBlock<Dev>(
    {.key = "device", .name = "Device"},
    params::param<Dev::StartDelay, uint8_t>(
        {.name = "Delay before sending after a start", .def = 0, .min = 0, .max = 255, .unit = "s"}),
    params::param<Dev::Heartbeat, uint16_t>(
        {.name = "\"In operation\" every (0 = never)", .def = 0, .min = 0, .max = 65535, .unit = "s"}),
    params::param<Dev::RateLimit, uint8_t>(
        {.name = "Telegrams per second at most (0 = no limit)", .def = 0, .min = 0, .max = 50}));

inline constexpr auto kInputParameters = params::parameterBlock<In>(
    {.key = "input", .name = "Input {n}"},
    params::param<In::Function, InputFunction>(
        {.name = "Function", .def = InputFunction::Switch, .options = kInputFunctionOptions}),
    params::param<In::Contact, ContactType>({.name = "Contact type",
                                             .def = ContactType::NormallyOpen,
                                             .options = kContactTypeOptions,
                                             .visible = params::whenNot<In::Function>(InputFunction::Disabled)}),
    params::param<In::Debounce, DebounceTime>({.name = "Debounce time",
                                               .def = DebounceTime::Ms30,
                                               .options = kDebounceTimeOptions,
                                               .visible = params::whenNot<In::Function>(InputFunction::Disabled)}),
    params::param<In::OnClose, EdgeAction>(
        {.name = "On closing", .def = EdgeAction::On, .options = kEdgeActionOptions,
         .visible = params::whenAny<In::Function>(InputFunction::Switch, InputFunction::Alarm, InputFunction::WindowDoor,
                                                  InputFunction::Occupancy, InputFunction::OpenClose)}),
    params::param<In::OnOpen, EdgeAction>(
        {.name = "On opening", .def = EdgeAction::Off, .options = kEdgeActionOptions,
         .visible = params::whenAny<In::Function>(InputFunction::Switch, InputFunction::Alarm, InputFunction::WindowDoor,
                                                  InputFunction::Occupancy, InputFunction::OpenClose)}),
    params::param<In::ShortAction, EdgeAction>({.name = "Short press",
                                                .def = EdgeAction::Toggle,
                                                .options = kEdgeActionOptions,
                                                .visible = params::when<In::Function>(InputFunction::ShortLong)}),
    params::param<In::LongAction, EdgeAction>({.name = "Long press",
                                               .def = EdgeAction::Toggle,
                                               .options = kEdgeActionOptions,
                                               .visible = params::when<In::Function>(InputFunction::ShortLong)}),
    params::param<In::LongTime, uint8_t>(
        {.name = "Long press after (× 0.1 s)", .def = 5, .min = 3, .max = 50,
         .visible = params::whenAny<In::Function>(InputFunction::ShortLong, InputFunction::Dimming,
                                                  InputFunction::Shutter, InputFunction::Scene)}),
    params::param<In::DimDirection, Direction>({.name = "Button",
                                                .def = Direction::OneButton,
                                                .options = kDimDirectionOptions,
                                                .visible = params::when<In::Function>(InputFunction::Dimming)}),
    params::param<In::ShutterDirection, Direction>({.name = "Button",
                                                    .def = Direction::OneButton,
                                                    .options = kShutterDirectionOptions,
                                                    .visible = params::when<In::Function>(InputFunction::Shutter)}),
    params::param<In::Value, uint8_t>(
        {.name = "Value", .def = 0, .min = 0, .max = 255,
         .visible = params::whenAny<In::Function>(InputFunction::Value, InputFunction::Percent)}),
    params::param<In::Scene, uint8_t>({.name = "Scene number", .def = 1, .min = 1, .max = 64,
                                       .visible = params::when<In::Function>(InputFunction::Scene)}),
    params::param<In::PulsesPerUnit, uint16_t>(
        {.name = "Pulses per unit", .def = 1, .min = 1, .max = 65535,
         .visible = params::whenAny<In::Function>(InputFunction::Counter, InputFunction::EnergyCounter)}),
    params::param<In::SendDelta, uint16_t>(
        {.name = "Send after an increase of (units)", .def = 1, .min = 1, .max = 65535,
         .visible = params::whenAny<In::Function>(InputFunction::Counter, InputFunction::EnergyCounter)}),
    params::param<In::Lock, Use>({.name = "Lock object",
                                  .def = Use::Disabled,
                                  .options = kUseOptions,
                                  .visible = params::whenNot<In::Function>(InputFunction::Disabled)}),
    params::param<In::SendAfterStart, bool>(
        {.name = "Send after a start", .def = false, .options = kNoYesOptions,
         .visible = params::whenAny<In::Function>(InputFunction::Switch, InputFunction::Alarm, InputFunction::WindowDoor,
                                                  InputFunction::Occupancy, InputFunction::OpenClose,
                                                  InputFunction::Counter, InputFunction::EnergyCounter)}),
    params::param<In::Cyclic, uint16_t>(
        {.name = "Send cyclically every (0 = never)", .def = 0, .min = 0, .max = 65535, .unit = "s",
         .visible = params::whenAny<In::Function>(InputFunction::Switch, InputFunction::Alarm, InputFunction::WindowDoor,
                                                  InputFunction::Occupancy, InputFunction::OpenClose,
                                                  InputFunction::Counter, InputFunction::EnergyCounter)}));

inline constexpr auto kSwitchingFunctions = params::whenAny<Out::Function>(
    OutputFunction::Switch, OutputFunction::Staircase, OutputFunction::Pulse);

inline constexpr auto kOutputParameters = params::parameterBlock<Out>(
    {.key = "output", .name = "Output {n}"},
    params::param<Out::Function, OutputFunction>(
        {.name = "Function", .def = OutputFunction::Switch, .options = kOutputFunctionOptions}),
    params::param<Out::Mode, OperatingMode>({.name = "Operating mode",
                                             .def = OperatingMode::NormallyOpen,
                                             .options = kOperatingModeOptions,
                                             .visible = params::whenNot<Out::Function>(OutputFunction::Disabled)}),
    params::param<Out::Status, StatusObject>({.name = "Status object",
                                              .def = StatusObject::OnChange,
                                              .options = kStatusObjectOptions,
                                              .visible = params::whenNot<Out::Function>(OutputFunction::Disabled)}),
    params::param<Out::Start, StartState>(
        {.name = "State after a restart", .def = StartState::Released, .options = kStartStateOptions,
         .visible = kSwitchingFunctions}),
    params::param<Out::OnDelay, uint16_t>({.name = "Switch-on delay", .def = 0, .min = 0, .max = 65535, .unit = "s",
                                           .visible = params::when<Out::Function>(OutputFunction::Switch)}),
    params::param<Out::OffDelay, uint16_t>({.name = "Switch-off delay", .def = 0, .min = 0, .max = 65535, .unit = "s",
                                            .visible = params::when<Out::Function>(OutputFunction::Switch)}),
    params::param<Out::StairTime, uint16_t>({.name = "Staircase time", .def = 120, .min = 1, .max = 65535, .unit = "s",
                                             .visible = params::when<Out::Function>(OutputFunction::Staircase)}),
    params::param<Out::Retrigger, bool>({.name = "Retrigger while on", .def = true, .options = kNoYesOptions,
                                         .visible = params::when<Out::Function>(OutputFunction::Staircase)}),
    params::param<Out::OffWithZero, bool>({.name = "Switch off with 0", .def = true, .options = kNoYesOptions,
                                           .visible = params::when<Out::Function>(OutputFunction::Staircase)}),
    params::param<Out::Warning, uint8_t>(
        {.name = "Warn this long before the end (0 = no warning)", .def = 0, .min = 0, .max = 255, .unit = "s",
         .visible = params::when<Out::Function>(OutputFunction::Staircase)}),
    params::param<Out::PulseLength, uint8_t>({.name = "Pulse length (× 0.1 s)", .def = 10, .min = 1, .max = 255,
                                              .visible = params::when<Out::Function>(OutputFunction::Pulse)}),
    params::param<Out::Lock, Use>({.name = "Lock object",
                                   .def = Use::Disabled,
                                   .options = kUseOptions,
                                   .visible = params::whenNot<Out::Function>(OutputFunction::Disabled)}),
    params::param<Out::LockStart, LockAction>({.name = "When the lock starts",
                                               .def = LockAction::Unchanged,
                                               .options = kLockStartOptions,
                                               .visible = params::when<Out::Lock>(Use::Enabled)}),
    params::param<Out::LockEnd, LockAction>({.name = "When the lock ends",
                                             .def = LockAction::Unchanged,
                                             .options = kLockEndOptions,
                                             .visible = params::when<Out::Lock>(Use::Enabled)}),
    params::param<Out::Central, bool>({.name = "Follows the central switch", .def = false, .options = kNoYesOptions,
                                       .visible = kSwitchingFunctions}),
#if H8I8O_OUTPUT_SCENES
    params::section(
        {.name = "Scenes", .visible = kSwitchingFunctions},
        params::param<Out::Scene1, uint8_t>({.name = "Scene A number (0 = unused)", .def = 0, .min = 0, .max = 64}),
        params::param<Out::Scene1On, bool>({.name = "Scene A", .def = false, .options = kSceneActionOptions}),
        params::param<Out::Scene2, uint8_t>({.name = "Scene B number (0 = unused)", .def = 0, .min = 0, .max = 64}),
        params::param<Out::Scene2On, bool>({.name = "Scene B", .def = false, .options = kSceneActionOptions}),
        params::param<Out::Scene3, uint8_t>({.name = "Scene C number (0 = unused)", .def = 0, .min = 0, .max = 64}),
        params::param<Out::Scene3On, bool>({.name = "Scene C", .def = false, .options = kSceneActionOptions}),
        params::param<Out::Scene4, uint8_t>({.name = "Scene D number (0 = unused)", .def = 0, .min = 0, .max = 64}),
        params::param<Out::Scene4On, bool>({.name = "Scene D", .def = false, .options = kSceneActionOptions})),
#endif
    params::param<Out::Cycle, uint8_t>({.name = "Cycle time", .def = 10, .min = 1, .max = 60, .unit = "min",
                                        .visible = params::when<Out::Function>(OutputFunction::Valve)}),
    params::param<Out::Monitor, uint8_t>(
        {.name = "Safety value after no control value for (0 = never)", .def = 0, .min = 0, .max = 255,
         .unit = "min", .visible = params::when<Out::Function>(OutputFunction::Valve)}),
    params::param<Out::Safety, uint8_t>({.name = "Safety value", .def = 30, .min = 0, .max = 100, .unit = "%",
                                         .visible = params::when<Out::Function>(OutputFunction::Valve)}));

inline constexpr auto kParameters = params::parameterLayout(
    kDeviceParameters, params::repeat<kChannels>(kInputParameters), params::repeat<kChannels>(kOutputParameters));

// ── The product ──────────────────────────────────────────────────────────────

#if KNX_SECURE_ENABLED
inline constexpr bool kDataSecure = true;
#else
inline constexpr bool kDataSecure = false;
#endif

inline constexpr auto kProduct = makeCommissionedProduct<kParameters>(
    makeEndpointDefinition<Port,
        H8I8O_INPUT_PORTS(1) H8I8O_INPUT_PORTS(2) H8I8O_INPUT_PORTS(3) H8I8O_INPUT_PORTS(4)
        H8I8O_INPUT_PORTS(5) H8I8O_INPUT_PORTS(6) H8I8O_INPUT_PORTS(7) H8I8O_INPUT_PORTS(8)
        H8I8O_OUTPUT_PORTS(1) H8I8O_OUTPUT_PORTS(2) H8I8O_OUTPUT_PORTS(3) H8I8O_OUTPUT_PORTS(4)
        H8I8O_OUTPUT_PORTS(5) H8I8O_OUTPUT_PORTS(6) H8I8O_OUTPUT_PORTS(7) H8I8O_OUTPUT_PORTS(8)
        OutS<kCentralPort, "central", "Central switch">,
        OutT<kInOperationPort, "in_operation", "In operation">>(
        ProductIdentity{
            .productKey = CONFIG_H8I8O_PRODUCT_KEY,
            .productDisplayName = CONFIG_H8I8O_PRODUCT_NAME,
            .manufacturerId = ManufacturerId(0x00FA),  // KNstaX's example ID: not a registered product (README)
            .medium = endpoint::Medium::TP1,
            .applicationNumber = CONFIG_H8I8O_APPLICATION_NUMBER,
            .applicationVersion = 5,  // bump when objects, parameters or knx.defconfig change
            .firmwareRevision = 4,
            .maxApduLength = 55,      // CONFIG_KNX_MAX_APDU_LENGTH in knx.defconfig
            .hardwareSerialNumber = CONFIG_H8I8O_HARDWARE_SERIAL,
        },
        PersistencePolicy{
            .namespacePrefix = "h8i8o",
            .schemaVersion = 2,
            .persistKnxState = true,
        },
        // KNX Data Secure: ETS may secure any object; none has to be. The
        // table sizes are the KNX_SECURITY_* capacities in knx.defconfig.
        SecurityPolicy{
            .dataSecureCapable = kDataSecure,
            .groupObjectRequirement = SecurityRequirement::None,
            .individualAddressEntries = 16,
            .groupKeyTableEntries = 64,
            .p2pKeyTableEntries = 2,
        }));

// ── Object views: what ETS shows (export data, never linked) ─────────────────

namespace detail {

inline constexpr size_t kMaxObjectViews = 256;

struct ObjectViewList {
    std::array<views::ObjectView, kMaxObjectViews> views{};
    size_t count{0};

    constexpr void add(Port port, uint16_t instance, params::Condition visible, std::string_view function,
                       views::DatapointType dpt)
    {
        views[count++] = views::ObjectView{
            static_cast<uint16_t>(port),
            {.instance = instance, .visible = visible, .function = function, .dpt = dpt}};
    }
};

consteval ObjectViewList buildObjectViews()
{
    using F = InputFunction;
    using O = OutputFunction;
    ObjectViewList list;
    for (uint16_t ch = 0; ch < kChannels; ++ch) {
        const auto in = [&](InSlot slot, F function, std::string_view text, views::DatapointType dpt) {
            list.add(inPort(ch, slot), ch, params::when<In::Function>(function), text, dpt);
        };
        in(InSlot::A, F::Switch, "Switch", {1, 1});
        in(InSlot::A, F::Alarm, "Alarm", {1, 5});
        in(InSlot::A, F::WindowDoor, "Window or door", {1, 19});
        in(InSlot::A, F::Occupancy, "Presence", {1, 18});
        in(InSlot::A, F::OpenClose, "Open/close", {1, 9});
        in(InSlot::A, F::ShortLong, "Short press", {1, 1});
        in(InSlot::B, F::ShortLong, "Long press", {1, 1});
        if (kInputDimming) {
            in(InSlot::A, F::Dimming, "Switch", {1, 1});
            in(InSlot::C, F::Dimming, "Dimming", {3, 7});
        }
        if (kInputShutter) {
            in(InSlot::A, F::Shutter, "Move", {1, 8});
            in(InSlot::B, F::Shutter, "Stop/step", {1, 7});
        }
        if (kInputValue) {
            in(InSlot::D, F::Value, "Value", {5, 10});
            in(InSlot::D, F::Percent, "Percentage", {5, 1});
            in(InSlot::D, F::Scene, "Scene", {18, 1});
        }
        if (kInputCounter) {
            in(InSlot::E, F::Counter, "Pulses", {12, 1});
            in(InSlot::E, F::EnergyCounter, "Energy", {13, 10});
        }
        list.add(inPort(ch, InSlot::L), ch, params::when<In::Lock>(Use::Enabled), "Lock (1 = locked)", {1, 1});

        const auto out = [&](OutSlot slot, params::Condition visible, std::string_view text,
                             views::DatapointType dpt) { list.add(outPort(ch, slot), ch, visible, text, dpt); };
        out(OutSlot::S, params::when<Out::Function>(O::Switch), "Switch", {1, 1});
        out(OutSlot::S, params::when<Out::Function>(O::Staircase), "Staircase", {1, 1});
        out(OutSlot::S, params::when<Out::Function>(O::Pulse), "Pulse", {1, 1});
        out(OutSlot::T, params::whenNot<Out::Function>(O::Disabled), "Status", {1, 1});
        out(OutSlot::K, params::when<Out::Lock>(Use::Enabled), "Lock (1 = locked)", {1, 1});
        if (kOutputForced) {
            out(OutSlot::F, params::whenNot<Out::Function>(O::Disabled), "Forced operation", {2, 1});
        }
        if (kOutputScenes) out(OutSlot::V, kSwitchingFunctions, "Scene", {18, 1});
        if (kOutputValve) out(OutSlot::V, params::when<Out::Function>(O::Valve), "Control value", {5, 1});
    }
    list.add(kCentralPort, 0, {}, "Switch", {1, 1});
    list.add(kInOperationPort, 0, params::whenNot<Dev::Heartbeat>(0), "In operation", {1, 2});
    return list;
}

inline constexpr ObjectViewList kObjectViewList = buildObjectViews();

}  // namespace detail

inline constexpr auto kObjectViews = [] {
    std::array<views::ObjectView, detail::kObjectViewList.count> views{};
    for (size_t i = 0; i < views.size(); ++i) views[i] = detail::kObjectViewList.views[i];
    return views;
}();

}  // namespace h8i8o
