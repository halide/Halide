#ifndef HANNK_STATUS_H
#define HANNK_STATUS_H

#include <cstring>
#include <iostream>

#include "util/error_util.h"

// Status is designed to simplify down to a single int32 error-code when NDEBUG is defined,
// so overhead is kept to a bare minimum in optimized builds.

// If NDEBUG is undefined (i.e., asserts are enabled), try to save the source-location for each Status creation
// (if the C++ compiler supports it, which most modern ones do) to make debugging easier. You can manually override
// this by defining HANNK_STATUS_SOURCE_LOCATION to 0 or 1 to force-disable or force-enable, regardless of NDEBUG.
#ifndef HANNK_STATUS_SAVE_SOURCE_LOCATION
#ifndef NDEBUG
#ifdef __is_identifier
#if !__is_identifier(__builtin_LINE) && !__is_identifier(__builtin_FILE) && !__is_identifier(__builtin_FUNCTION)
#define HANNK_STATUS_SAVE_SOURCE_LOCATION 1
#endif
#elif __GNUC__ >= 5
#define HANNK_STATUS_SAVE_SOURCE_LOCATION 1
#endif
#endif
#endif

#ifndef HANNK_STATUS_SAVE_SOURCE_LOCATION
#define HANNK_STATUS_SAVE_SOURCE_LOCATION 0
#endif

#ifndef HANNK_STATUS_SAVE_VERBOSE_MSG
#ifndef NDEBUG
#define HANNK_STATUS_SAVE_VERBOSE_MSG 1
#else
#define HANNK_STATUS_SAVE_VERBOSE_MSG 0
#endif
#endif

namespace hannk {

namespace internal {

#if HANNK_STATUS_SAVE_SOURCE_LOCATION
struct SourceLocation final {
    // These aren't 'standard' yet but widely supported in modern GCC and Clang compilers
    const char *function = __builtin_FUNCTION();
    const char *file = __builtin_FILE();
    unsigned line = __builtin_LINE();
};
#endif  // HANNK_STATUS_SAVE_SOURCE_LOCATION

#if HANNK_STATUS_SAVE_VERBOSE_MSG

struct VerboseMsg final {
    VerboseMsg() = default;

    template<typename... Args>
    explicit VerboseMsg(Args... args) {
        static_assert(sizeof...(args) > 0);
        std::ostringstream oss;
        (oss << ... << args);
        msg = oss.str();
    }

    std::string msg;
};

#define VSTATUS(CODE, ...) (::hannk::Status(::hannk::Status::CODE, ::hannk::internal::VerboseMsg(__VA_ARGS__)))

#else

#define VSTATUS(CODE, ...) (::hannk::Status(::hannk::Status::CODE))

#endif

}  // namespace internal

// [[nodiscard]] is the C++17 way of specifying MUST_USE_RESULT
class [[nodiscard]] Status final {
public:
    enum Code {
        OK = 0,
        Error = 1,

        // Op (or type combination of Op) isn't implemented in Hannk.
        UnimplementedOp = 2,

        // Error returned by Halide.
        HalideError = 3,

        _CodeCount = 4,
        // Add more as needed, but only as *needed*; very little of our code
        // cares much about why things fail, only whether they fail or not.
    };

    /*not-explicit*/ Status(
        Code code = OK
#if HANNK_STATUS_SAVE_SOURCE_LOCATION
        ,
        internal::SourceLocation location = {}
#endif
        )
        : code_(code)
#if HANNK_STATUS_SAVE_VERBOSE_MSG
          ,
          vmsg_()
#endif
#if HANNK_STATUS_SAVE_SOURCE_LOCATION
          ,
          location_(std::move(location))
#endif
    {
    }

    // This ctor is meant for use only by the VSTATUS() macro.
    // You probably could call it directly, but please don't.
#if HANNK_STATUS_SAVE_VERBOSE_MSG
#if HANNK_STATUS_SAVE_SOURCE_LOCATION
    explicit Status(Code code, internal::VerboseMsg vmsg, internal::SourceLocation location = {})
        : code_(code), vmsg_(std::move(vmsg)), location_(std::move(location)) {
    }
#else
    explicit Status(Code code, internal::VerboseMsg vmsg)
        : code_(code), vmsg_(std::move(vmsg)) {
    }
#endif
#endif

    [[nodiscard]] bool ok() const {
        return code_ == OK;
    }

    [[nodiscard]] Code code() const {
        return code_;
    }

    [[nodiscard]] std::string to_string() const {
        static const char *const code_names[_CodeCount] = {
            "OK",
            "Error",
            "UnimplementedOp",
            "HalideError",
        };
        const char *result = code_names[code_];
#if HANNK_STATUS_SAVE_SOURCE_LOCATION || HANNK_STATUS_SAVE_VERBOSE_MSG
        std::ostringstream oss;
        oss << "Status::" << result;
#if HANNK_STATUS_SAVE_SOURCE_LOCATION
        oss << " in " << location_.function << "() (" << location_.file << ":" << std::to_string(location_.line) << ")";
#endif
#if HANNK_STATUS_SAVE_VERBOSE_MSG
        oss << ": " << vmsg_.msg;
#endif
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
            std::abort();
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
#if HANNK_STATUS_SAVE_VERBOSE_MSG
    internal::VerboseMsg vmsg_;
#endif
#if HANNK_STATUS_SAVE_SOURCE_LOCATION
    internal::SourceLocation location_;
#endif
};

// This is a macro (rather than an inline function) to ensure that the source location is useful.
#define halide_error_to_status(HALIDE_ERROR_CODE) \
    (Status((HALIDE_ERROR_CODE) == 0 ? Status::OK : Status::HalideError))

}  // namespace hannk

#endif  // HANNK_STATUS_H
