// SPDX-License-Identifier: GPL-3.0-or-later
// Timelines for the output functions (docs/design/channel-functions.md §5):
// commands and milliseconds in, the output and its status out.

#include "check.hpp"

#include "logic/output_channel.hpp"

using namespace h8i8o;

namespace {

struct Rig {
    OutputChannel ch;
    uint32_t now{1000};

    explicit Rig(const OutputConfig& config) { ch.configure(config, now); (void)ch.takeStatus(); }
    void run(uint32_t ms)
    {
        for (uint32_t i = 0; i < ms; ++i) {
            ++now;
            ch.tick(now);
        }
    }
    /// Runs until the output changes (or the limit); returns the time it took.
    uint32_t untilChange(uint32_t limit)
    {
        const bool start = ch.on();
        for (uint32_t i = 1; i <= limit; ++i) {
            ++now;
            ch.tick(now);
            if (ch.on() != start) return i;
        }
        return 0;
    }
};

// The ETS defaults (app/product.hpp).
OutputConfig function(OutputFunction f)
{
    OutputConfig c{};
    c.function = f;
    c.staircaseS = 120;
    c.retrigger = true;
    c.offWithZero = true;
    c.pulse100ms = 10;
    c.cycleMin = 10;
    c.safetyPercent = 30;
    return c;
}

}  // namespace

// ── Switch ───────────────────────────────────────────────────────────────────

TEST(switch_without_delays_follows_the_object)
{
    Rig r(function(OutputFunction::Switch));
    r.ch.command(true, r.now);
    CHECK(r.ch.on());
    CHECK(r.ch.takeStatus());
    r.ch.command(false, r.now);
    CHECK(!r.ch.on());
}

TEST(status_is_due_after_every_command_even_without_a_change)
{
    Rig r(function(OutputFunction::Switch));
    r.ch.command(false, r.now);
    CHECK(r.ch.takeStatus());
    CHECK(!r.ch.takeStatus());
}

TEST(on_and_off_delays)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.onDelayS = 2;
    c.offDelayS = 3;
    Rig r(c);
    r.ch.command(true, r.now);
    CHECK_EQ(r.untilChange(10000), 2000u);
    CHECK(r.ch.takeStatus());
    r.ch.command(false, r.now);
    CHECK_EQ(r.untilChange(10000), 3000u);
}

TEST(an_opposite_command_cancels_a_pending_delay)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.onDelayS = 2;
    Rig r(c);
    r.ch.command(true, r.now);
    r.run(1000);
    r.ch.command(false, r.now);
    r.run(5000);
    CHECK(!r.ch.on());
}

TEST(a_repeated_command_keeps_its_deadline)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.onDelayS = 2;
    Rig r(c);
    r.ch.command(true, r.now);
    r.run(1500);
    r.ch.command(true, r.now);
    CHECK_EQ(r.untilChange(10000), 500u);
}

TEST(disabled_ignores_everything)
{
    OutputConfig c = function(OutputFunction::Disabled);
    c.lockEnabled = true;
    c.lockStart = LockAction::On;
    Rig r(c);
    r.ch.command(true, r.now);
    r.ch.lock(true, r.now);
    r.ch.forced(true, true, r.now);
    CHECK(!r.ch.on());
}

// ── Staircase ────────────────────────────────────────────────────────────────

TEST(staircase_goes_off_after_its_time)
{
    OutputConfig c = function(OutputFunction::Staircase);
    c.staircaseS = 60;
    Rig r(c);
    r.ch.command(true, r.now);
    CHECK(r.ch.on());
    CHECK_EQ(r.untilChange(100000), 60000u);
}

TEST(staircase_retriggers_or_not)
{
    OutputConfig c = function(OutputFunction::Staircase);
    c.staircaseS = 10;
    Rig again(c);
    again.ch.command(true, again.now);
    again.run(8000);
    again.ch.command(true, again.now);
    CHECK_EQ(again.untilChange(100000), 10000u);

    c.retrigger = false;
    Rig once(c);
    once.ch.command(true, once.now);
    once.run(8000);
    once.ch.command(true, once.now);
    CHECK_EQ(once.untilChange(100000), 2000u);
}

