// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file main.cpp
 * @brief H8i8o firmware: KNstaX on the board, 8 inputs and 8 outputs
 *
 * One main loop plus three interrupts: USART1 receive (the TP-UART2's octets
 * and the acknowledge decision, in KNstaX's backend), the TP-UART2's SAVE edge
 * (bus power failing) and SysTick (1 ms). The loop runs the stack, the flash
 * log's background step, the input scan and debounce, the channel logic, the
 * programming button, the RTT command channel and the watchdog, then sleeps
 * until the next interrupt.
 *
 * Channels: what each input and output does is the channel logic's
 * (app/logic, docs/design/channel-functions.md). This file is the glue: it
 * decodes the parameters into the logic's configuration, binds the objects to
 * it, turns what it sends into telegrams, drives the pins from what it says,
 * and keeps the output states and the counters in flash.
 *
 * Programming button: a press toggles programming mode, which also ends by
 * itself after 15 minutes. Held for 10 s the LED flashes fast; released while
 * it flashes, the device does a factory reset (KNstaX's factoryReset(): what
 * ETS's Master Reset 02h does). Held for 15 s, nothing happens.
 *
 * Bus power failure: SAVE releases every output at once, from the interrupt,
 * so the loads stop draining the bus capacitors; the loop then stores the
 * outputs' switch values (and the counters) in the flash log. If the bus
 * comes back before the supply gives out, the outputs follow the logic again.
 *
 * KNX Data Secure (variants with KNX_SECURE_ENABLED): at the first start the
 * firmware makes the device's Factory Tool Key (FDSK) from its random source
 * and keeps it in the flash log, among the product's own records, which
 * neither a factory reset nor a firmware update with a new persistence layout
 * erases. ETS needs it as the device certificate, which the log prints at
 * every start (and the k command on request). The random source (S-A_Sync
 * responses, the FDSK) is AES in counter mode, seeded at every start from ADC
 * noise, the unique ID and a start counter (knx::security::SeededRandom).
 *
 * Flash erases: the FMC driver masks interrupts for the 37-39 ms erase and the
 * backend, as FlashLog's erase guard, puts the TP-UART2 in busy mode for it
 * (docs/firmware.md).
 *
 * RTT commands (on the RTT terminal; all but b are one character):
 *   p  toggle programming mode (as the button does)
 *   s  print statistics
 *   i  print the raw input levels
 *   1-8 toggle that output's switch value locally
 *   a  print the individual address, device serial, lifecycle state (and trace drops)
 *   k  print the device certificate (serial number and FDSK, for ETS) and the
 *      Data Secure state (Data Secure variants)
 *   R  restart
 *   b<area>.<line>.<device>,<count>,<gap ms>;  send that many A_DeviceDescriptor_Read
 *      to the device (a bus test generator, see stepBurst; CONFIG_H8I8O_BUS_TEST);
 *      x stops it
 *
 * Built with H8I8O_TRACE=ON, every octet on the host link is logged too
 * (printTrace), one line per burst.
 */

#include "product.hpp"

#include "board.h"
#include "gd32_fmc.h"
#include "rtt.h"
#include "usart1.h"

#include "logic/debounce.hpp"
#include "logic/input_channel.hpp"
#include "logic/output_channel.hpp"

#include "knx/objects/flash_log.hpp"
#include "knx/objects/flash_log_persistence.hpp"
#include "knx/objects/interface_object_manager.hpp"
#include "knx/objects/object_persistence.hpp"
#include "knx/physical/tpuart_medium_backend.hpp"
#include "knx/platform/cooperative_platform.hpp"
#if KNX_SECURE_ENABLED
#include "knx/security/device_certificate.hpp"
#include "knx/security/seeded_random.hpp"
#endif
#include "knx/util/heap.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

extern "C" std::size_t g_heap_high_water;        // startup/syscalls.c
extern "C" uint32_t startup_stack_peak(void);     // startup/startup.c
extern "C" uint32_t startup_stack_reserve(void);  // startup/startup.c

using namespace h8i8o;

namespace {

// ── Log over RTT ────────────────────────────────────────────────────────────

// One log line: "[s.mmm] text\n". It streams through a small buffer that is
// written to RTT whenever it fills and at the end, so a line costs 72 B of
// stack wherever it is logged (the stack's own log lines arrive deep in the
// receive path) and is never truncated. Only the main loop logs, so lines do
// not interleave; a line that logs while it is being built would.
class Line {
public:
    Line() {
        const uint32_t ms = board_millis();
        *this << "[" << ms / 1000u << "." << (ms / 100u) % 10u << (ms / 10u) % 10u << ms % 10u << "] ";
    }
    Line(const Line&) = delete;
    Line& operator=(const Line&) = delete;

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
        flush();
    }

private:
    void put(char c) {
        if (_length == sizeof _text) flush();
        _text[_length++] = c;
    }
    void flush() {
        rtt_write(_text, _length);
        _length = 0;
    }
    char _text[64];
    uint32_t _length{0};
};

// Guaranteed copy elision: the line is built in the caller's frame.
Line rlog() { return Line(); }

// ── Platform hooks ──────────────────────────────────────────────────────────

uint32_t hookMillis(void*) { return board_millis(); }
uint64_t hookMicros(void*) { return board_micros(); }
void hookIdle(void*) { asm volatile("wfi"); }
uint32_t hookSerial(void*) { return board_serial_number(); }
void restartDevice(const char* why);
void hookRestart(void*) {
    rlog() << "restart requested by the stack";
    restartDevice("restart");
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

// The product's own records, in the key range the stack leaves alone
// (InterfaceObjectManager::kProductKeyIdFirst..Last): a factory reset does not
// touch them, and a new persistence layout keeps them.
constexpr uint16_t kProductKeyFirst = knx::objects::InterfaceObjectManager::kProductKeyIdFirst;
constexpr uint16_t kOutputStateKey = kProductKeyFirst;       // output switch values
constexpr uint16_t kFdskKey = kProductKeyFirst + 1u;         // Factory Tool Key, 16 octets
constexpr uint16_t kStartCounterKey = kProductKeyFirst + 2u; // starts so far, 4 octets
constexpr uint16_t kCounterKey = kProductKeyFirst + 3u;      // input counters, 8 × 4 octets

uint32_t loadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
           static_cast<uint32_t>(p[2]) << 8 | p[3];
}
void storeU32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

// ── Random source and Factory Tool Key (KNX Data Secure) ────────────────────

#if KNX_SECURE_ENABLED
knx::security::SeededRandom g_random;
std::array<uint8_t, 16> g_fdsk{};

void hookRandom(void*, uint8_t* out, size_t length) { g_random.fill(std::span<uint8_t>(out, length)); }

// Seeds the random source: a start counter kept in flash (so no two seedings
// are the same, whatever the noise), the unique ID, the time, and 1024 ADC
// conversions of the temperature sensor and VREFINT, whose low bits are noise.
// At start, before the heap is locked: load() allocates.
void seedRandom() {
    uint32_t starts = 0;
    std::vector<uint8_t> record;
    if (g_flashLog.load(kStartCounterKey, record).isOk() && record.size() == 4u) starts = loadU32(record.data());
    ++starts;
    uint8_t counter[4];
    storeU32(counter, starts);
    if (const auto saved = g_flashLog.save(kStartCounterKey, counter); saved.isError()) {
        rlog() << "start counter not stored: " << static_cast<uint32_t>(saved.error());
    }
    g_random.absorb(counter);

    uint8_t uid[12];
    board_unique_id(uid);
    g_random.absorb(uid);

    uint16_t samples[128];
    for (int round = 0; round < 8; ++round) {
        board_adc_noise(samples, sizeof samples / sizeof samples[0]);
        g_random.absorb(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(samples), sizeof samples));
    }
    const uint64_t us = board_micros();
    g_random.absorb(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&us), sizeof us));
    g_random.finish();
}

