#ifndef ERROR_UTIL_H_
#define ERROR_UTIL_H_

#include <sstream>

#include "HalideRuntime.h"

namespace interpret_nn {

inline std::ostream &operator<<(std::ostream &stream, const halide_type_t &type) {
    if (type.code == halide_type_uint && type.bits == 1) {
        stream << "bool";
    } else {
        assert(type.code >= 0 && type.code <= 3);
        static const char *const names[4] = {"int", "uint", "float", "handle"};
        stream << names[type.code] << (int)type.bits;
    }
    if (type.lanes > 1) {
        stream << "x" << (int)type.lanes;
    }
    return stream;
}

inline std::ostream &operator<<(std::ostream &s, const halide_dimension_t &dim) {
    return s << "{" << dim.min << ", " << dim.extent << ", " << dim.stride << "}";
}

template<typename T>
inline std::ostream &operator<<(std::ostream &s, const std::vector<T> &v) {
    s << "{";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) {
            s << ", ";
        }
        s << v[i];
    }
    return s << "}";
}

// Note: all severity values output to stderr, not stdout.
// Note: ERROR does *not* trigger an exit()/abort() call.
enum LogSeverity {
    INFO = 0,
    WARNING = 1,
    ERROR = 2,
};

namespace internal {

struct Logger {
    std::ostringstream msg;
    const LogSeverity severity;

    Logger(LogSeverity severity);
    Logger(LogSeverity severity, const char *file, int line);

    ~Logger() noexcept(false);

    template<typename T>
    Logger &operator<<(const T &x) {
        msg << x;
        return *this;
    }

    Logger &ref() {
        return *this;
    }

    Logger() = delete;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;
};

struct CheckLogger : public Logger {
    CheckLogger(LogSeverity severity, const char *condition_string);
    CheckLogger(LogSeverity severity, const char *file, int line, const char *condition_string);

    [[noreturn]] ~CheckLogger() noexcept(false);

    CheckLogger() = delete;
    CheckLogger(const CheckLogger &) = delete;
    CheckLogger &operator=(const CheckLogger &) = delete;
    CheckLogger(CheckLogger &&) = delete;
    CheckLogger &operator=(CheckLogger &&) = delete;
};

// This uses operator precedence as a trick to avoid argument evaluation if
// an assertion is true: it is intended to be used as part of the
// _halide_internal_assertion macro, to coerce the result of the stream
// expression to void (to match the condition-is-false case).
class Voidifier final {
public:
    Voidifier() = default;
    Voidifier(const Voidifier &) = delete;
    Voidifier &operator=(const Voidifier &) = delete;
    Voidifier(Voidifier &&) = delete;
    Voidifier &operator=(Voidifier &&) = delete;

    // This has to be an operator with a precedence lower than << but
    // higher than ?:
    void operator&(Logger &) {
    }
};

}  // namespace internal

#ifndef NDEBUG
// In debug builds, include file-and-line
#define LOG(SEVERITY) \
    ::interpret_nn::internal::Logger(::interpret_nn::SEVERITY, __FILE__, __LINE__)
#else
// In nondebug builds, don't include file-and-line
#define LOG(SEVERITY) \
    ::interpret_nn::internal::Logger(::interpret_nn::SEVERITY)
#endif

/**
 * CHECK() is used to implement our assertion macros
 * in such a way that the messages output for the assertion are only
 * evaluated if the assertion's value is false.
 *
 * Note that this macro intentionally has no parens internally; in actual
 * use, the implicit grouping will end up being
 *
 *   condition ? (void) : (Voidifier() & (Logger << arg1 << arg2 ... << argN))
 *
 * This (regrettably) requires a macro to work, but has the highly desirable
 * effect that all assertion parameters are totally skipped (not ever evaluated)
 * when the assertion is true.
 */
#ifndef NDEBUG
// In debug builds, include file-and-line
#define CHECK(condition) \
    (condition) ? (void)0 : ::interpret_nn::internal::Voidifier() & ::interpret_nn::internal::CheckLogger(::interpret_nn::ERROR, __FILE__, __LINE__, #condition).ref()
#else
// In nondebug builds, don't include file-and-line
#define CHECK(condition) \
    (condition) ? (void)0 : ::interpret_nn::internal::Voidifier() & ::interpret_nn::internal::CheckLogger(::interpret_nn::ERROR, #condition).ref()
#endif
}  // namespace interpret_nn

#endif  // ERROR_UTIL_H_
