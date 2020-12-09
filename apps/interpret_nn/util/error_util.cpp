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

const char *const severity_names[] = {"INFO", "WARNING", "ERROR", "FATAL"};

#if defined(__ANDROID__)
int const android_severity[] = {ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR, ANDROID_LOG_FATAL};
#endif

}  // namespace

Logger::Logger(LogSeverity severity, const char *file, int line)
    : severity(severity) {
    assert(severity >= 0 && severity <= 3);
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
    if (severity == FATAL) {
        std::abort();
    }
}

Checker::Checker(const char *condition_string)
    : logger(FATAL) {
    logger.msg << " Condition Failed: " << condition_string << '\n';
}

Checker::Checker(const char *file, int line, const char *condition_string)
    : logger(FATAL, file, line) {
    logger.msg << " Condition Failed: " << condition_string << '\n';
}

}  // namespace internal
}  // namespace interpret_nn
