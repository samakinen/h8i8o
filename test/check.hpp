// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file check.hpp
 * @brief A minimal test runner for the host tests of app/logic
 *
 *   TEST(name) { CHECK(a == b); CHECK_EQ(a, b); }
 *   int main() { return h8i8o::test::run(); }
 */

#pragma once

#include <cstdio>
#include <functional>
#include <vector>

namespace h8i8o::test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& cases()
{
    static std::vector<Case> list;
    return list;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
};

inline int run()
{
    int failedCases = 0;
    for (const auto& c : cases()) {
        const int before = failures();
        c.fn();
        const bool ok = failures() == before;
        failedCases += ok ? 0 : 1;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", c.name);
    }
    std::printf("%zu tests, %d failed\n", cases().size(), failedCases);
    return failedCases == 0 ? 0 : 1;
}

}  // namespace h8i8o::test

#define TEST(name)                                                              \
    static void name();                                                         \
    static ::h8i8o::test::Register name##_registration(#name, &name);           \
    static void name()

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            ++::h8i8o::test::failures();                                        \
        }                                                                       \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        const auto check_a_ = (a);                                              \
        const auto check_b_ = (b);                                              \
        if (!(check_a_ == check_b_)) {                                          \
            std::printf("  %s:%d: CHECK_EQ(%s, %s) failed: %lld != %lld\n", __FILE__, __LINE__, #a, #b, \
                        static_cast<long long>(check_a_), static_cast<long long>(check_b_)); \
            ++::h8i8o::test::failures();                                        \
        }                                                                       \
    } while (0)