// The Factory Tool Key: the stored one, or a new one at the first start. It
// must stay the same for the device's life: ETS knows the device by it. Only
// a full erase of the flash makes a new one (and a new device certificate).
void loadOrMakeFdsk() {
    std::vector<uint8_t> record;
    const auto nonZero = [](const std::array<uint8_t, 16>& key) {
        return std::any_of(key.begin(), key.end(), [](uint8_t b) { return b != 0u; });
    };
    if (g_flashLog.load(kFdskKey, record).isOk() && record.size() == g_fdsk.size()) {
        std::copy(record.begin(), record.end(), g_fdsk.begin());
        if (nonZero(g_fdsk)) return;
    }
    do {
        g_random.fill(g_fdsk);
    } while (!nonZero(g_fdsk));
    if (const auto saved = g_flashLog.save(kFdskKey, g_fdsk); saved.isError()) {
        // Without it the next start makes another key, and ETS loses the device.
        rlog() << "factory tool key NOT stored: " << static_cast<uint32_t>(saved.error());
        return;
    }
    rlog() << "factory tool key made";
}
#endif  // KNX_SECURE_ENABLED

// ── Channels ────────────────────────────────────────────────────────────────

constexpr uint32_t kButtonDebounceMs = 50;

InputChannel g_in[kChannels];
OutputChannel g_out[kChannels];

// What the glue itself needs of the parameters: one bit per channel, and the
// device's settings. Loaded at start and after every completed ETS download
// (and when the device changes a parameter itself: a learned scene).
struct Config {
    uint8_t inputEnabled{0};
    uint8_t inputInverted{0};          // contact type "normally closed"
    uint8_t debounceMs[kChannels]{};
    uint8_t outputEnabled{0};          // function other than Disabled
    uint8_t outputNormallyClosed{0};   // operating mode: 1 = de-energise
    uint8_t statusEnabled{0};
    uint8_t restoreAtStart{0};         // state after a restart: as before
    uint8_t central{0};                // follows the central switch
    uint16_t cyclicS[kChannels]{};
    uint16_t heartbeatS{0};
    uint8_t startDelayS{0};
    uint8_t rateLimit{0};
};

Config g_config;
bool g_policiesDue = false;  // transmit policies to apply from the loop

void loadConfig(params::ViewFor<kParameters> parameters) {
    const uint32_t now = board_millis();
    Config config{};
    const auto device = parameters.block<Dev>();
    config.startDelayS = device.get<Dev::StartDelay>();
    config.heartbeatS = device.get<Dev::Heartbeat>();
    config.rateLimit = device.get<Dev::RateLimit>();
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        const uint8_t bit = static_cast<uint8_t>(1u << ch);
        const auto in = parameters.block<In>(ch);
        InputConfig ic{};
        ic.function = in.get<In::Function>();
        ic.onClose = in.get<In::OnClose>();
        ic.onOpen = in.get<In::OnOpen>();
        ic.shortAction = in.get<In::ShortAction>();
        ic.longAction = in.get<In::LongAction>();
        ic.longMs = static_cast<uint16_t>(in.get<In::LongTime>() * 100u);
        ic.dimDirection = in.get<In::DimDirection>();
        ic.shutterDirection = in.get<In::ShutterDirection>();
        ic.value = in.get<In::Value>();
        ic.scene = in.get<In::Scene>();
        ic.pulsesPerUnit = in.get<In::PulsesPerUnit>();
        ic.sendDelta = in.get<In::SendDelta>();
        ic.lockEnabled = in.get<In::Lock>() == Use::Enabled;
        ic.sendAfterStart = in.get<In::SendAfterStart>();
        g_in[ch].configure(ic);
        if (ic.function != InputFunction::Disabled) config.inputEnabled |= bit;
        if (in.get<In::Contact>() == ContactType::NormallyClosed) config.inputInverted |= bit;
        config.debounceMs[ch] = static_cast<uint8_t>(in.get<In::Debounce>());
        config.cyclicS[ch] = in.get<In::Cyclic>();

        const auto out = parameters.block<Out>(ch);
        OutputConfig oc{};
        oc.function = out.get<Out::Function>();
        oc.onDelayS = out.get<Out::OnDelay>();
        oc.offDelayS = out.get<Out::OffDelay>();
        oc.staircaseS = out.get<Out::StairTime>();
        oc.retrigger = out.get<Out::Retrigger>();
        oc.offWithZero = out.get<Out::OffWithZero>();
        oc.warningS = out.get<Out::Warning>();
        oc.pulse100ms = out.get<Out::PulseLength>();
        oc.lockEnabled = out.get<Out::Lock>() == Use::Enabled;
        oc.lockStart = out.get<Out::LockStart>();
        oc.lockEnd = out.get<Out::LockEnd>();
        oc.central = out.get<Out::Central>();
#if H8I8O_OUTPUT_SCENES
        oc.sceneNumber[0] = out.get<Out::Scene1>();
        oc.sceneOn[0] = out.get<Out::Scene1On>();
        oc.sceneNumber[1] = out.get<Out::Scene2>();
        oc.sceneOn[1] = out.get<Out::Scene2On>();
        oc.sceneNumber[2] = out.get<Out::Scene3>();
        oc.sceneOn[2] = out.get<Out::Scene3On>();
        oc.sceneNumber[3] = out.get<Out::Scene4>();
        oc.sceneOn[3] = out.get<Out::Scene4On>();
#endif
        oc.cycleMin = out.get<Out::Cycle>();
        oc.monitorMin = out.get<Out::Monitor>();
        oc.safetyPercent = out.get<Out::Safety>();
        g_out[ch].configure(oc, now);
        if (oc.function != OutputFunction::Disabled) config.outputEnabled |= bit;
        if (out.get<Out::Mode>() == OperatingMode::NormallyClosed) config.outputNormallyClosed |= bit;
        if (out.get<Out::Status>() != StatusObject::Disabled) config.statusEnabled |= bit;
        if (out.get<Out::Start>() == StartState::Restore) config.restoreAtStart |= bit;
        if (oc.central) config.central |= bit;
    }
    g_config = config;
    g_policiesDue = true;
}

