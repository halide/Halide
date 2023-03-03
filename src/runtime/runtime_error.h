#ifndef HALIDE_RUNTIME_ERROR_H
#define HALIDE_RUNTIME_ERROR_H

#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_internal.h"

/*
  Guidelines for error-handling in Halide Runtime code.

- halide_error_code_t is the preferred error code for all functions in
  the runtime. Existing 'public' API calls will remain typed as `int`
  for compatibility (for now), but all values they return are expected
  to be in the range of halide_error_code_t. All other functions
  (internal etc) should be migrated to return halide_error_code_t for
  error conditions.

- Functions should prefer to normalize all errors to halide_error_code_t
  within the function in which they are first detected.

- Funtions should prefer to return an explicit halide_error_code_t
  (vs. returning a null pointer, etc); additional 'return' values
  should be returned via out-params-by-reference.

- When encountering an error condition that **HASN'T** been detected via
  a failing `halide_error_code_t` value, you must always use
  `report_error()` to return the value. You should never directly do
  `return halide_error_code_foo;`, but rather `return report_error_foo
  (user_context);`. (Internally, this calls `halide_error()` and
  returns the foo value.)

- When encountering an error condition that **HAS** been detected via a
  failing `halide_error_code_t` value, you should simply return that
  value, and not call `report_error()`. The implication here is
  always "if I get a failing halide_error_code_t as a result, I should
  assume that the provider of that result has already called
  halide_error()."

- Exception: It is never necessary to use `report_error()` to report
  success (ie it is always ok to do `return
  halide_error_code_success;`).

- Functions should prefer to avoid calling `halide_error()` directly,
  and use `report_error()` unless not feasible. (This applies to `error
  ()` as well, which would eventually be removed.)

- Returning a `halide_error_code_t` implies an error condition that
  should terminate the current Halide pipeline (but *not* necessarily
  the current process). If you need to indicate a condition that is not
  success but not failure -- e.g.,
  "retry in the following way" -- you should not use
   halide_error_code_t.
*/

namespace Halide {
namespace Runtime {
namespace Internal {

// The static_cast<Args&&> is out homegrown version of std::forward

template<typename... Args>
halide_error_code_t report_error_with_code(void *user_context,
                                           halide_error_code_t error,
                                           Args &&...args) {
    if constexpr (sizeof...(Args) > 0) {
        // Arbitrary number of arguments, assume we need the malloc-based stringstream.
        StringStreamPrinter<> str(user_context);
        str << "HalideRuntimeError=" << (int)error << ": ";
        (str << ... << static_cast<Args &&>(args));
        str.add_eol();
        halide_error(user_context, str.str());
    } else {
        // No extra args: really, 64 bytes should be enough here, but let's go 128
        // in case someone is sloppy when adding stuff
        StackStringStreamPrinter<128> str(user_context);
        str << "HalideRuntimeError=" << (int)error;
        str.add_eol();
        halide_error(user_context, str.str());
    }
    return error;
}

// Make `report_error` an alias for `report_error_generic_error`
template<typename... Args>
halide_error_code_t report_error(void *user_context, Args &&...args) {
    return report_error_with_code(user_context, halide_error_code_generic_error, static_cast<Args &&>(args)...);
}

#define HALIDE_REPORT_ERROR(ID)                                                                             \
    template<typename... Args>                                                                              \
    halide_error_code_t report_error_##ID(void *user_context, Args &&...args) {                             \
        return report_error_with_code(user_context, halide_error_code_##ID, static_cast<Args &&>(args)...); \
    }

HALIDE_REPORT_ERROR(access_out_of_bounds)
HALIDE_REPORT_ERROR(bad_dimensions)
HALIDE_REPORT_ERROR(bad_extern_fold)
HALIDE_REPORT_ERROR(bad_fold)
HALIDE_REPORT_ERROR(bad_type)
HALIDE_REPORT_ERROR(buffer_allocation_too_large)
HALIDE_REPORT_ERROR(buffer_argument_is_null)
HALIDE_REPORT_ERROR(buffer_extents_negative)
HALIDE_REPORT_ERROR(buffer_extents_too_large)
HALIDE_REPORT_ERROR(buffer_is_null)
HALIDE_REPORT_ERROR(constraint_violated)
HALIDE_REPORT_ERROR(constraints_make_required_region_smaller)
HALIDE_REPORT_ERROR(copy_to_device_failed)
HALIDE_REPORT_ERROR(copy_to_host_failed)
HALIDE_REPORT_ERROR(debug_to_file_failed)
HALIDE_REPORT_ERROR(device_buffer_copy_failed)
HALIDE_REPORT_ERROR(device_crop_failed)
HALIDE_REPORT_ERROR(device_crop_unsupported)
HALIDE_REPORT_ERROR(device_detach_native_failed)
HALIDE_REPORT_ERROR(device_dirty_with_no_device_support)
HALIDE_REPORT_ERROR(device_free_failed)
HALIDE_REPORT_ERROR(device_interface_no_device)
HALIDE_REPORT_ERROR(device_malloc_failed)
HALIDE_REPORT_ERROR(device_run_failed)
HALIDE_REPORT_ERROR(device_sync_failed)
HALIDE_REPORT_ERROR(device_wrap_native_failed)
HALIDE_REPORT_ERROR(explicit_bounds_too_small)
HALIDE_REPORT_ERROR(fold_factor_too_small)
HALIDE_REPORT_ERROR(host_and_device_dirty)
HALIDE_REPORT_ERROR(host_is_null)
HALIDE_REPORT_ERROR(incompatible_device_interface)
HALIDE_REPORT_ERROR(internal_error)
HALIDE_REPORT_ERROR(no_device_interface)
HALIDE_REPORT_ERROR(out_of_memory)
HALIDE_REPORT_ERROR(param_too_large)
HALIDE_REPORT_ERROR(param_too_small)
HALIDE_REPORT_ERROR(requirement_failed)
HALIDE_REPORT_ERROR(specialize_fail)
HALIDE_REPORT_ERROR(storage_bound_too_small)
HALIDE_REPORT_ERROR(unaligned_host_ptr)

#undef HALIDE_REPORT_ERROR

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_ERROR_H
