#ifndef APP_UTIL_H_
#define APP_UTIL_H_

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace app_util {

namespace internal {

struct FatalError final {
    std::ostringstream msg;

    FatalError(const char *file, int line, const char *condition_string) {
        msg << "Error @ " << file << ":" << line << ".";
        if (condition_string) {
            msg << " Condition failed: " << condition_string;
        }
        msg << "\n";
    }

    [[noreturn]] ~FatalError() noexcept(false) {
        if (!msg.str().empty() && msg.str().back() != '\n') {
            msg << "\n";
        }

        std::cerr << msg.str();
        abort();
    }

    template<typename T>
    FatalError &operator<<(const T &x) {
        msg << x;
        return *this;
    }

    FatalError &ref() {
        return *this;
    }

    FatalError() = delete;
    FatalError(const FatalError &) = delete;
    FatalError &operator=(const FatalError &) = delete;
    FatalError(FatalError &&) = delete;
    FatalError &operator=(FatalError &&) = delete;
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
    void operator&(FatalError &) {
    }
};

}  // namespace internal

#define APP_FATAL \
    ::app_util::internal::FatalError(__FILE__, __LINE__, nullptr)

/**
 * APP_CHECK() is used to implement our assertion macros
 * in such a way that the messages output for the assertion are only
 * evaluated if the assertion's value is false.
 *
 * Note that this macro intentionally has no parens internally; in actual
 * use, the implicit grouping will end up being
 *
 *   condition ? (void) : (Voidifier() & (FatalError << arg1 << arg2 ... << argN))
 *
 * This (regrettably) requires a macro to work, but has the highly desirable
 * effect that all assertion parameters are totally skipped (not ever evaluated)
 * when the assertion is true.
 */
#define APP_CHECK(condition) \
    (condition) ? (void)0 : ::app_util::internal::Voidifier() & ::app_util::internal::FatalError(__FILE__, __LINE__, #condition).ref()

}  // namespace app_util

#endif  // APP_UTIL_H_