// The inputs as the logic sees them: contact type applied, a disabled
// channel reads inactive.
uint8_t readLogicalInputs() {
    return static_cast<uint8_t>((board_read_inputs() ^ g_config.inputInverted) & g_config.inputEnabled);
}

InputDebounce g_inputs;
Debounce g_button;
uint8_t g_statusDirty = 0;  // output status objects to send
uint8_t g_learn[kChannels]{};  // scene slots to learn, per output (OutputChannel::scene)

// Sending starts once the device is operational and its start delay is over.
bool g_sending = false;
uint32_t g_startedAt = 0;

// ── Bus power ───────────────────────────────────────────────────────────────
// The stack debounces SAVE (20 ms down, 200 ms up) and reports the link state;
// its early warning arrives from the SAVE edge interrupt, ahead of the
// debounce. The interrupt releases every output; the main loop does the rest
// (followBusPower) and keeps them released while SAVE is asserted. It logs
// "pf <ms>" every millisecond while SAVE stays asserted: the last line the
// host received is the run time on the capacitors, to within the host's
// polling interval.

volatile bool g_powerFailWarning = false;
volatile uint32_t g_powerFailAtMs = 0u;
bool g_saveAsserted = false;                  // the main loop's view

void onPowerFailIsr(void*, uint64_t) {
    if (!g_powerFailWarning) board_set_outputs(0u);
    g_powerFailAtMs = board_millis();
    g_powerFailWarning = true;
}

// Drives the pins from the logic (operating mode applied) and notes the
// status objects that are due. A disabled output stays released.
void applyOutputs() {
    uint8_t pins = 0;
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        const uint8_t bit = static_cast<uint8_t>(1u << ch);
        if (g_out[ch].takeStatus()) g_statusDirty = static_cast<uint8_t>(g_statusDirty | bit);
        if ((g_config.outputEnabled & bit) == 0u) continue;
        if (g_out[ch].on() != ((g_config.outputNormallyClosed & bit) != 0u)) pins = static_cast<uint8_t>(pins | bit);
    }
    if (g_saveAsserted || g_powerFailWarning) return;  // released until the bus is back
    const uint8_t was = board_outputs();
    if (pins == was) return;
    board_set_outputs(pins);
    Line line = rlog();
    line << "DO ";
    line.bits(pins);
}

// ── Output states across a restart ──────────────────────────────────────────
// The outputs' switch values, one bit per channel, in a flash log record of
// the product's own. Written when SAVE warns of a bus power failure and before
// a planned restart, and only when it changed; read at start for the outputs
// whose state after a restart is "as before". A reset without warning (the
// watchdog, a fault, a power cut SAVE did not report) finds the last record.

uint8_t g_storedOutputs = 0u;
bool g_storedOutputsKnown = false;
bool g_factoryReset = false;  // the restart that follows must not store them again

uint8_t switchValues() {
    uint8_t values = 0;
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        if (g_out[ch].switchValue()) values = static_cast<uint8_t>(values | (1u << ch));
    }
    return static_cast<uint8_t>(values & g_config.outputEnabled);
}

void storeOutputs(const char* why) {
    const uint8_t values = switchValues();
    if (g_factoryReset || (g_storedOutputsKnown && values == g_storedOutputs)) return;
    const uint8_t record[1] = {values};
    if (const auto saved = g_flashLog.save(kOutputStateKey, record); saved.isError()) {
        rlog() << "output states not stored (" << why << "): " << static_cast<uint32_t>(saved.error());
        return;
    }
    g_storedOutputs = values;
    g_storedOutputsKnown = true;
    Line line = rlog();
    line << "output states stored (" << why << ") ";
    line.bits(values);
}

// At start, before the heap is locked: load() allocates. An output that is
// not restored starts released; one in "normally closed" mode with the Switch
// function therefore stands at on, as it always has, without a status.
void restoreOutputs(uint32_t now) {
    std::vector<uint8_t> record;
    if (g_flashLog.load(kOutputStateKey, record).isOk() && record.size() == 1u) {
        g_storedOutputs = record[0];
        g_storedOutputsKnown = true;
    }
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        const uint8_t bit = static_cast<uint8_t>(1u << ch);
        if ((g_config.outputEnabled & bit) == 0u) continue;
        if ((g_config.restoreAtStart & bit) != 0u) {
            if (g_storedOutputsKnown) g_out[ch].command((g_storedOutputs & bit) != 0u, now);
        } else if ((g_config.outputNormallyClosed & bit) != 0u &&
                   g_out[ch].config().function == OutputFunction::Switch) {
            g_out[ch].command(true, now);
            (void)g_out[ch].takeStatus();
        }
    }
}

// ── Counters across a restart ───────────────────────────────────────────────
// Stored when SAVE warns, before a planned restart, and every 15 minutes
// while a count changed (docs/design/channel-functions.md §3.6).

