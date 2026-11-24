// SPDX-License-Identifier: GPL-3.0-or-later
// Timelines for the input functions (docs/design/channel-functions.md §4):
// contact edges and milliseconds in, telegrams out.

#include "check.hpp"

#include "logic/input_channel.hpp"

#include <vector>

using namespace h8i8o;

namespace {

struct Sent {
    InObject object;
    uint32_t value;
    bool operator==(const Sent&) const = default;
};

struct Recorder : InputSink {
    std::vector<Sent> sent;
    uint8_t lastChannel{0xFF};
    void send(uint8_t channel, InObject object, uint32_t value) override
    {
        lastChannel = channel;
        sent.push_back({object, value});
    }
    void clear() { sent.clear(); }
};

// A channel on a timeline: press for `ms`, then release; time runs on.
struct Rig {
    InputChannel ch;
    Recorder out;
    uint32_t now{1000};

    explicit Rig(const InputConfig& config, uint8_t index = 3)
    {
        ch.setIndex(index);
        ch.configure(config);
    }
    void run(uint32_t ms)
    {
        for (uint32_t i = 0; i < ms; ++i) {
            ++now;
            ch.tick(now, out);
        }
    }
    void close() { ch.edge(true, now, out); }
    void open() { ch.edge(false, now, out); }
    void press(uint32_t ms)
    {
        close();
        run(ms);
        open();
        run(10);
    }
};

// The ETS defaults (app/product.hpp), with On for short and long press.
InputConfig function(InputFunction f)
{
    InputConfig c{};
    c.function = f;
    c.onClose = EdgeAction::On;
    c.onOpen = EdgeAction::Off;
    c.shortAction = EdgeAction::On;
    c.longAction = EdgeAction::On;
    c.longMs = 500;
    c.scene = 1;
    c.pulsesPerUnit = 1;
    c.sendDelta = 1;
    return c;
}

}  // namespace

// ── Switch ───────────────────────────────────────────────────────────────────

TEST(switch_defaults_send_the_contact_state_as_before)
{
    Rig r(function(InputFunction::Switch));
    r.close();
    r.open();
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 1}, {InObject::A, 0}}));
    CHECK_EQ(r.out.lastChannel, 3);
}

TEST(switch_toggle_on_closing_and_nothing_on_opening_is_a_toggle_button)
{
    InputConfig c = function(InputFunction::Switch);
    c.onClose = EdgeAction::Toggle;
    c.onOpen = EdgeAction::Nothing;
    Rig r(c);
    r.press(50);
    r.press(50);
    r.press(50);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 1}, {InObject::A, 0}, {InObject::A, 1}}));
}

TEST(switch_toggle_follows_a_written_a)
{
    InputConfig c = function(InputFunction::Switch);
    c.onClose = EdgeAction::Toggle;
    c.onOpen = EdgeAction::Nothing;
    Rig r(c);
    r.ch.writeA(true);  // the light was switched on elsewhere
    r.press(50);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 0}}));
}

TEST(switch_off_on_closing_inverts)
{
    InputConfig c = function(InputFunction::Switch);
    c.onClose = EdgeAction::Off;
    c.onOpen = EdgeAction::On;
    Rig r(c);
    r.close();
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 0}}));
}

TEST(disabled_sends_nothing)
{
    Rig r(function(InputFunction::Disabled));
    r.press(2000);
    CHECK(r.out.sent.empty());
}

// ── Lock ─────────────────────────────────────────────────────────────────────

TEST(a_locked_input_sends_nothing_and_unlocking_sends_nothing)
{
    InputConfig c = function(InputFunction::Switch);
    c.lockEnabled = true;
    Rig r(c);
    r.ch.writeLock(true);
    r.press(50);
    CHECK(r.out.sent.empty());
    r.ch.writeLock(false);
    CHECK(r.out.sent.empty());
    r.close();
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 1}}));
}

TEST(the_lock_object_is_ignored_without_a_lock)
{
    Rig r(function(InputFunction::Switch));
    r.ch.writeLock(true);
    r.close();
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 1}}));
}

TEST(locking_drops_a_press_in_progress)
{
    InputConfig c = function(InputFunction::ShortLong);
    c.lockEnabled = true;
    Rig r(c);
    r.close();
    r.run(100);
    r.ch.writeLock(true);
    r.ch.writeLock(false);
    r.open();
    r.run(1000);
    CHECK(r.out.sent.empty());
}

// ── Short/long press ─────────────────────────────────────────────────────────

