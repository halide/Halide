#include "util/error_util.h"

#include <cstdlib>

namespace hannk {

std::ostream &operator<<(std::ostream &stream, const halide_type_t &type) {
    if (type.code == halide_type_uint && type.bits == 1) {
        stream << "bool";
    } else {
        static const char *const names[5] = {"int", "uint", "float", "handle", "bfloat"};
        assert(type.code >= 0 && type.code < size(names));
        stream << names[type.code] << (int)type.bits;
    }
    if (type.lanes > 1) {
        stream << "x" << (int)type.lanes;
    }
    return stream;
}

std::ostream &operator<<(std::ostream &s, const halide_dimension_t &dim) {
    return s << "{" << dim.min << ", " << dim.extent << ", " << dim.stride << "}";
}

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
        msg << "\n";
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
    logger.msg << " Condition Failed: " << condition_string << "\n";
}

Checker::Checker(const char *file, int line, const char *condition_string)
    : logger(FATAL, file, line) {
    logger.msg << " Condition Failed: " << condition_string << "\n";
}

Checker::~Checker() noexcept(false) {
    logger.finish();
    std::abort();
}

}  // namespace internal
}  // namespace hannk
