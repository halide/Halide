#ifndef HALIDE_APPS_HALLMARK_STATUS_HELPERS_H_
#define HALIDE_APPS_HALLMARK_STATUS_HELPERS_H_

#include "HalideRuntime.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace hallmark {

inline ::absl::Status StatusFromHalide(int halide_error) {
    switch (halide_error) {
    case halide_error_code_t::halide_error_code_success:
        return ::absl::OkStatus();
    case halide_error_code_t::halide_error_code_out_of_memory:
        return absl::ResourceExhaustedError("Halide error: out of memory");
    case halide_error_code_t::halide_error_code_device_malloc_failed:
        return absl::ResourceExhaustedError("Halide error: device malloc failed");
    case halide_error_code_t::halide_error_code_buffer_allocation_too_large:
        return absl::OutOfRangeError("Halide error: buffer allocation too large. Consider enabling 'large_buffers'");
    case halide_error_code_t::halide_error_code_buffer_extents_too_large:
        return absl::OutOfRangeError("Halide error: buffer extents too large");
    case halide_error_code_t::halide_error_code_constraint_violated:
        return absl::OutOfRangeError("Halide error: A constraint on a size or stride of an input or output buffer was not met.");
    case halide_error_code_t::halide_error_code_bad_dimensions:
        return absl::InvalidArgumentError("Halide error: The dimensions of an input buffer do not match the generator Input or Param dimensions.");
    default:
        return absl::UnknownError(::absl::StrFormat("Halide error: %d", halide_error));
    }
}

// -------------------------------------

#define RETURN_IF_ERROR(expr)            \
    do {                                 \
        auto status = (expr);            \
        if (!status.ok()) return status; \
    } while (0)

#define RET_CHECK(expr)                                                      \
    do {                                                                     \
        if (!(expr)) return absl::UnknownError("RET_CHECK failure: " #expr); \
    } while (0)

// -------------------------------------

template<typename T>
inline absl::Status DoAssignOrReturn(T &lhs, absl::StatusOr<T> result) {
    if (result.ok()) {
        lhs = result.value();
    }
    return result.status();
}

#define STATUS_MACROS_CONCAT_NAME_INNER(x, y) x##y
#define STATUS_MACROS_CONCAT_NAME(x, y) STATUS_MACROS_CONCAT_NAME_INNER(x, y)

#define ASSIGN_OR_RETURN_IMPL(status, lhs, rexpr)         \
    absl::Status status = DoAssignOrReturn(lhs, (rexpr)); \
    if (!status.ok()) return status;

#define ASSIGN_OR_RETURN(lhs, rexpr) \
    ASSIGN_OR_RETURN_IMPL(           \
        STATUS_MACROS_CONCAT_NAME(_status_or_value, __COUNTER__), lhs, rexpr);

// -------------------------------------

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_STATUS_HELPERS_H_