#if H8I8O_INPUT_COUNTER
constexpr uint32_t kCounterSaveMs = 15u * 60u * 1000u;
uint32_t g_storedPulses[kChannels]{};
uint32_t g_countersSavedAt = 0;

void storeCounters(const char* why) {
    if (g_factoryReset) return;
    bool changed = false;
    uint8_t record[kChannels * 4u];
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        storeU32(&record[ch * 4u], g_in[ch].pulses());
        changed = changed || g_in[ch].pulses() != g_storedPulses[ch];
    }
    if (!changed) return;
    if (const auto saved = g_flashLog.save(kCounterKey, record); saved.isError()) {
        rlog() << "counters not stored (" << why << "): " << static_cast<uint32_t>(saved.error());
        return;
    }
    for (unsigned ch = 0; ch < kChannels; ++ch) g_storedPulses[ch] = g_in[ch].pulses();
    rlog() << "counters stored (" << why << ")";
}

void restoreCounters() {
    std::vector<uint8_t> record;
    if (g_flashLog.load(kCounterKey, record).isError() || record.size() != kChannels * 4u) return;
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        g_storedPulses[ch] = loadU32(&record[ch * 4u]);
        g_in[ch].restorePulses(g_storedPulses[ch]);
    }
}
#else
void storeCounters(const char*) {}
void restoreCounters() {}
#endif

void restartDevice(const char* why) {
    storeOutputs(why);
    storeCounters(why);
    board_restart();
}

// ── Object bindings ─────────────────────────────────────────────────────────
// The handlers run from app.loop(). They only feed the logic; what it sends
// and switches is picked up by the loop.

using Bindings = CommissionedBindingsBuilder<std::remove_cvref_t<decltype(kProduct)>, kDefaultBindingCapacity>;

#if H8I8O_OUTPUT_V
[[gnu::noinline]] void onOutputV(unsigned ch, uint8_t value) {
    const uint32_t now = board_millis();
    if (g_out[ch].config().function == OutputFunction::Valve) {
        g_out[ch].controlValue(value, now);
    } else {
        g_learn[ch] = static_cast<uint8_t>(g_learn[ch] | g_out[ch].scene(value, now));
    }
}
#endif

[[gnu::noinline]] void onCentral(bool on) {
    const uint32_t now = board_millis();
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        if ((g_config.central & (1u << ch)) != 0u) g_out[ch].command(on, now);
    }
}

// One handler type per kind of object, capturing the channel: a binding keeps
// a callable that small inside its entry, and the stack makes one thunk per
// callable type, so the eight channels share it (instead of 8 × 10 thunks).
auto inputWriteA(unsigned ch) { return [ch](bool value) { g_in[ch].writeA(value); }; }
auto inputA(unsigned ch) { return [ch]() -> bool { return g_in[ch].a(); }; }
auto inputLock(unsigned ch) { return [ch](bool locked) { g_in[ch].writeLock(locked); }; }
#if H8I8O_INPUT_COUNTER
auto inputWriteCount(unsigned ch) { return [ch](uint32_t units) { g_in[ch].writeCount(units); }; }
auto inputCount(unsigned ch) { return [ch]() -> uint32_t { return g_in[ch].units(); }; }
#endif
auto outputSwitch(unsigned ch) { return [ch](bool on) { g_out[ch].command(on, board_millis()); }; }
auto outputStatus(unsigned ch) { return [ch]() -> bool { return g_out[ch].on(); }; }
auto outputLock(unsigned ch) { return [ch](bool locked) { g_out[ch].lock(locked, board_millis()); }; }
#if H8I8O_OUTPUT_FORCED
auto outputForced(unsigned ch) {
    return [ch](knx::application::Dpt2Value value) { g_out[ch].forced(value.control, value.value, board_millis()); };
}
#endif
#if H8I8O_OUTPUT_V
auto outputV(unsigned ch) { return [ch](uint8_t value) { onOutputV(ch, value); }; }
#endif

template <unsigned I>
void bindChannel(Bindings& bindings) {
    bindings.template onStateWrite<inPort(I, InSlot::A)>(inputWriteA(I));
    bindings.template provideState<inPort(I, InSlot::A)>(inputA(I));
    bindings.template onCommand<inPort(I, InSlot::L)>(inputLock(I));
#if H8I8O_INPUT_COUNTER
    bindings.template onStateWrite<inPort(I, InSlot::E)>(inputWriteCount(I));
    bindings.template provideState<inPort(I, InSlot::E)>(inputCount(I));
#endif

    bindings.template onCommand<outPort(I, OutSlot::S)>(outputSwitch(I));
    bindings.template provideState<outPort(I, OutSlot::T)>(outputStatus(I));
    bindings.template onCommand<outPort(I, OutSlot::K)>(outputLock(I));
#if H8I8O_OUTPUT_FORCED
    bindings.template onCommand<outPort(I, OutSlot::F)>(outputForced(I));
#endif
#if H8I8O_OUTPUT_V
    bindings.template onCommand<outPort(I, OutSlot::V)>(outputV(I));
#endif
}

template <unsigned... I>
void bindChannels(Bindings& bindings, std::integer_sequence<unsigned, I...>) {
    (bindChannel<I>(bindings), ...);
    bindings.template onCommand<kCentralPort>([](bool on) { onCentral(on); });
}

// ── Telegrams from the logic ────────────────────────────────────────────────

template <typename App>
class BusSink final : public InputSink {
public:
    explicit BusSink(App& app) : _app(app) {}

    void send(uint8_t channel, InObject object, uint32_t value) override {
        if (!g_sending) return;  // the logic keeps its state; nothing is sent yet
        knx::util::Result<void> result = knx::util::Result<void>::ok();
        switch (object) {
            case InObject::A:
                result = _app.publish(inPort(channel, InSlot::A), value != 0u);
                break;
            case InObject::B:
                result = _app.publish(inPort(channel, InSlot::B), value != 0u);
                break;
#if H8I8O_INPUT_DIMMING
            case InObject::C:
                result = _app.publish(inPort(channel, InSlot::C),
                                      knx::application::Dpt3Value{(value & 0x8u) != 0u,
                                                                  static_cast<uint8_t>(value & 0x7u)});
                break;
#endif
#if H8I8O_INPUT_VALUE
            case InObject::D:
                result = _app.publish(inPort(channel, InSlot::D), static_cast<uint8_t>(value));
                break;
#endif
#if H8I8O_INPUT_COUNTER
            case InObject::E:
                result = _app.publish(inPort(channel, InSlot::E), value);
                break;
#endif
            default:
                break;
        }
        if (result.isError()) rlog() << "DI" << channel + 1u << " not sent: " << static_cast<uint32_t>(result.error());
    }

private:
    App& _app;
};

