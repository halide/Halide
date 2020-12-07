#include "util/error_util.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace interpret_nn {
namespace internal {

namespace {

const char *const severity_names[] = {"INFO", "WARNING", "ERROR"};

#if defined(__ANDROID__)
int const android_severity[] = {ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR};
#endif

}  // namespace

Logger::Logger(LogSeverity severity, const char *file, int line)
    : severity(severity) {
    assert(severity >= 0 && severity <= 2);
    msg << severity_names[(int)severity] << ": "
        << "(" << file << ":" << line << ") ";
}

Logger::Logger(LogSeverity severity)
    : severity(severity) {
    assert(severity >= 0 && severity <= 2);
    msg << severity_names[(int)severity] << ": ";
}

Logger::~Logger() noexcept(false) {
    if (!msg.str().empty() && msg.str().back() != '\n') {
        msg << '\n';
    }

    std::cerr << msg.str();

#if defined(__ANDROID__)
    __android_log_write(android_severity[(int)severity], "interpret_nn", msg.str().c_str());
#endif

    // TODO: call iOS-specific logger here?
}

CheckLogger::CheckLogger(LogSeverity severity, const char *condition_string)
    : Logger(severity) {
    msg << " Condition Failed: " << condition_string << '\n';
}

CheckLogger::CheckLogger(LogSeverity severity, const char *file, int line, const char *condition_string)
    : Logger(severity, file, line) {
    msg << " Condition Failed: " << condition_string << '\n';
}

CheckLogger::~CheckLogger() noexcept(false) {
    std::abort();
}

}  // namespace internal
}  // namespace interpret_nn
