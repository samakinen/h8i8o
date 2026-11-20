// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file main.cpp
 * @brief H8i8o firmware: KNstaX on the board, 8 inputs and 8 outputs
 *
 * One main loop plus two interrupts: USART1 receive (the TP-UART2's octets
 * and the acknowledge decision, in KNstaX's backend) and SysTick (1 ms). The
 * loop runs the stack, the flash log's background step, the input scan and
 * debounce, the programming button, the RTT command channel and the
 * watchdog, then sleeps until the next interrupt.
 *
 * Flash erases: the FMC driver masks interrupts for the 37-39 ms erase and the
 * backend, as FlashLog's erase guard, puts the TP-UART2 in busy mode for it
 * (docs/firmware.md).
 *
 * RTT commands (one character each, on the RTT terminal):
 *   p  toggle programming mode (as the button does)
 *   s  print statistics
 *   i  print the raw input levels
 *   1-8 toggle that output locally and send its status
 *   a  print the individual address, device serial, lifecycle state and trace drops
 *   R  restart
 *
 * Every octet on the host link is logged too (printTrace), one line per burst.
 */

#include "product.hpp"

#include "board.h"
#include "gd32_fmc.h"
#include "rtt.h"
#include "usart1.h"

#include "knx/objects/flash_log.hpp"
#include "knx/objects/flash_log_persistence.hpp"
#include "knx/objects/object_persistence.hpp"
#include "knx/physical/tpuart_medium_backend.hpp"
#include "knx/platform/cooperative_platform.hpp"
#include "knx/util/heap.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

extern "C" std::size_t g_heap_high_water;  // startup/syscalls.c

using namespace h8i8o;

namespace {

// ── Log over RTT ────────────────────────────────────────────────────────────

class Line {
public:
    Line& operator<<(const char* text) {
        while (*text != 0) put(*text++);
        return *this;
    }
    Line& operator<<(uint32_t value) {
        char digits[10];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);
        while (n > 0) put(digits[--n]);
        return *this;
    }
    Line& hex(uint32_t value, int width) {
        for (int shift = (width - 1) * 4; shift >= 0; shift -= 4) put("0123456789ABCDEF"[(value >> shift) & 0xFu]);
        return *this;
    }
    Line& bits(uint8_t value) {
        for (int i = 0; i < 8; ++i) put((value & (1u << i)) ? '1' : '0');  // DI1/DO1 first
        return *this;
    }
    ~Line() {
        put('\n');
        rtt_write(_text, _length);
    }

private:
    void put(char c) {
        if (_length < sizeof _text - 1u) _text[_length++] = c;
    }
    char _text[240];
    uint32_t _length{0};
};

Line rlog() {
    Line line;
    const uint32_t ms = board_millis();
    line << "[" << ms / 1000u << "." ;
    line << (ms / 100u) % 10u << (ms / 10u) % 10u << ms % 10u << "] ";
    return line;
}

// ── Platform hooks ──────────────────────────────────────────────────────────

uint32_t hookMillis(void*) { return board_millis(); }
uint64_t hookMicros(void*) { return board_micros(); }
void hookIdle(void*) { asm volatile("wfi"); }
uint32_t hookSerial(void*) { return board_serial_number(); }
void hookRestart(void*) {
    rlog() << "restart requested by the stack";
    board_restart();
}
void hookFatal(void*) {
    rlog() << "fatal error in the stack";
    board_restart();
}
void hookLogLine(void*, const char* text) { rlog() << text; }

// ── Persistence ─────────────────────────────────────────────────────────────

knx::objects::FlashLog g_flashLog(h8i8o_fmc_hal());

std::unique_ptr<knx::objects::ObjectPersistence> makePersistence() {
    return std::make_unique<knx::objects::FlashLogPersistence>(g_flashLog);
}

// ── Inputs and outputs ──────────────────────────────────────────────────────

constexpr uint32_t kButtonDebounceMs = 50;

// The parameters as the main loop uses them: one bit per channel. Loaded at
// start and after every completed ETS download.
struct Config {
    uint8_t inputEnabled{0};
    uint8_t inputInverted{0};          // contact type "normally closed"
    uint8_t debounceMs[kChannels]{};
    uint8_t outputEnabled{0};
    uint8_t outputNormallyClosed{0};   // operating mode: 1 = de-energise
    uint8_t statusEnabled{0};
};

Config g_config;

void loadConfig(params::ViewFor<kParameters> parameters) {
    Config config{};
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        const uint8_t bit = static_cast<uint8_t>(1u << ch);
        const auto in = parameters.block<In>(ch);
        if (in.get<In::Function>() != InputFunction::Disabled) config.inputEnabled |= bit;
        if (in.get<In::Contact>() == ContactType::NormallyClosed) config.inputInverted |= bit;
        config.debounceMs[ch] = static_cast<uint8_t>(in.get<In::Debounce>());

        const auto out = parameters.block<Out>(ch);
        if (out.get<Out::Channel>() == ChannelUse::Enabled) config.outputEnabled |= bit;
        if (out.get<Out::Mode>() == OperatingMode::NormallyClosed) config.outputNormallyClosed |= bit;
        if (out.get<Out::Status>() != StatusObject::Disabled) config.statusEnabled |= bit;
    }
    g_config = config;
}