// Cyclic sending, the heartbeat and the rate limit, from the parameters. Each
// object that can send cyclically got its policy slot at start (the slot
// allocates); this only changes the settings.
template <typename App>
void applyPolicies(App& app) {
    const auto cyclic = [&](Port port, uint32_t seconds) {
        application::GroupObjectTransmitPolicy policy{};
        policy.cyclicIntervalMs = seconds * 1000u;
        (void)app.setTransmitPolicy(port, policy);
    };
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        const InputFunction function = g_in[ch].config().function;
        cyclic(inPort(ch, InSlot::A), function == InputFunction::Switch ? g_config.cyclicS[ch] : 0u);
#if H8I8O_INPUT_COUNTER
        cyclic(inPort(ch, InSlot::E), function == InputFunction::Counter ? g_config.cyclicS[ch] : 0u);
#endif
    }
    cyclic(kInOperationPort, g_config.heartbeatS);
    application::TelegramRateLimitConfig limit{};
    limit.maxTelegrams = g_config.rateLimit;
    app.setTelegramRateLimit(limit);
}

// Allocates the policy slot of every object that can send cyclically: a
// non-inert policy does, so it must happen before the heap is locked.
template <typename App>
void reservePolicies(App& app) {
    application::GroupObjectTransmitPolicy reserve{};
    reserve.cyclicIntervalMs = 0xFFFFFFFFu;
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        (void)app.setTransmitPolicy(inPort(ch, InSlot::A), reserve);
#if H8I8O_INPUT_COUNTER
        (void)app.setTransmitPolicy(inPort(ch, InSlot::E), reserve);
#endif
    }
    (void)app.setTransmitPolicy(kInOperationPort, reserve);
    applyPolicies(app);
}

// Writes the learned scenes: the action of each slot a store asked for
// becomes the output's state now.
template <typename App>
void learnScenes(App& app) {
#if H8I8O_OUTPUT_SCENES
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        const uint8_t slots = g_learn[ch];
        if (slots == 0u) continue;
        g_learn[ch] = 0u;
        const bool on = g_out[ch].on();
        if ((slots & 1u) != 0u) (void)app.template setParameter<Out::Scene1On>(ch, on);
        if ((slots & 2u) != 0u) (void)app.template setParameter<Out::Scene2On>(ch, on);
        if ((slots & 4u) != 0u) (void)app.template setParameter<Out::Scene3On>(ch, on);
        if ((slots & 8u) != 0u) (void)app.template setParameter<Out::Scene4On>(ch, on);
        rlog() << "DO" << ch + 1u << " scene learned: " << (on ? "on" : "off");
    }
#else
    (void)app;
#endif
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
          << " erases " << s.erasesGuarded << "/" << s.erasesDeclined << " deferred " << s.ackDecisionsDeferred
          << " pfw " << s.powerFailWarnings;
    const auto u = h8i8o_usart1_stats();
    rlog() << "usart rx " << u.rx_octets << " err " << u.rx_errors << " tx " << u.tx_octets << " refused "
          << u.tx_refused << " rtt dropped " << rtt_dropped();
    rlog() << "heap high water " << static_cast<uint32_t>(g_heap_high_water) << " locked allocs "
          << static_cast<uint32_t>(knx::heap::violationCount()) << ", stack peak " << startup_stack_peak() << " of "
          << startup_stack_reserve();
}

#if H8I8O_TRACE
// ── Host link trace ─────────────────────────────────────────────────────────
// One RTT line per burst of octets (bsp/usart1.h): "rx" from the chip, "tx" to
// it, with the burst's start time in ms modulo 16384. The once-a-second state
// poll (tx 02, rx 07) is left out.

bool g_statePollOutstanding = false;  // a U_State.req left out of the trace awaits its answer

struct TraceBurst {
    uint8_t octets[96];
    uint32_t count{0};
    uint32_t ms{0};
    bool tx{false};
    bool open{false};