TEST(staircase_off_with_zero_or_not)
{
    OutputConfig c = function(OutputFunction::Staircase);
    c.staircaseS = 10;
    Rig off(c);
    off.ch.command(true, off.now);
    off.ch.command(false, off.now);
    CHECK(!off.ch.on());

    c.offWithZero = false;
    Rig on(c);
    on.ch.command(true, on.now);
    on.ch.command(false, on.now);
    CHECK(on.ch.on());
}

TEST(staircase_warning_blinks_off_for_a_second)
{
    OutputConfig c = function(OutputFunction::Staircase);
    c.staircaseS = 60;
    c.warningS = 10;
    Rig r(c);
    r.ch.command(true, r.now);
    CHECK_EQ(r.untilChange(100000), 50000u);  // off at 10 s before the end
    CHECK(r.ch.takeStatus());
    CHECK_EQ(r.untilChange(100000), 1000u);   // on again for the rest
    CHECK(r.ch.on());
    CHECK_EQ(r.untilChange(100000), 9000u);   // and off at the end
}

// ── Pulse ────────────────────────────────────────────────────────────────────

TEST(pulse_is_on_for_its_length_and_ignores_off)
{
    OutputConfig c = function(OutputFunction::Pulse);
    c.pulse100ms = 5;
    Rig r(c);
    r.ch.command(true, r.now);
    r.ch.command(false, r.now);
    CHECK(r.ch.on());
    CHECK_EQ(r.untilChange(10000), 500u);
}

// ── Lock and forced operation ────────────────────────────────────────────────

TEST(lock_start_and_end_actions)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.lockEnabled = true;
    c.lockStart = LockAction::On;
    c.lockEnd = LockAction::Off;
    Rig r(c);
    r.ch.lock(true, r.now);
    CHECK(r.ch.on());
    r.ch.command(false, r.now);  // ignored while locked
    CHECK(r.ch.on());
    r.ch.lock(false, r.now);
    CHECK(!r.ch.on());
}

TEST(lock_end_last_applies_what_was_commanded_meanwhile)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.lockEnabled = true;
    c.lockStart = LockAction::Off;
    c.lockEnd = LockAction::Last;
    Rig r(c);
    r.ch.lock(true, r.now);
    r.ch.command(true, r.now);
    CHECK(!r.ch.on());
    r.ch.lock(false, r.now);
    CHECK(r.ch.on());
}

TEST(lock_unchanged_keeps_the_output_as_it_was)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.lockEnabled = true;
    Rig r(c);
    r.ch.command(true, r.now);
    r.ch.lock(true, r.now);
    r.ch.command(false, r.now);
    CHECK(r.ch.on());
    r.ch.lock(false, r.now);
    CHECK(r.ch.on());
    CHECK(r.ch.switchValue());
}

TEST(the_lock_object_is_ignored_without_a_lock)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.lockStart = LockAction::On;
    Rig r(c);
    r.ch.lock(true, r.now);
    CHECK(!r.ch.on());
}

TEST(forced_operation_beats_the_lock_and_returns_to_it)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.lockEnabled = true;
    c.lockStart = LockAction::Off;
    Rig r(c);
    r.ch.lock(true, r.now);
    r.ch.forced(true, true, r.now);
    CHECK(r.ch.on());
    r.ch.forced(false, false, r.now);
    CHECK(!r.ch.on());
}

TEST(forced_off_then_end_returns_to_the_switch_value)
{
    Rig r(function(OutputFunction::Switch));
    r.ch.command(true, r.now);
    r.ch.forced(true, false, r.now);
    CHECK(!r.ch.on());
    r.ch.command(true, r.now);
    CHECK(!r.ch.on());
    r.ch.forced(false, false, r.now);
    CHECK(r.ch.on());
}

TEST(a_staircase_keeps_running_under_a_lock)
{
    OutputConfig c = function(OutputFunction::Staircase);
    c.staircaseS = 10;
    c.lockEnabled = true;
    c.lockStart = LockAction::On;
    c.lockEnd = LockAction::Last;
    Rig r(c);
    r.ch.command(true, r.now);
    r.ch.lock(true, r.now);
    r.run(20000);
    CHECK(r.ch.on());
    r.ch.lock(false, r.now);
    CHECK(!r.ch.on());
}

