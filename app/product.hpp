// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file product.hpp
 * @brief H8i8o product: 8 switch inputs and 8 switch outputs
 *
 * The single source of truth for the firmware and the ETS product file
 * (tools/knxprod). No hardware or OS dependencies here.
 *
 * Objects: each input sends its state (DPT 1.001) when it changes and answers
 * reads; each output has a switch object and a status object.
 *
 * Parameters, declared with KNstaX's parameter layout: per input the function
 * (Disabled or Switch), the contact type and the debounce time; per output the
 * channel, the operating mode, the status object and the state after a
 * restart. docs/application.md describes them for ETS users.
 *
 * The capacities the product file declares (group addresses, associations,
 * APDU length) come from knx.defconfig, which the firmware and the product
 * file build both apply.
 *
 * Bump PersistencePolicy::schemaVersion whenever a port is added, removed or
 * reordered: the stored layout is positional.
 */

#pragma once

#include "knx/product/commissioned_product.hpp"

namespace h8i8o {

using namespace knx;
using namespace knx::product;

enum class Port : uint16_t {
    In1 = 0, In2, In3, In4, In5, In6, In7, In8,
    Out1 = 8, Out2, Out3, Out4, Out5, Out6, Out7, Out8,
    Status1 = 16, Status2, Status3, Status4, Status5, Status6, Status7, Status8,
};

inline constexpr uint16_t kChannels = 8;

constexpr Port inputPort(unsigned channel) { return static_cast<Port>(0u + channel); }
constexpr Port outputPort(unsigned channel) { return static_cast<Port>(8u + channel); }
constexpr Port statusPort(unsigned channel) { return static_cast<Port>(16u + channel); }

// Inputs and status objects are not persisted: they mirror hardware.
template <Port P, FixedString Key, FixedString Name>
using InputState = semantics::SwitchState<P, Key, Name, false>;
template <Port P, FixedString Key, FixedString Name>
using OutputStatus = semantics::SwitchState<P, Key, Name, false>;
template <Port P, FixedString Key, FixedString Name>
using OutputSwitch = semantics::SwitchCommand<P, Key, Name>;

// ── Parameters ──────────────────────────────────────────────────────────────
// Enum values are what ETS stores; do not renumber them without a new
// application version.

enum class InputFunction : uint8_t { Disabled = 0, Switch = 1 };
enum class ContactType : uint8_t { NormallyOpen = 0, NormallyClosed = 1 };
/// The value is the time in milliseconds.
enum class DebounceTime : uint8_t { Ms10 = 10, Ms20 = 20, Ms30 = 30, Ms50 = 50, Ms100 = 100, Ms200 = 200 };
enum class ChannelUse : uint8_t { Disabled = 0, Enabled = 1 };
enum class OperatingMode : uint8_t { NormallyOpen = 0, NormallyClosed = 1 };
enum class StatusObject : uint8_t { Disabled = 0, OnChange = 1 };
enum class StartState : uint8_t { Released = 0, Restore = 1 };

inline constexpr auto kInputFunctionOptions =
    params::options<InputFunction>({{InputFunction::Disabled, "Disabled"}, {InputFunction::Switch, "Switch"}});
inline constexpr auto kContactTypeOptions = params::options<ContactType>(
    {{ContactType::NormallyOpen, "Normally open"}, {ContactType::NormallyClosed, "Normally closed"}});
inline constexpr auto kDebounceTimeOptions = params::options<DebounceTime>(
    {{DebounceTime::Ms10, "10 ms"}, {DebounceTime::Ms20, "20 ms"}, {DebounceTime::Ms30, "30 ms"},
     {DebounceTime::Ms50, "50 ms"}, {DebounceTime::Ms100, "100 ms"}, {DebounceTime::Ms200, "200 ms"}});
inline constexpr auto kChannelUseOptions =
    params::options<ChannelUse>({{ChannelUse::Disabled, "Disabled"}, {ChannelUse::Enabled, "Enabled"}});
inline constexpr auto kOperatingModeOptions = params::options<OperatingMode>(
    {{OperatingMode::NormallyOpen, "Normally open (1 = energise)"},
     {OperatingMode::NormallyClosed, "Normally closed (1 = de-energise)"}});
inline constexpr auto kStatusObjectOptions = params::options<StatusObject>(
    {{StatusObject::Disabled, "Disabled"}, {StatusObject::OnChange, "Send on change"}});
inline constexpr auto kStartStateOptions = params::options<StartState>(
    {{StartState::Released, "Released"}, {StartState::Restore, "As before the restart or bus power failure"}});

enum class In : uint8_t { Function, Contact, Debounce };
enum class Out : uint8_t { Channel, Mode, Status, Start };

// The default function is Switch, so every input sends after a download with
// default parameters.
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
                                               .visible = params::whenNot<In::Function>(InputFunction::Disabled)}));

