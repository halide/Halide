#ifndef HALIDE_APPS_HAMAL_STATUS_HELPERS_H_
#define HALIDE_APPS_HAMAL_STATUS_HELPERS_H_

#include "HalideRuntime.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace hamal {

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

#define RETURN_IF_ERROR(expr)                                                    \
    do {                                                                         \
        /* Using _status below to avoid capture problems if expr is "status". */ \
        auto status = (expr);                                                    \
        if (!status.ok()) return status;                                         \
    } while (0)

#define token_paste_inner(x, y) x##y
#define token_paste(x, y) token_paste_inner(x, y)

template<typename T>
absl::Status assign_or_return_impl(T &lhs, absl::StatusOr<T> result) {
    if (result.ok()) {
        lhs = result.value();
    }
    return result.status();
}

#define ASSIGN_OR_RETURN_IMPL(status, lhs, rhs)         \
    absl::Status status = assign_or_return_impl(lhs, rhs); \
    if (!status.ok()) return status;

#define ASSIGN_OR_RETURN(lhs, rhs) \
    ASSIGN_OR_RETURN_IMPL(token_paste(_status_or_value, __COUNTER__), lhs, rhs);

// -------------------------------------

}  // namespace hamal

#endif  // HALIDE_APPS_HAMAL_STATUS_HELPERS_H_