TEST(short_press_sends_on_release_long_press_when_the_time_is_up)
{
    InputConfig c = function(InputFunction::ShortLong);
    c.shortAction = EdgeAction::On;
    c.longAction = EdgeAction::Off;
    c.longMs = 500;
    Rig r(c);
    r.close();
    r.run(499);
    CHECK(r.out.sent.empty());
    r.open();
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 1}}));
    r.out.clear();
    r.close();
    r.run(500);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::B, 0}}));
    r.open();  // the release after a long press sends nothing
    r.run(1000);
    CHECK_EQ(r.out.sent.size(), 1u);
}

TEST(short_and_long_toggle_their_own_objects)
{
    InputConfig c = function(InputFunction::ShortLong);
    c.shortAction = EdgeAction::Toggle;
    c.longAction = EdgeAction::Toggle;
    Rig r(c);
    r.press(100);
    r.press(600);
    r.press(100);
    r.press(600);
    CHECK(r.out.sent
          == (std::vector<Sent>{{InObject::A, 1}, {InObject::B, 1}, {InObject::A, 0}, {InObject::B, 0}}));
}

// ── Dimming ──────────────────────────────────────────────────────────────────

TEST(one_button_dimming_toggles_on_short_and_dims_on_long)
{
    Rig r(function(InputFunction::Dimming));
    r.press(100);  // on
    r.press(800);  // the light is on: the first dim goes down
    r.press(800);  // then up
    CHECK(r.out.sent
          == (std::vector<Sent>{{InObject::A, 1}, {InObject::C, 0x1}, {InObject::C, 0x0},
                                {InObject::C, 0x9}, {InObject::C, 0x8}}));
}

TEST(one_button_dimming_goes_up_while_the_light_is_off)
{
    Rig r(function(InputFunction::Dimming));
    r.press(800);
    r.press(800);
    CHECK(r.out.sent
          == (std::vector<Sent>{{InObject::C, 0x9}, {InObject::C, 0x8}, {InObject::C, 0x9}, {InObject::C, 0x8}}));
}

TEST(two_button_dimming_has_fixed_directions)
{
    InputConfig c = function(InputFunction::Dimming);
    c.dimDirection = Direction::Down;
    Rig r(c);
    r.press(100);
    r.press(800);
    r.press(800);
    CHECK(r.out.sent
          == (std::vector<Sent>{{InObject::A, 0}, {InObject::C, 0x1}, {InObject::C, 0x0},
                                {InObject::C, 0x1}, {InObject::C, 0x0}}));
}

// ── Shutter ──────────────────────────────────────────────────────────────────

TEST(one_button_shutter_alternates_and_stops_in_the_last_direction)
{
    Rig r(function(InputFunction::Shutter));
    r.press(800);  // up
    r.press(100);  // stop (step up)
    r.press(800);  // down
    r.press(100);  // stop (step down)
    CHECK(r.out.sent
          == (std::vector<Sent>{{InObject::A, 0}, {InObject::B, 0}, {InObject::A, 1}, {InObject::B, 1}}));
}

TEST(two_button_shutter_has_fixed_directions)
{
    InputConfig c = function(InputFunction::Shutter);
    c.shutterDirection = Direction::Down;
    Rig r(c);
    r.press(800);
    r.press(100);
    r.press(800);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 1}, {InObject::B, 1}, {InObject::A, 1}}));
}

// ── Value and scene ──────────────────────────────────────────────────────────

TEST(value_is_sent_on_closing_only)
{
    InputConfig c = function(InputFunction::Value);
    c.value = 200;
    Rig r(c);
    r.press(100);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::D, 200}}));
}

TEST(percentage_scales_and_clamps)
{
    InputConfig c = function(InputFunction::Percent);
    c.value = 50;
    Rig percent(c);
    percent.close();
    c.value = 150;  // more than 100 % is 100 %
    Rig full(c);
    full.close();
    CHECK(percent.out.sent == (std::vector<Sent>{{InObject::D, 128}}));
    CHECK(full.out.sent == (std::vector<Sent>{{InObject::D, 255}}));
    CHECK(percent.ch.config().function == InputFunction::Value);
}

TEST(contact_functions_behave_as_switch_and_energy_as_counter)
{
    for (const auto f : {InputFunction::Alarm, InputFunction::WindowDoor, InputFunction::Occupancy,
                         InputFunction::OpenClose}) {
        Rig r(function(f));
        r.close();
        r.open();
        CHECK(r.out.sent == (std::vector<Sent>{{InObject::A, 1}, {InObject::A, 0}}));
    }
    Rig energy(function(InputFunction::EnergyCounter));
    energy.press(40);
    CHECK(energy.out.sent == (std::vector<Sent>{{InObject::E, 1}}));
}

