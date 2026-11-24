// SPDX-License-Identifier: GPL-3.0-or-later
// Debounce of the inputs: a change counts once it has been stable for the
// input's own time.

#include "check.hpp"

#include "logic/debounce.hpp"

using namespace h8i8o;

TEST(a_change_counts_after_the_hold_time)
{
    InputDebounce d;
    const uint8_t hold[kChannels] = {30, 30, 30, 30, 30, 30, 30, 30};
    uint32_t now = 100;
    CHECK_EQ(d.update(0x01, now, hold), 0);
    for (int i = 0; i < 29; ++i) CHECK_EQ(d.update(0x01, ++now, hold), 0);
    CHECK_EQ(d.update(0x01, ++now, hold), 0x01);
    CHECK_EQ(d.stable, 0x01);
}

TEST(a_bounce_restarts_the_hold_time)
{
    InputDebounce d;
    const uint8_t hold[kChannels] = {10, 10, 10, 10, 10, 10, 10, 10};
    uint32_t now = 0;
    d.update(0x01, now, hold);
    now += 5;
    d.update(0x00, now, hold);  // bounce
    now += 1;
    d.update(0x01, now, hold);
    now += 9;
    CHECK_EQ(d.update(0x01, now, hold), 0);
    now += 1;
    CHECK_EQ(d.update(0x01, now, hold), 0x01);
}

TEST(each_input_has_its_own_time)
{
    InputDebounce d;
    const uint8_t hold[kChannels] = {10, 200, 10, 10, 10, 10, 10, 10};
    uint32_t now = 0;
    d.update(0x03, now, hold);
    now += 10;
    CHECK_EQ(d.update(0x03, now, hold), 0x01);
    now += 190;
    CHECK_EQ(d.update(0x03, now, hold), 0x02);
}

TEST(button_debounce)
{
    Debounce b;
    CHECK_EQ(b.update(1, 0, 50), 0);
    CHECK_EQ(b.update(1, 49, 50), 0);
    CHECK_EQ(b.update(1, 50, 50), 1);
}

int main() { return h8i8o::test::run(); }
