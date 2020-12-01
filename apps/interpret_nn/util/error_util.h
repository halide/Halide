#ifndef ERROR_UTIL_H_
#define ERROR_UTIL_H_

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

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

#define LOG_FATAL \
    ::interpret_nn::internal::FatalError(__FILE__, __LINE__, nullptr)

/**
 * CHECK() is used to implement our assertion macros
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
#define CHECK(condition) \
    (condition) ? (void)0 : ::interpret_nn::internal::Voidifier() & ::interpret_nn::internal::FatalError(__FILE__, __LINE__, #condition).ref()

}  // namespace interpret_nn

#endif  // ERROR_UTIL_H_
