#pragma once

#include <cstdio>
#include <string>

// Minimal test support: a check has a text, a case counts its checks, and the program gives
// status 0 only when no check failed. No test framework.

namespace soulwarden::backend::tests {

struct Counters {
    int checks{0};
    int failures{0};
};

inline Counters& counters() {
    static Counters value;
    return value;
}

/** Records one check of the current case and prints its line. */
inline void check(bool condition, const std::string& text) {
    ++counters().checks;
    if (!condition) {
        ++counters().failures;
    }
    std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", text.c_str());
}

/** Prints one line of data that is not a check. */
inline void note(const std::string& text) {
    std::printf("        %s\n", text.c_str());
}

} // namespace soulwarden::backend::tests
