// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file input_channel.hpp
 * @brief One input's function: what its contact sends, and when
 *
 * Driven by the debounced contact (edge()), the 1 ms tick (tick()) and the
 * objects the bus writes (writeA(), writeLock(), writeCount()). What it sends
 * goes to an InputSink as (channel, object, value); the glue turns that into
 * a telegram. docs/design/channel-functions.md §4 describes the functions.
 *
 * No KNstaX, no hardware: tested on the host (test/).
 */

#pragma once

#include "logic/functions.hpp"

#include <cstdint>

namespace h8i8o {

/// The objects of an input (docs/design/channel-functions.md §3.2). The lock
/// object L only receives, so it is not here.
enum class InObject : uint8_t {
    A,  ///< 1 bit: switch, short press, dimming switch, shutter move
    B,  ///< 1 bit: long press, shutter stop/step
    C,  ///< 4 bit (DPT 3.007): the value is control << 3 | step code
    D,  ///< 1 octet: value or scene
    E,  ///< 4 octets: counter
};

class InputSink {
public:
    virtual void send(uint8_t channel, InObject object, uint32_t value) = 0;

protected:
    ~InputSink() = default;
};

/// An input's parameters, decoded from the ETS image by the glue. Everything
/// defaults to zero, so the channels need no initial image in flash; the glue
/// sets every field, and configure() takes 0 for the counts that cannot be.
struct InputConfig {
    /// Any InputFunction; config() reports the one it behaves as.
    InputFunction function{};
    EdgeAction onClose{};          ///< Switch
    EdgeAction onOpen{};           ///< Switch
    EdgeAction shortAction{};      ///< Short/long press, on A
    EdgeAction longAction{};       ///< Short/long press, on B
    uint16_t longMs{};             ///< long press after this
    Direction dimDirection{};
    Direction shutterDirection{};
    ValueType valueType{};         ///< Percent for InputFunction::Percent
    uint8_t value{};               ///< Value
    uint8_t scene{};               ///< Scene, 1-64
    uint16_t pulsesPerUnit{};      ///< Counter (0 counts as 1)
    uint16_t sendDelta{};          ///< Counter: send after this many units (0 counts as 1)
    bool lockEnabled{};
    bool sendAfterStart{};
};

class InputChannel {
public:
    void setIndex(uint8_t index) { _index = index; }

    /// New parameters. The run-time state (A, the count, a press in
    /// progress) is kept: a parameter written from the device itself takes
    /// this path too.
    void configure(const InputConfig& config);
    const InputConfig& config() const { return _config; }

    /// The debounced contact changed: active = closed (contact type applied).
    void edge(bool active, uint32_t now, InputSink& sink);
    /// Every millisecond.
    void tick(uint32_t now, InputSink& sink);

    /// A was written from the bus: a toggle continues from there.
    void writeA(bool value) { _a = value; }
    /// The lock object. Without a lock object configured it is ignored.
    void writeLock(bool locked);
    /// E was written from the bus: the count, in units.
    void writeCount(uint32_t units);

    /// Once, when the device starts sending after a start: "Send after start".
    void sendAfterStart(bool active, InputSink& sink);

    bool a() const { return _a; }
    bool locked() const { return _locked; }
    /// The counter in pulses (what is stored), and in units (what is sent).
    uint32_t pulses() const { return _pulses; }
    uint32_t units() const;
    /// The stored count, at start, before anything is counted.
    void restorePulses(uint32_t pulses);

private:
    void apply(EdgeAction action, InObject object, bool& state, InputSink& sink);
    void press(uint32_t now, InputSink& sink);
    void release(InputSink& sink);
    void longPress(InputSink& sink);
    void count(InputSink& sink);
    uint8_t encodedValue() const;
    void send(InObject object, uint32_t value, InputSink& sink) { sink.send(_index, object, value); }

    InputConfig _config{};
    uint8_t _index{0};
    bool _a{false};
    bool _b{false};
    bool _locked{false};
    bool _pressed{false};
    bool _long{false};          ///< the press in progress became a long one
    bool _dimDown{false};       ///< direction of the dimming in progress, or the last one
                                ///< (up at first, so a lit light dims down first)
    bool _shutterUp{false};     ///< direction of the last shutter move (down at first)
    uint32_t _pressedAt{0};
    uint32_t _pulses{0};
    uint32_t _sentUnits{0};
};

}  // namespace h8i8o