// The inputs as the application sees them: contact type applied, a disabled
// channel reads inactive.
uint8_t readLogicalInputs() {
    return static_cast<uint8_t>((board_read_inputs() ^ g_config.inputInverted) & g_config.inputEnabled);
}

// The outputs as the bus sees them: operating mode applied.
uint8_t logicalOutputs() {
    return static_cast<uint8_t>(board_outputs() ^ g_config.outputNormallyClosed);
}

struct Debounce {
    uint8_t stable{0};
    uint8_t candidate{0};
    uint32_t since{0};
    uint8_t lastRaw{0};

    // Returns the bits that changed their stable value.
    uint8_t update(uint8_t raw, uint32_t now, uint32_t holdMs) {
        if (raw != candidate) {
            candidate = raw;
            since = now;
            return 0u;
        }
        if (candidate == stable || now - since < holdMs) return 0u;
        const uint8_t changed = static_cast<uint8_t>(stable ^ candidate);
        stable = candidate;
        return changed;
    }
};

// Eight inputs, each with its own debounce time.
struct InputDebounce {
    uint8_t stable{0};
    uint8_t candidate{0};
    uint8_t lastRaw{0};
    uint32_t since[kChannels]{};

    // Returns the bits that changed their stable value.
    uint8_t update(uint8_t raw, uint32_t now, const uint8_t* holdMs) {
        uint8_t changed = 0;
        for (unsigned ch = 0; ch < kChannels; ++ch) {
            const uint8_t bit = static_cast<uint8_t>(1u << ch);
            if (((raw ^ candidate) & bit) != 0u) {
                candidate = static_cast<uint8_t>(candidate ^ bit);
                since[ch] = now;
            } else if (((candidate ^ stable) & bit) != 0u && now - since[ch] >= holdMs[ch]) {
                stable = static_cast<uint8_t>(stable ^ bit);
                changed = static_cast<uint8_t>(changed | bit);
            }
        }
        return changed;
    }
};

InputDebounce g_inputs;
Debounce g_button;
uint8_t g_statusDirty = 0;  // output status objects to send

// Calls fn(std::integral_constant<unsigned, i>) for a run-time channel index.
template <typename Fn, unsigned... I>
void withChannel(unsigned channel, Fn&& fn, std::integer_sequence<unsigned, I...>) {
    ((channel == I ? (fn(std::integral_constant<unsigned, I>{}), 0) : 0), ...);
}
template <typename Fn>
void withChannel(unsigned channel, Fn&& fn) {
    withChannel(channel, std::forward<Fn>(fn), std::make_integer_sequence<unsigned, kChannels>{});
}

