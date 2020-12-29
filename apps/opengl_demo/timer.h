#ifndef _TIMER_H_
#define _TIMER_H_

#include <chrono>

namespace Timer {
struct info {
    const std::string what;
    std::chrono::time_point<std::chrono::high_resolution_clock> time;
};

struct info start(const std::string &what);
std::string report(const struct info &);
}  // namespace Timer

#endif
