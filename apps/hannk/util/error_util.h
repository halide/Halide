#ifndef HANNK_ERROR_UTIL_H
#define HANNK_ERROR_UTIL_H

#include <cassert>
#include <sstream>
#include <vector>

#include "HalideRuntime.h"
#include "util/hannk_log.h"

namespace hannk {

// There should be std::size, like std::begin and std::end.
template<typename T, size_t N>
constexpr size_t size(T (&)[N]) {
    return N;
}

std::ostream &operator<<(std::ostream &stream, const halide_type_t &type);
std::ostream &operator<<(std::ostream &s, const halide_dimension_t &dim);

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

namespace internal {

struct Logger final {
    std::ostringstream msg;
    const LogSeverity severity;

    Logger(LogSeverity severity);
    Logger(LogSeverity severity, const char *file, int line);

    ~Logger() noexcept(false);

    void finish() noexcept(false);

    template<typename T>
    Logger &operator<<(const T &x) {
        msg << x;
        return *this;
    }

    Logger() = delete;
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;
};

struct Checker final {
    Logger logger;

    Checker(const char *condition_string);
    Checker(const char *file, int line, const char *condition_string);

    template<typename T>
    Checker &operator<<(const T &x) {
        logger << x;
        return *this;
    }

    Checker &ref() {
        return *this;
    }

    [[noreturn]] ~Checker() noexcept(false);

    Checker() = delete;
    Checker(const Checker &) = delete;
    Checker &operator=(const Checker &) = delete;
    Checker(Checker &&) = delete;
    Checker &operator=(Checker &&) = delete;
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
    void operator&(Checker &) {
    }
};

}  // namespace internal

#ifndef NDEBUG
// In debug builds, include file-and-line
#define HLOG(SEVERITY) \
    ::hannk::internal::Logger(::hannk::SEVERITY, __FILE__, __LINE__)
#else
// In nondebug builds, don't include file-and-line
#define HLOG(SEVERITY) \
    ::hannk::internal::Logger(::hannk::SEVERITY)
#endif

/**
 * HCHECK() is used to implement our assertion macros
 * in such a way that the messages output for the assertion are only
 * evaluated if the assertion's value is false.
 *
 * Note that this macro intentionally has no parens internally; in actual
 * use, the implicit grouping will end up being
 *
 *   condition ? (void) : (Voidifier() & (Checker << arg1 << arg2 ... << argN))
 *
 * This (regrettably) requires a macro to work, but has the highly desirable
 * effect that all assertion parameters are totally skipped (not ever evaluated)
 * when the assertion is true.
 */
#ifndef NDEBUG
// In debug builds, include file-and-line
#define HCHECK(condition) \
    (condition) ? (void)0 : ::hannk::internal::Voidifier() & ::hannk::internal::Checker(__FILE__, __LINE__, #condition).ref()
#else
// In nondebug builds, don't include file-and-line
#define HCHECK(condition) \
    (condition) ? (void)0 : ::hannk::internal::Voidifier() & ::hannk::internal::Checker(#condition).ref()
#endif
}  // namespace hannk

#endif  // HANNK_ERROR_UTIL_H
