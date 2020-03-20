#include <iostream>
#include <sstream>

#include "timer.h"

using namespace Timer;

struct info Timer::start(const std::string &what) {
    struct info info {
        what
    };
    std::cerr << "\n-------------- Starting " << info.what << "\n";
    info.time = std::chrono::high_resolution_clock::now();
    return info;
}

std::string Timer::report(const struct info &info) {
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto ms = std::chrono::duration<double, std::milli>(end_time - info.time).count();
    std::stringstream report;
    report << info.what << ": " << ms << "ms";
    std::cerr << "-------------- Finished " << report.str() << "\n";
    return report.str();
}
