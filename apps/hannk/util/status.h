#ifndef HANNK_STATUS_H
#define HANNK_STATUS_H

#include <cstring>
#include <iostream>

#include "util/error_util.h"

#ifndef HANNK_STATUS_SOURCE_LOCATION
#ifndef NDEBUG
#ifdef __is_identifier
#if !__is_identifier(__builtin_LINE) && !__is_identifier(__builtin_FILE) && !__is_identifier(__builtin_FUNCTION)
#define HANNK_STATUS_SOURCE_LOCATION 1
#endif
#elif __GNUC__ >= 5
#define HANNK_STATUS_SOURCE_LOCATION 1
#endif
#endif
#endif

#ifndef HANNK_STATUS_SOURCE_LOCATION
#define HANNK_STATUS_SOURCE_LOCATION 0
#endif

namespace hannk {

namespace internal {

#if HANNK_STATUS_SOURCE_LOCATION
struct SourceLocation final {
    // These aren't 'standard' yet but widely supported in modern GCC and Clang compilers
    const char *function = __builtin_FUNCTION();
    const char *file = __builtin_FILE();
    unsigned line = __builtin_LINE();
};
#endif  // HANNK_STATUS_SOURCE_LOCATION

}  // namespace internal

// [[nodiscard]] is the C++17 way of specifying MUST_USE_RESULT
class [[nodiscard]] Status final {
public:
    enum Code {
        OK = 0,
        Error = 1,
        // Add more as needed, but only as *needed*; very little of our code
        // cares much about why things fail, only whether they fail or not.
    };

#if HANNK_STATUS_SOURCE_LOCATION
    /*not-explicit*/ constexpr Status(Code code = OK, internal::SourceLocation location = {})
        : code_(code), location_(std::move(location)) {
    }
#else
    /*not-explicit*/ constexpr Status(Code code = OK)
        : code_(code) {
    }
#endif

    [[nodiscard]] bool ok() const {
        return code_ == OK;
    }

    [[nodiscard]] Code code() const {
        return code_;
    }

    [[nodiscard]] std::string to_string() const {
        const char *result = ok() ? "OK" : "Error";
#if HANNK_STATUS_SOURCE_LOCATION
        std::ostringstream oss;
        oss << "Status::" << result << " in " << location_.function << "() (" << location_.file << ":" << std::to_string(location_.line) << ")";
        return oss.str();
#else
        return result;
#endif
    }

    friend std::ostream &operator<<(std::ostream &stream, const Status &status) {
        stream << status.to_string();
        return stream;
    }

    // Generally, this should only be called by top-level code (i.e., something with a main());
    // otherwise, return the result to the caller and force them to deal with it.
    void check() const {
        if (!ok()) {
            HLOG(FATAL) << to_string();
        }
    }

    friend bool operator==(const Status &a, const Status &b) {
        return a.code_ == b.code_;
    }

    friend bool operator!=(const Status &a, const Status &b) {
        return a.code_ != b.code_;
    }

    // Movable + Copyable
    Status(const Status &) = default;
    Status &operator=(const Status &) = default;
    Status(Status &&) = default;
    Status &operator=(Status &&) = default;

private:
    Code code_;
#if HANNK_STATUS_SOURCE_LOCATION
    internal::SourceLocation location_;
#endif
};

}  // namespace hannk

#endif  // HANNK_STATUS_H
