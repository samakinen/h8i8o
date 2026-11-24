// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file debounce.hpp
 * @brief Debounce of the eight inputs and of the programming button
 */

#pragma once

#include "logic/functions.hpp"

#include <cstdint>

namespace h8i8o {

/// One bit: a change counts once the level has been stable for the hold time.
struct Debounce {
    uint8_t stable{0};
    uint8_t candidate{0};
    uint32_t since{0};

    /// Returns the bits that changed their stable value.
    uint8_t update(uint8_t raw, uint32_t now, uint32_t holdMs)
    {
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

/// Eight inputs, each with its own debounce time.
struct InputDebounce {
    uint8_t stable{0};
    uint8_t candidate{0};
    uint8_t lastRaw{0};
    uint32_t since[kChannels]{};

    /// Returns the bits that changed their stable value.
    uint8_t update(uint8_t raw, uint32_t now, const uint8_t* holdMs)
    {
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

}  // namespace h8i8o