// `on` is the value of the switch object; the operating mode decides what the
// pin does with it.
void setOutput(unsigned channel, bool on) {
    const uint8_t bit = static_cast<uint8_t>(1u << channel);
    if ((g_config.outputEnabled & bit) == 0u) {
        rlog() << "DO" << channel + 1u << " is disabled";
        return;
    }
    const bool energise = on != ((g_config.outputNormallyClosed & bit) != 0u);
    uint8_t mask = board_outputs();
    mask = energise ? static_cast<uint8_t>(mask | bit) : static_cast<uint8_t>(mask & ~bit);
    board_set_outputs(mask);
    g_statusDirty = static_cast<uint8_t>(g_statusDirty | bit);
    rlog() << "DO" << channel + 1u << (on ? " on" : " off") << (energise ? ", energised" : ", released");
}

using Bindings = CommissionedBindingsBuilder<std::remove_cvref_t<decltype(kProduct)>, kDefaultBindingCapacity>;

template <unsigned... I>
void bindChannels(Bindings& bindings, std::integer_sequence<unsigned, I...>) {
    (bindings.template onCommand<outputPort(I)>([](bool on) { setOutput(I, on); }), ...);
    (bindings.template provideState<inputPort(I)>([]() -> bool { return (g_inputs.stable >> I) & 1u; }), ...);
    (bindings.template provideState<statusPort(I)>([]() -> bool { return (logicalOutputs() >> I) & 1u; }), ...);
}

const char* lifecycleText(DeviceLifecycleState state) {
    switch (state) {
        case DeviceLifecycleState::Uncommissioned: return "uncommissioned";
        case DeviceLifecycleState::Commissioning: return "programming mode";
        case DeviceLifecycleState::Operational: return "operational";
    }
    return "?";
}

void printBackend(const knx::physical::TpUartMediumBackend& backend) {
    const auto s = backend.statistics();
    rlog() << "tpuart rx " << s.framesReceived << " ack " << s.acksSent << " busy " << s.busySent
          << " echo " << s.echoesDropped << " con+ " << s.confirmsPositive << " con- " << s.confirmsNegative
          << " conTO " << s.confirmTimeouts;
    rlog() << "tpuart err cks " << s.checksumErrors << " oct " << s.octetErrorFrames << " part "
          << s.partialFramesDiscarded << " ovf " << s.rxOverflowDrops << " unexp " << s.unexpectedOctets
          << " resets " << s.chipResets;
    rlog().hex(s.lastStateBits, 2) << " state, missedPolls " << s.missedStatePolls
          << " erases " << s.erasesGuarded << "/" << s.erasesDeclined << " deferred " << s.ackDecisionsDeferred;
    const auto u = h8i8o_usart1_stats();
    rlog() << "usart rx " << u.rx_octets << " err " << u.rx_errors << " tx " << u.tx_octets << " refused "
          << u.tx_refused << " rtt dropped " << rtt_dropped();
    rlog() << "heap high water " << static_cast<uint32_t>(g_heap_high_water) << " locked allocs "
          << static_cast<uint32_t>(knx::heap::violationCount());
}

// ── Host link trace ─────────────────────────────────────────────────────────
// One RTT line per burst of octets (bsp/usart1.h): "rx" from the chip, "tx" to
// it, with the burst's start time in ms modulo 16384. The once-a-second state
// poll (tx 02, rx 07) is left out.

struct TraceBurst {
    uint8_t octets[96];
    uint32_t count{0};
    uint32_t ms{0};
    bool tx{false};
    bool open{false};

    void flush() {
        if (!open) return;
        open = false;
        if (count == 1u && ((tx && octets[0] == 0x02u) || (!tx && octets[0] == 0x07u))) return;
        Line line = rlog();
        line << (tx ? "tx " : "rx ") << ms << ":";
        for (uint32_t i = 0; i < count && i < sizeof octets; ++i) {
            line << " ";
            line.hex(octets[i], 2);
        }
        if (count > sizeof octets) line << " +" << count - static_cast<uint32_t>(sizeof octets);
    }
};

