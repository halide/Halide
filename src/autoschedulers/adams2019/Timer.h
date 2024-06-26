#ifndef HL_TIMER_H
#define HL_TIMER_H

#include <chrono>
#include <set>
#include <string>
#include <vector>

#include "ASLog.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

using Clock = std::chrono::high_resolution_clock;

struct ScopedTimer {
    std::chrono::time_point<Clock> start = Clock::now();
    std::string msg;

    explicit ScopedTimer(const std::string &msg)
        : msg{msg} {
        aslog(0) << "Start: " << msg << "\n";
    }

    ~ScopedTimer() {
        auto duration = Clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        aslog(0) << "Duration (ms): " << msg << " = " << ms << "\n";
    }
};

struct Timer {
    std::chrono::time_point<Clock> start = Clock::now();

    Timer() = default;

    void restart() {
        start = Clock::now();
    }

    std::chrono::duration<double> elapsed() const {
        return Clock::now() - start;
    }
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // HL_TIMER_H