inline constexpr auto kOutputParameters = params::parameterBlock<Out>(
    {.key = "output", .name = "Output {n}"},
    params::param<Out::Channel, ChannelUse>(
        {.name = "Channel", .def = ChannelUse::Enabled, .options = kChannelUseOptions}),
    params::param<Out::Mode, OperatingMode>({.name = "Operating mode",
                                             .def = OperatingMode::NormallyOpen,
                                             .options = kOperatingModeOptions,
                                             .visible = params::when<Out::Channel>(ChannelUse::Enabled)}),
    params::param<Out::Status, StatusObject>({.name = "Status object",
                                              .def = StatusObject::OnChange,
                                              .options = kStatusObjectOptions,
                                              .visible = params::when<Out::Channel>(ChannelUse::Enabled)}),
    params::param<Out::Start, StartState>({.name = "State after a restart",
                                           .def = StartState::Released,
                                           .options = kStartStateOptions,
                                           .visible = params::when<Out::Channel>(ChannelUse::Enabled)}));

inline constexpr auto kParameters =
    params::parameterLayout(params::repeat<kChannels>(kInputParameters), params::repeat<kChannels>(kOutputParameters));

inline constexpr auto kProduct = makeCommissionedProduct<kParameters>(
    makeEndpointDefinition<Port,
        InputState<Port::In1, "in_1", "Input 1">,
        InputState<Port::In2, "in_2", "Input 2">,
        InputState<Port::In3, "in_3", "Input 3">,
        InputState<Port::In4, "in_4", "Input 4">,
        InputState<Port::In5, "in_5", "Input 5">,
        InputState<Port::In6, "in_6", "Input 6">,
        InputState<Port::In7, "in_7", "Input 7">,
        InputState<Port::In8, "in_8", "Input 8">,
        OutputSwitch<Port::Out1, "out_1", "Output 1 switch">,
        OutputSwitch<Port::Out2, "out_2", "Output 2 switch">,
        OutputSwitch<Port::Out3, "out_3", "Output 3 switch">,
        OutputSwitch<Port::Out4, "out_4", "Output 4 switch">,
        OutputSwitch<Port::Out5, "out_5", "Output 5 switch">,
        OutputSwitch<Port::Out6, "out_6", "Output 6 switch">,
        OutputSwitch<Port::Out7, "out_7", "Output 7 switch">,
        OutputSwitch<Port::Out8, "out_8", "Output 8 switch">,
        OutputStatus<Port::Status1, "status_1", "Output 1 status">,
        OutputStatus<Port::Status2, "status_2", "Output 2 status">,
        OutputStatus<Port::Status3, "status_3", "Output 3 status">,
        OutputStatus<Port::Status4, "status_4", "Output 4 status">,
        OutputStatus<Port::Status5, "status_5", "Output 5 status">,
        OutputStatus<Port::Status6, "status_6", "Output 6 status">,
        OutputStatus<Port::Status7, "status_7", "Output 7 status">,
        OutputStatus<Port::Status8, "status_8", "Output 8 status">>(
        ProductIdentity{
            .productKey = "h8i8o",
            .productDisplayName = "H8i8o 8 inputs 8 outputs",
            .manufacturerId = ManufacturerId(0x00FA),  // KNstaX's example ID: not a registered product (README)
            .medium = endpoint::Medium::TP1,
            .applicationNumber = 1,
            .applicationVersion = 3,  // bump when objects, parameters or knx.defconfig change
            .firmwareRevision = 2,
            .maxApduLength = 55,      // CONFIG_KNX_MAX_APDU_LENGTH in knx.defconfig
        },
        PersistencePolicy{
            .namespacePrefix = "h8i8o",
            .schemaVersion = 1,
            .persistKnxState = true,
        }));

}  // namespace h8i8o