// ── Scenes ───────────────────────────────────────────────────────────────────

TEST(a_scene_recall_applies_its_action)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.sceneNumber[1] = 7;
    c.sceneOn[1] = true;
    c.sceneNumber[2] = 8;
    c.sceneOn[2] = false;
    Rig r(c);
    CHECK_EQ(r.ch.scene(6, r.now), 0);  // scene 7
    CHECK(r.ch.on());
    CHECK_EQ(r.ch.scene(7, r.now), 0);  // scene 8
    CHECK(!r.ch.on());
    r.ch.scene(20, r.now);  // not ours
    CHECK(!r.ch.on());
}

TEST(a_scene_store_asks_for_the_matching_slots)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.sceneNumber[0] = 3;
    c.sceneNumber[3] = 3;
    Rig r(c);
    CHECK_EQ(r.ch.scene(0x80 | 2, r.now), 0x09);
    CHECK_EQ(r.ch.scene(0x80 | 9, r.now), 0);
}

TEST(scene_slot_zero_is_unused)
{
    Rig r(function(OutputFunction::Switch));
    CHECK_EQ(r.ch.scene(0x80, r.now), 0);  // scene 1 matches no slot set to 0
}

// ── Valve ────────────────────────────────────────────────────────────────────

TEST(valve_pulse_width_follows_the_control_value)
{
    OutputConfig c = function(OutputFunction::Valve);
    c.cycleMin = 1;
    Rig r(c);
    r.ch.controlValue(128, r.now);  // about 50 %
    CHECK(r.ch.on());
    const uint32_t onMs = r.untilChange(120000);
    CHECK(onMs > 29000u && onMs < 31000u);
    const uint32_t offMs = r.untilChange(120000);
    CHECK_EQ(onMs + offMs, 60000u);
}

TEST(valve_stays_shut_or_open_at_the_ends)
{
    OutputConfig c = function(OutputFunction::Valve);
    c.cycleMin = 1;
    Rig shut(c);
    shut.ch.controlValue(3, shut.now);
    CHECK(!shut.ch.on());
    CHECK_EQ(shut.untilChange(200000), 0u);
    Rig open(c);
    open.ch.controlValue(252, open.now);
    CHECK(open.ch.on());
    CHECK_EQ(open.untilChange(200000), 0u);
}

TEST(valve_falls_back_to_the_safety_value)
{
    OutputConfig c = function(OutputFunction::Valve);
    c.cycleMin = 1;
    c.monitorMin = 2;
    c.safetyPercent = 100;
    Rig r(c);
    r.ch.controlValue(0, r.now);
    CHECK(!r.ch.on());
    CHECK_EQ(r.untilChange(200000), 120000u);
    CHECK(r.ch.on());
    r.ch.controlValue(0, r.now);  // a value arrives: monitoring starts again
    CHECK(!r.ch.on());
}

TEST(valve_ignores_switch_commands_and_scenes)
{
    OutputConfig c = function(OutputFunction::Valve);
    c.sceneNumber[0] = 1;
    c.sceneOn[0] = true;
    Rig r(c);
    r.ch.command(true, r.now);
    r.ch.scene(0, r.now);
    CHECK(!r.ch.on());
}

// ── Reconfiguration, wrap ────────────────────────────────────────────────────

TEST(reconfiguring_with_the_same_function_keeps_the_state)
{
    OutputConfig c = function(OutputFunction::Switch);
    Rig r(c);
    r.ch.command(true, r.now);
    c.offDelayS = 5;
    r.ch.configure(c, r.now);
    CHECK(r.ch.on());
}

TEST(a_new_function_starts_off)
{
    Rig r(function(OutputFunction::Switch));
    r.ch.command(true, r.now);
    r.ch.configure(function(OutputFunction::Staircase), r.now);
    CHECK(!r.ch.on());
}

TEST(delays_work_across_the_millisecond_wrap)
{
    OutputConfig c = function(OutputFunction::Switch);
    c.onDelayS = 1;
    Rig r(c);
    r.now = 0xFFFFFE00u;
    r.ch.command(true, r.now);
    CHECK_EQ(r.untilChange(5000), 1000u);
}

int main() { return h8i8o::test::run(); }