TEST(scene_numbers_out_of_range_are_clamped)
{
    InputConfig c = function(InputFunction::Scene);
    c.scene = 0;
    Rig low(c);
    low.press(100);
    c.scene = 99;
    Rig high(c);
    high.press(100);
    CHECK(low.out.sent == (std::vector<Sent>{{InObject::D, 0}}));
    CHECK(high.out.sent == (std::vector<Sent>{{InObject::D, 63}}));
}

TEST(scene_recalls_on_short_and_stores_on_long)
{
    InputConfig c = function(InputFunction::Scene);
    c.scene = 5;
    Rig r(c);
    r.press(100);
    r.press(800);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::D, 4}, {InObject::D, 0x84}}));
}

// ── Counter ──────────────────────────────────────────────────────────────────

TEST(counter_counts_closings_and_sends_after_the_delta)
{
    InputConfig c = function(InputFunction::Counter);
    c.pulsesPerUnit = 2;
    c.sendDelta = 3;
    Rig r(c);
    for (int i = 0; i < 12; ++i) r.press(40);
    // 12 pulses = 6 units: sent at 3 and 6.
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::E, 3}, {InObject::E, 6}}));
    CHECK_EQ(r.ch.pulses(), 12u);
}

TEST(a_locked_counter_counts_but_does_not_send)
{
    InputConfig c = function(InputFunction::Counter);
    c.lockEnabled = true;
    Rig r(c);
    r.ch.writeLock(true);
    for (int i = 0; i < 5; ++i) r.press(40);
    CHECK(r.out.sent.empty());
    r.ch.writeLock(false);
    r.press(40);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::E, 6}}));
}

TEST(writing_e_sets_the_count_in_units)
{
    InputConfig c = function(InputFunction::Counter);
    c.pulsesPerUnit = 10;
    c.sendDelta = 1;
    Rig r(c);
    r.ch.writeCount(1000);
    CHECK_EQ(r.ch.pulses(), 10000u);
    for (int i = 0; i < 10; ++i) r.press(40);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::E, 1001}}));
}

TEST(a_restored_count_continues)
{
    InputConfig c = function(InputFunction::Counter);
    Rig r(c);
    r.ch.restorePulses(41);
    r.press(40);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::E, 42}}));
}

// ── After start, reconfiguration ─────────────────────────────────────────────

TEST(send_after_start_sends_the_level_of_a_switch)
{
    InputConfig c = function(InputFunction::Switch);
    c.sendAfterStart = true;
    Rig closed(c);
    closed.ch.sendAfterStart(true, closed.out);
    Rig open(c);
    open.ch.sendAfterStart(false, open.out);
    CHECK(closed.out.sent == (std::vector<Sent>{{InObject::A, 1}}));
    CHECK(open.out.sent == (std::vector<Sent>{{InObject::A, 0}}));
}

TEST(send_after_start_skips_toggles_and_disabled_option)
{
    InputConfig c = function(InputFunction::Switch);
    c.sendAfterStart = true;
    c.onClose = EdgeAction::Toggle;
    Rig toggle(c);
    toggle.ch.sendAfterStart(true, toggle.out);
    c.sendAfterStart = false;
    c.onClose = EdgeAction::On;
    Rig off(c);
    off.ch.sendAfterStart(true, off.out);
    CHECK(toggle.out.sent.empty());
    CHECK(off.out.sent.empty());
}

TEST(send_after_start_sends_a_counter)
{
    InputConfig c = function(InputFunction::Counter);
    c.sendAfterStart = true;
    Rig r(c);
    r.ch.restorePulses(7);
    r.ch.sendAfterStart(false, r.out);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::E, 7}}));
}

TEST(reconfiguring_keeps_a_and_the_count)
{
    InputConfig c = function(InputFunction::Counter);
    Rig r(c);
    r.ch.restorePulses(5);
    r.ch.writeA(true);
    r.ch.configure(c);
    CHECK_EQ(r.ch.pulses(), 5u);
    CHECK(r.ch.a());
}

TEST(a_function_change_drops_a_press_in_progress)
{
    Rig r(function(InputFunction::ShortLong));
    r.close();
    r.run(100);
    r.ch.configure(function(InputFunction::Scene));
    r.open();
    CHECK(r.out.sent.empty());
}

TEST(long_press_works_across_the_millisecond_wrap)
{
    Rig r(function(InputFunction::ShortLong));
    r.now = 0xFFFFFF00u;
    r.close();
    r.run(600);
    CHECK(r.out.sent == (std::vector<Sent>{{InObject::B, 1}}));
}

int main() { return h8i8o::test::run(); }
