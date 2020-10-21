#ifndef BASE_H_
#define BASE_H_

#include <sstream>

// TODO: move this into apps/support?

namespace interpret_nn {

struct FatalError {
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
};

#define halide_app_error \
    ::interpret_nn::FatalError(__FILE__, __LINE__, nullptr)

// This uses operator precedence as a trick to avoid argument evaluation if
// an assertion is true: it is intended to be used as part of the
// _halide_internal_assertion macro, to coerce the result of the stream
// expression to void (to match the condition-is-false case).
class Voidifier {
public:
    Voidifier() = default;
    // This has to be an operator with a precedence lower than << but
    // higher than ?:
    void operator&(FatalError &) {
    }
};

/**
 * halide_app_assert() is used to implement our assertion macros
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
#define halide_app_assert(condition) \
    (condition) ? (void)0 : ::interpret_nn::Voidifier() & ::interpret_nn::FatalError(__FILE__, __LINE__, #condition).ref()

}  // namespace interpret_nn

#endif  // BASE_H_