TraceBurst g_burst;

void printTrace() {
    uint16_t entries[32];
    const size_t n = h8i8o_usart1_trace_read(entries, sizeof entries / sizeof entries[0]);
    for (size_t i = 0; i < n; ++i) {
        const uint16_t e = entries[i];
        if ((e & H8I8O_USART1_TRACE_MARK) != 0u) {
            g_burst.flush();
            g_burst = TraceBurst{};
            g_burst.open = true;
            g_burst.tx = (e & H8I8O_USART1_TRACE_TX) != 0u;
            g_burst.ms = e & 0x3FFFu;
        } else if (g_burst.open) {
            if (g_burst.count < sizeof g_burst.octets) g_burst.octets[g_burst.count] = static_cast<uint8_t>(e);
            g_burst.count++;
        }
    }
}

void printAddress(const char* what, uint16_t address) {
    rlog() << what << (address >> 12) << "." << ((address >> 8) & 0xFu) << "." << (address & 0xFFu);
}

}  // namespace

int main() {
    board_init();
    rtt_init();
    const uint32_t cause = board_reset_cause();
    {
        Line line = rlog();
        line << "h8i8o start, reset: " << board_reset_cause_text(cause) << ", serial 00FA";
        line.hex(board_serial_number(), 8);
    }

    // Persistence first: mount, then finish any erase a reset interrupted
    // before the stack's first save; otherwise that save can fail.
    if (const auto mounted = g_flashLog.mount(); mounted.isError()) {
        rlog() << "flash log mount failed: " << static_cast<uint32_t>(mounted.error());
    }
    for (int i = 0; i < 8 && g_flashLog.servicePending(); ++i) {
        (void)g_flashLog.service();
    }
    knx::objects::setPersistenceFactory(&makePersistence);

    knx::platform::CooperativeHooks hooks{};
    hooks.millis = &hookMillis;
    hooks.micros = &hookMicros;
    hooks.idle = &hookIdle;
    hooks.serialNumber = &hookSerial;
    hooks.restart = &hookRestart;
    hooks.fatalError = &hookFatal;
    hooks.logLine = &hookLogLine;
    static knx::platform::CooperativePlatform platform(hooks);

    auto* backend = new knx::physical::TpUartMediumBackend(h8i8o_usart1_hal(), platform);

    g_inputs.lastRaw = board_read_input_levels();

    auto bindings = makeCommissionedBindings(kProduct);
    bindChannels(bindings, std::make_integer_sequence<unsigned, kChannels>{});
    bindings.onParametersChanged([](params::ViewFor<kParameters> parameters) {
        loadConfig(parameters);
        rlog() << "parameters downloaded";
    });
    bindings.onLifecycleChanged([](DeviceLifecycleState state) {
        board_set_led(state == DeviceLifecycleState::Commissioning);
        rlog() << "lifecycle: " << lifecycleText(state);
    });
    auto started = startCommissionedProduct(platform, kProduct, std::move(bindings),
                                            std::unique_ptr<knx::physical::Tp1MediumBackend>(backend));
    if (started.isError()) {
        rlog() << "start failed: " << static_cast<uint32_t>(started.error());
        // Outputs stay inactive; the watchdog is not running yet. Try again in 10 s.
        const uint32_t t0 = board_millis();
        while (board_millis() - t0 < 10000u) asm volatile("wfi");
        board_restart();
    }
    auto app = std::move(started.value());
    loadConfig(app->parameters());
    g_inputs.stable = g_inputs.candidate = readLogicalInputs();
    g_flashLog.setEraseGuard(backend);
    knx::heap::lock();

    {
        Line line = rlog();
        const uint16_t address = app->individualAddress().raw;
        line << "started, address " << (address >> 12) << "." << ((address >> 8) & 0xFu) << "." << (address & 0xFFu)
             << ", heap " << static_cast<uint32_t>(g_heap_high_water)
             << (app->parametersLoaded() ? ", parameters loaded" : ", parameters NOT loaded (defaults)");
    }
    board_watchdog_start();

    uint32_t lastTick = board_millis();
    uint16_t lastAddress = app->individualAddress().raw;
    for (;;) {
        app->loop();
        (void)g_flashLog.service();
        printTrace();
        if (const uint16_t address = app->individualAddress().raw; address != lastAddress) {
            lastAddress = address;
            printAddress("address now ", address);
        }

        const uint32_t now = board_millis();
        if (now != lastTick) {
            lastTick = now;

            const uint8_t levels = board_read_input_levels();
            if (levels != g_inputs.lastRaw) {
                g_inputs.lastRaw = levels;
                rlog().bits(levels);  // raw pin levels, DI1 first
            }
            const uint8_t changed = g_inputs.update(readLogicalInputs(), now, g_config.debounceMs);
            for (unsigned ch = 0; ch < kChannels; ++ch) {
                if ((changed & (1u << ch)) == 0u) continue;
                const bool on = (g_inputs.stable >> ch) & 1u;
                rlog() << "DI" << ch + 1u << (on ? " active" : " inactive");
                if (app->lifecycleState() == DeviceLifecycleState::Operational) {
                    withChannel(ch, [&](auto c) { (void)app->template publish<inputPort(c)>(on); });
                }
            }

            const uint8_t buttonChanged = g_button.update(board_button_pressed() ? 1u : 0u, now, kButtonDebounceMs);
            if ((buttonChanged & 1u) != 0u && g_button.stable == 1u) {
                rlog() << "button";
                app->toggleProgrammingMode();
            }
        }

        if (g_statusDirty != 0u && app->lifecycleState() == DeviceLifecycleState::Operational) {
            for (unsigned ch = 0; ch < kChannels; ++ch) {
                if ((g_statusDirty & (1u << ch)) == 0u) continue;
                if ((g_config.statusEnabled & (1u << ch)) == 0u) {
                    g_statusDirty = static_cast<uint8_t>(g_statusDirty & ~(1u << ch));
                    continue;
                }
                const bool on = (logicalOutputs() >> ch) & 1u;
                bool sent = false;
                withChannel(ch, [&](auto c) { sent = app->template publish<statusPort(c)>(on).isOk(); });
                if (!sent) break;  // the send pipeline is full: next pass
                g_statusDirty = static_cast<uint8_t>(g_statusDirty & ~(1u << ch));
            }
        }

        if (const int c = rtt_read_char(); c >= 0) {
            if (c == 'p') {
                app->toggleProgrammingMode();
            } else if (c == 's') {
                printBackend(*backend);
            } else if (c == 'i') {
                Line line = rlog();
                line << "DI levels ";
                line.bits(board_read_input_levels());
                line << " button " << (board_button_pressed() ? 1u : 0u);
            } else if (c >= '1' && c <= '8') {
                const unsigned ch = static_cast<unsigned>(c - '1');
                setOutput(ch, ((logicalOutputs() >> ch) & 1u) == 0u);
            } else if (c == 'a') {
                printAddress("address ", app->individualAddress().raw);
                {
                    // PID_SERIAL_NUMBER as ETS reads it, not the board's number.
                    Line line = rlog();
                    line << "device serial ";
                    for (const uint8_t octet : app->deviceObject().getSerialNumber()) {
                        line.hex(octet, 2);
                    }
                }
                rlog() << "lifecycle: " << lifecycleText(app->lifecycleState()) << ", trace dropped "
                       << h8i8o_usart1_trace_dropped();
            } else if (c == 'R') {
                board_restart();
            }
        }

        board_watchdog_feed();
        asm volatile("wfi");
    }
}
