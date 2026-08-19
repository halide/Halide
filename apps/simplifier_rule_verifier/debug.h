#ifndef SIMPLIFIER_RULE_VERIFIER_DEBUG_H
#define SIMPLIFIER_RULE_VERIFIER_DEBUG_H

#include <cstdlib>
#include <iostream>
#include <utility>

// A stand-in for Halide's internal debug stream, which isn't part of the
// public API. Messages at a level above the value of the HL_DEBUG_RULE_VERIFIER
// environment variable are dropped.
class debug {
    const bool enabled;

    static int verbosity() {
        static const int level = []() {
            const char *s = getenv("HL_DEBUG_RULE_VERIFIER");
            return s ? atoi(s) : 0;
        }();
        return level;
    }

public:
    explicit debug(int level)
        : enabled(level <= verbosity()) {
    }

    template<typename T>
    debug &operator<<(T &&x) {
        if (enabled) {
            std::cerr << std::forward<T>(x);
        }
        return *this;
    }
};

#endif