    void flush() {
        if (!open) return;
        open = false;
        // The once-a-second U_State poll is left out: the U_State.req (02h) and
        // the 07h that answers it. A lone 07h is only that answer if a request
        // is outstanding; otherwise it is part of a frame split over two
        // bursts (the mask version 07B0h, say, while our own frame is fed).
        if (count == 1u && tx && octets[0] == 0x02u) {
            g_statePollOutstanding = true;
            return;
        }
        if (count == 1u && !tx && octets[0] == 0x07u && g_statePollOutstanding) {
            g_statePollOutstanding = false;
            return;
        }
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

[[gnu::noinline]] void printTrace() {
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
#endif

[[gnu::noinline]] void printAddress(const char* what, uint16_t address) {
    rlog() << what << (address >> 12) << "." << ((address >> 8) & 0xFu) << "." << (address & 0xFFu);
}

#ifdef CONFIG_H8I8O_BUS_TEST
// Bus test generator. Sends A_DeviceDescriptor_Read (connectionless) to one
// device, `count` times, `gap` ms apart, each send waiting for the TP-UART2's
// confirmation. With gap 0 the next request leaves while the target is still
// answering the last one, so both devices contend for the bus. The answers
// are not counted here: the host link trace shows them, with the repeat flag.
//
//   b<area>.<line>.<device>,<count>,<gap ms>;   e.g. b1.1.3,100,0;
//   x  stop a running burst
struct BusBurst {
    bool active = false;
    uint16_t target = 0;
    uint32_t total = 0, sent = 0, gapMs = 0, lastMs = 0, startMs = 0;
    uint32_t ok = 0, busy = 0, failed = 0, timeout = 0, other = 0;
};
BusBurst g_busTest;

struct CommandLine {
    bool collecting = false;
    char text[24] = {};
    size_t length = 0;
};
CommandLine g_command;

bool parseNumber(const char*& p, uint32_t& value, uint32_t max) {
    if (*p < '0' || *p > '9') return false;
    value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10u + static_cast<uint32_t>(*p++ - '0');
        if (value > max) return false;
    }
    return true;
}

bool parseBurst(const char* p, BusBurst& burst) {
    uint32_t area = 0, line = 0, device = 0, count = 0, gap = 0;
    if (!parseNumber(p, area, 15) || *p++ != '.' || !parseNumber(p, line, 15) || *p++ != '.' ||
        !parseNumber(p, device, 255) || *p++ != ',' || !parseNumber(p, count, 60000) || *p++ != ',' ||
        !parseNumber(p, gap, 60000) || *p != '\0' || count == 0) {
        return false;
    }
    burst = BusBurst{};
    burst.target = static_cast<uint16_t>((area << 12) | (line << 8) | device);
    burst.total = count;
    burst.gapMs = gap;
    return true;
}

void printBurst(const char* what) {
    rlog() << what << g_busTest.sent << "/" << g_busTest.total << " in " << (board_millis() - g_busTest.startMs)
           << " ms: ok " << g_busTest.ok << ", busy " << g_busTest.busy << ", failed " << g_busTest.failed
           << ", timeout " << g_busTest.timeout << ", other " << g_busTest.other;
}

template <typename App>
[[gnu::noinline]] void stepBurst(App& app) {
    if (!g_busTest.active || app.lifecycleState() != DeviceLifecycleState::Operational) return;
    if (g_busTest.sent != 0u && board_millis() - g_busTest.lastMs < g_busTest.gapMs) return;

    knx::datalink::LDataFrame frame;
    frame.source = app.individualAddress();
    frame.destination = knx::GroupAddress(g_busTest.target);
    frame.destinationType = knx::AddressType::Individual;
    frame.priority = knx::Priority::System;
    frame.hopCount = 6;
    static constexpr uint8_t kDeviceDescriptorRead[] = {0x03, 0x00};  // T_Data_Individual, A_DeviceDescriptor_Read type 0
    frame.tpdu.assign(std::span<const uint8_t>(kDeviceDescriptorRead));

    if (g_busTest.sent == 0u) g_busTest.startMs = board_millis();
    const auto result = app.link().sendFrame(frame);
    g_busTest.lastMs = board_millis();
    ++g_busTest.sent;
    if (result.isOk()) {
        ++g_busTest.ok;
    } else if (result.error() == knx::util::ErrorCode::Busy) {
        ++g_busTest.busy;
    } else if (result.error() == knx::util::ErrorCode::TransmissionFailed) {
        ++g_busTest.failed;
    } else if (result.error() == knx::util::ErrorCode::Timeout) {
        ++g_busTest.timeout;
    } else {
        ++g_busTest.other;
    }
    if (g_busTest.sent >= g_busTest.total) {
        g_busTest.active = false;
        printBurst("burst done ");
    }
}

// Collects a b command; true while one is being typed (the character is taken).
bool collectBurstCommand(int c) {
    if (g_command.collecting) {
        if (c == ';') {
            g_command.collecting = false;
            g_command.text[g_command.length] = '\0';
            if (parseBurst(g_command.text, g_busTest)) {
                g_busTest.active = true;
                Line line = rlog();
                line << "burst to " << (g_busTest.target >> 12) << "." << ((g_busTest.target >> 8) & 0xFu) << "."
                     << (g_busTest.target & 0xFFu) << ": " << g_busTest.total << " requests, gap " << g_busTest.gapMs
                     << " ms";
            } else {
                rlog() << "usage: b<area>.<line>.<device>,<count>,<gap ms>;";
            }
        } else if (g_command.length + 1u < sizeof(g_command.text)) {
            g_command.text[g_command.length++] = static_cast<char>(c);
        } else {
            g_command.collecting = false;
            rlog() << "usage: b<area>.<line>.<device>,<count>,<gap ms>;";
        }
        return true;
    }
    if (c == 'b') {
        g_command = CommandLine{};
        g_command.collecting = true;
        return true;
    }
    if (c == 'x') {
        if (g_busTest.active) {
            g_busTest.active = false;
            printBurst("burst stopped ");
        }
        return true;
    }
    return false;
}
#endif  // CONFIG_H8I8O_BUS_TEST

// ── Bus power, continued ────────────────────────────────────────────────────

uint32_t g_saveSince = 0u;
knx::physical::LinkState g_linkState = knx::physical::LinkState::Unknown;

template <typename App>
void followBusPower(const knx::physical::TpUartMediumBackend& backend, App& app, uint32_t now) {
    if (g_powerFailWarning && !g_saveAsserted) {
        g_saveAsserted = true;
        g_saveSince = g_powerFailAtMs;
        board_set_outputs(0u);  // again: a switch the loop was making may have undone the interrupt's
        rlog() << "power fail warning (SAVE edge), outputs released";
        storeOutputs("power fail");
        storeCounters("power fail");
        app.requestPowerFailSave();  // the stack's own state (Data Secure sequence numbers)
    }
    const bool failing = board_bus_power_failing();
    if (failing && !g_saveAsserted) {  // asserted without an edge seen (at start)
        g_saveAsserted = true;
        g_saveSince = now;
        rlog() << "SAVE asserted";
    } else if (!failing && g_saveAsserted) {
        // The bus came back before the supply gave out: the outputs follow
        // the logic again (applyOutputs).
        g_saveAsserted = false;
        g_powerFailWarning = false;
        rlog() << "SAVE released";
    } else if (failing) {
        rlog() << "pf " << (now - g_saveSince);
    }
    if (const auto state = backend.getLinkState(); state != g_linkState) {
        g_linkState = state;
        rlog() << "link " << knx::physical::linkStateToString(state);
    }
}

// ── Programming button ──────────────────────────────────────────────────────

constexpr uint32_t kFactoryResetHoldMs = 10000;   // held this long, the LED flashes ...
constexpr uint32_t kFactoryResetWindowMs = 5000;  // ... and a release within this resets
constexpr uint32_t kProgrammingModeTimeoutMs = 15u * 60u * 1000u;
constexpr uint32_t kFlashPeriodMs = 100;

struct ButtonState {
    uint32_t pressedAt = 0;
    bool armed = false;      // a release now is a factory reset
    bool cancelled = false;  // held past the window
    bool progMode = false;
    uint32_t progModeSince = 0;
};
ButtonState g_buttonState;

template <typename App>
void followButton(App& app, uint32_t now) {
    const uint8_t changed = g_button.update(board_button_pressed() ? 1u : 0u, now, kButtonDebounceMs);
    const bool pressed = g_button.stable == 1u;
    if ((changed & 1u) != 0u && pressed) {
        rlog() << "button";
        app.toggleProgrammingMode();
        g_buttonState.pressedAt = now;
        g_buttonState.armed = false;
        g_buttonState.cancelled = false;
    } else if ((changed & 1u) != 0u && g_buttonState.armed) {
        rlog() << "factory reset";
        g_buttonState.armed = false;
        (void)g_flashLog.erase(kOutputStateKey);
        (void)g_flashLog.erase(kCounterKey);
        g_factoryReset = true;
        const auto reset = app.factoryReset();  // restarts the device when it succeeds
        g_factoryReset = false;
        rlog() << "factory reset failed: " << static_cast<uint32_t>(reset.error());
        board_set_led(app.isProgrammingModeActive());
    } else if (pressed && !g_buttonState.cancelled) {
        const uint32_t held = now - g_buttonState.pressedAt;
        if (held >= kFactoryResetHoldMs + kFactoryResetWindowMs) {
            g_buttonState.armed = false;
            g_buttonState.cancelled = true;
            board_set_led(app.isProgrammingModeActive());
            rlog() << "factory reset not armed: held too long";
        } else if (held >= kFactoryResetHoldMs) {
            if (!g_buttonState.armed) rlog() << "factory reset armed: release the button";
            g_buttonState.armed = true;
            board_set_led(((held / kFlashPeriodMs) & 1u) == 0u);
        }
    }

    // Programming mode ends by itself, however it was turned on.
    const bool progMode = app.isProgrammingModeActive();
    if (progMode && !g_buttonState.progMode) g_buttonState.progModeSince = now;
    g_buttonState.progMode = progMode;
    if (progMode && now - g_buttonState.progModeSince >= kProgrammingModeTimeoutMs) {
        rlog() << "programming mode timed out";
        app.requestProgrammingMode(false);
    }
}

// ── Start-up and main loop ──────────────────────────────────────────────────
#if KNX_SECURE_ENABLED
// The device certificate: what ETS asks for when a secure device is added to
// a project (serial number and FDSK). Printed at every start and by the k
// command, on the debug probe's channel: the only one the board has besides
// the bus, and the one whoever installs this firmware has a probe on. It is
// the FDSK in clear, which is no secret from someone holding the device
// (docs/data-secure.md, "What Data Secure protects").
template <typename App>
[[gnu::noinline]] void printCertificate(App& app) {
    const auto serial = app.deviceObject().getSerialNumber();
    if (serial.size() != 6u) {
        rlog() << "device certificate: no serial number";
        return;
    }
    const auto text = knx::security::formatDeviceCertificate(std::span<const uint8_t, 6>(serial.data(), 6), g_fdsk);
    rlog() << "device certificate " << text.data();
    rlog() << "data secure " << (app.isSecurityEnabled() ? "on" : "off") << ", tool key "
           << (app.toolKeyInForceIs(g_fdsk) ? "FDSK" : "from ETS (the FDSK is back after a factory reset)");
}
#endif

// Start-up runs once, in a frame of its own (startDevice), so its locals (the
// bindings being built, the start result) are gone before the loop starts.
// The loop's jobs that log or publish are functions of their own as well:
// what stays on the stack for good is main()'s small frame, and the deepest
// path is the stack's receive chain on top of it.

using Backend = knx::physical::TpUartMediumBackend;

[[gnu::noinline]] auto startDevice(Backend*& backendOut) {
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

#if KNX_SECURE_ENABLED
    seedRandom();
    loadOrMakeFdsk();
#endif

    knx::platform::CooperativeHooks hooks{};
#if KNX_SECURE_ENABLED
    hooks.randomBytes = &hookRandom;
#endif
    hooks.millis = &hookMillis;
    hooks.micros = &hookMicros;
    hooks.idle = &hookIdle;
    hooks.serialNumber = &hookSerial;
    hooks.restart = &hookRestart;
    hooks.fatalError = &hookFatal;
    hooks.logLine = &hookLogLine;
    static knx::platform::CooperativePlatform platform(hooks);

    knx::physical::TpUartConfig tpuartConfig{};
    tpuartConfig.saveSignal.notifyPowerFailFromIsr = true;
    auto* backend = new Backend(h8i8o_usart1_hal(), platform, tpuartConfig);
    backend->setPowerFailIsrHandler(&onPowerFailIsr, nullptr);

    g_inputs.lastRaw = board_read_input_levels();
    for (unsigned ch = 0; ch < kChannels; ++ch) g_in[ch].setIndex(static_cast<uint8_t>(ch));

    auto bindings = makeCommissionedBindings(kProduct);
    bindChannels(bindings, std::make_integer_sequence<unsigned, kChannels>{});
    bindings.onParametersChanged([](params::ViewFor<kParameters> parameters) {
        loadConfig(parameters);
        rlog() << "parameters changed";
    });
    bindings.onLifecycleChanged([](DeviceLifecycleState state) {
        board_set_led(state == DeviceLifecycleState::Commissioning);
        rlog() << "lifecycle: " << lifecycleText(state);
    });
    CommissionedProductOptions options{};
    options.mediumBackend = std::unique_ptr<knx::physical::Tp1MediumBackend>(backend);
#if KNX_SECURE_ENABLED
    // A factory default: in force until ETS installs its own Tool Key, and
    // again after a factory reset. Security Mode stays ETS's to switch on.
    options.secure.factoryToolKey = g_fdsk;
#endif
    auto started = startCommissionedProduct(platform, kProduct, std::move(bindings), std::move(options));
    if (started.isError()) {
        rlog() << "start failed: " << static_cast<uint32_t>(started.error());
        // Outputs stay inactive; the watchdog is not running yet. Try again in 10 s.
        const uint32_t t0 = board_millis();
        while (board_millis() - t0 < 10000u) asm volatile("wfi");
        board_restart();
    }
    auto app = std::move(started.value());
    // The stack's own clock. Without it the coalesced configuration save never
    // falls due, and a change that ends without a restart (an individual
    // address, a property write) reaches flash only at the next restart; nor
    // do cyclic sending and the rate limit work.
    app->setTimeSource([]() -> uint32_t { return board_millis(); });
    loadConfig(app->parameters());
    const uint32_t now = board_millis();
    restoreOutputs(now);
    restoreCounters();
    reservePolicies(*app);
    g_policiesDue = false;
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
#if KNX_SECURE_ENABLED
    printCertificate(*app);
#endif
    g_startedAt = board_millis();
    backendOut = backend;
    return app;
}

// Sending starts once the device is operational and its start delay is over:
// then, once per start, the inputs that send after a start do, and "in
// operation" starts. Programming mode stops sending; leaving it resumes
// without repeating those.
bool g_startSendsDone = false;

template <typename App>
[[gnu::noinline]] void followSending(App& app, BusSink<App>& sink, uint32_t now) {
    const bool operational = app.lifecycleState() == DeviceLifecycleState::Operational;
    if (!operational) {
        g_sending = false;
        return;
    }
    if (g_sending || now - g_startedAt < static_cast<uint32_t>(g_config.startDelayS) * 1000u) return;
    g_sending = true;
    rlog() << "sending";
    if (g_startSendsDone) return;
    g_startSendsDone = true;
    for (unsigned ch = 0; ch < kChannels; ++ch) g_in[ch].sendAfterStart(((g_inputs.stable >> ch) & 1u) != 0u, sink);
    if (g_config.heartbeatS != 0u) (void)app.publish(kInOperationPort, true);
}

// Once a millisecond: bus power, the inputs, the channel logic, the button.
template <typename App>
[[gnu::noinline]] void serviceTick(App& app, const Backend& backend, BusSink<App>& sink, uint32_t now) {
    followBusPower(backend, app, now);
    followSending(app, sink, now);

    const uint8_t levels = board_read_input_levels();
    if (levels != g_inputs.lastRaw) {
        g_inputs.lastRaw = levels;
        rlog().bits(levels);  // raw pin levels, DI1 first
    }
    const uint8_t changed = g_inputs.update(readLogicalInputs(), now, g_config.debounceMs);
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        if ((changed & (1u << ch)) != 0u) {
            const bool active = (g_inputs.stable >> ch) & 1u;
            rlog() << "DI" << ch + 1u << (active ? " active" : " inactive");
            g_in[ch].edge(active, now, sink);
        }
        g_in[ch].tick(now, sink);
        g_out[ch].tick(now);
    }
    applyOutputs();

#if H8I8O_INPUT_COUNTER
    if (now - g_countersSavedAt >= kCounterSaveMs) {
        g_countersSavedAt = now;
        storeCounters("periodic");
    }
#endif

    followButton(app, now);
}

template <typename App>
[[gnu::noinline]] void publishStatus(App& app) {
    for (unsigned ch = 0; ch < kChannels; ++ch) {
        if ((g_statusDirty & (1u << ch)) == 0u) continue;
        if ((g_config.statusEnabled & (1u << ch)) == 0u) {
            g_statusDirty = static_cast<uint8_t>(g_statusDirty & ~(1u << ch));
            continue;
        }
        if (!app.publish(outPort(ch, OutSlot::T), g_out[ch].on()).isOk()) break;  // the send pipeline is full: next pass
        g_statusDirty = static_cast<uint8_t>(g_statusDirty & ~(1u << ch));
    }
}

// The RTT command channel (the commands are listed at the top of this file).
template <typename App>
[[gnu::noinline]] void handleCommand(App& app, const Backend& backend, int c) {
#ifdef CONFIG_H8I8O_BUS_TEST
    if (collectBurstCommand(c)) return;
#endif
    if (c == 'p') {
        app.toggleProgrammingMode();
    } else if (c == 's') {
        printBackend(backend);
    } else if (c == 'i') {
        Line line = rlog();
        line << "DI levels ";
        line.bits(board_read_input_levels());
        line << " button " << (board_button_pressed() ? 1u : 0u);
    } else if (c >= '1' && c <= '8') {
        const unsigned ch = static_cast<unsigned>(c - '1');
        g_out[ch].command(!g_out[ch].switchValue(), board_millis());
    } else if (c == 'a') {
        printAddress("address ", app.individualAddress().raw);
        {
            // PID_SERIAL_NUMBER as ETS reads it, not the board's number.
            Line line = rlog();
            line << "device serial ";
            for (const uint8_t octet : app.deviceObject().getSerialNumber()) {
                line.hex(octet, 2);
            }
        }
#if H8I8O_TRACE
        rlog() << "lifecycle: " << lifecycleText(app.lifecycleState()) << ", trace dropped "
               << h8i8o_usart1_trace_dropped();
#else
        rlog() << "lifecycle: " << lifecycleText(app.lifecycleState());
#endif
#if KNX_SECURE_ENABLED
    } else if (c == 'k') {
        printCertificate(app);
#endif
    } else if (c == 'R') {
        restartDevice("R");
    }
}

}  // namespace

int main() {
    Backend* backend = nullptr;
    auto app = startDevice(backend);
    using App = std::remove_reference_t<decltype(*app)>;
    BusSink<App> sink(*app);
    board_watchdog_start();

    uint32_t lastTick = board_millis();
    uint16_t lastAddress = app->individualAddress().raw;
    for (;;) {
        app->loop();
        (void)g_flashLog.service();
#if H8I8O_TRACE
        printTrace();
#endif
        if (const uint16_t address = app->individualAddress().raw; address != lastAddress) {
            lastAddress = address;
            printAddress("address now ", address);
        }
#ifdef CONFIG_H8I8O_BUS_TEST
        stepBurst(*app);
#endif
        if (g_policiesDue) {
            g_policiesDue = false;
            applyPolicies(*app);
        }
        learnScenes(*app);

        if (const uint32_t now = board_millis(); now != lastTick) {
            lastTick = now;
            serviceTick(*app, *backend, sink, now);
        }
        if (g_statusDirty != 0u && g_sending) {
            publishStatus(*app);
        }
        if (const int c = rtt_read_char(); c >= 0) {
            handleCommand(*app, *backend, c);
        }

        board_watchdog_feed();
        asm volatile("wfi");
    }
}
