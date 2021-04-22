#include "util/error_util.h"

#include <cstdlib>

namespace hannk {
namespace internal {

namespace {

const char *const severity_names[] = {"INFO", "WARNING", "ERROR", "FATAL"};

}  // namespace

Logger::Logger(LogSeverity severity, const char *file, int line)
    : severity(severity) {
    assert(severity >= 0 && (size_t)severity < size(severity_names));
    msg << severity_names[(int)severity] << ": "
        << "(" << file << ":" << line << ") ";
}

Logger::Logger(LogSeverity severity)
    : severity(severity) {
    assert(severity >= 0 && (size_t)severity < size(severity_names));
    msg << severity_names[(int)severity] << ": ";
}

void Logger::finish() noexcept(false) {
    if (!msg.str().empty() && msg.str().back() != '\n') {
        msg << '\n';
    }

    hannk_log(severity, msg.str().c_str());

    // TODO: call iOS-specific logger here?
    if (severity == FATAL) {
        std::abort();
    }
}

Logger::~Logger() noexcept(false) {
    finish();
}

Checker::Checker(const char *condition_string)
    : logger(FATAL) {
    logger.msg << " Condition Failed: " << condition_string << '\n';
}

Checker::Checker(const char *file, int line, const char *condition_string)
    : logger(FATAL, file, line) {
    logger.msg << " Condition Failed: " << condition_string << '\n';
}

Checker::~Checker() noexcept(false) {
    logger.finish();
    std::abort();
}

}  // namespace internal
}  // namespace hannk
