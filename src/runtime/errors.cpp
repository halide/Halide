#include "HalideRuntime.h"
#include "printer.h"

extern "C" {

WEAK int halide_error_bounds_inference_call_failed(void *user_context, const char *extern_stage_name, int result) {
    error(user_context)
        << "Bounds inference call to external stage " << extern_stage_name
        << " returned non-zero value: " << result;
    return result;
}

WEAK int halide_error_extern_stage_failed(void *user_context, const char *extern_stage_name, int result) {
    error(user_context)
        << "Call to external stage " << extern_stage_name
        << " returned non-zero value: " << result;
    return result;
}

WEAK int halide_error_explicit_bounds_too_small(void *user_context, const char *func_name, const char *var_name,
                                                int min_bound, int max_bound, int min_required, int max_required) {
    error(user_context)
        << "Bounds given for " << var_name << " in " << func_name
        << " (from " << min_bound << " to " << max_bound
        << ") do not cover required region (from " << min_required
        << " to " << max_required << ")";
    return halide_error_code_explicit_bounds_too_small;
}

WEAK int halide_error_bad_elem_size(void *user_context, const char *func_name,
                                    const char *type_name, int elem_size_given, int correct_elem_size) {
    error(user_context)
        << func_name << " has type " << type_name
        << " but elem_size of the buffer passed in is "
        << elem_size_given << " instead of " << correct_elem_size;
    return halide_error_code_bad_elem_size;
}

WEAK int halide_error_access_out_of_bounds(void *user_context, const char *func_name,
                                           int dimension, int min_touched, int max_touched,
                                           int min_valid, int max_valid) {
    if (min_touched < min_valid) {
        error(user_context)
            << func_name << " is accessed at " << min_touched
            << ", which is before the min (" << min_valid
            << ") in dimension " << dimension;
    } else if (max_touched > max_valid) {
        error(user_context)
            << func_name << " is accessed at " << max_touched
            << ", which is beyond the max (" << max_valid
            << ") in dimension " << dimension;
    }
    return halide_error_code_access_out_of_bounds;
}

WEAK int halide_error_buffer_allocation_too_large(void *user_context, const char *buffer_name, uint64_t allocation_size, uint64_t max_size) {
    error(user_context)
        << "Total allocation for buffer " << buffer_name
        << " is " << allocation_size
        << ", which exceeds the maximum size of " << max_size;
    return halide_error_code_buffer_allocation_too_large;
}

WEAK int halide_error_buffer_extents_too_large(void *user_context, const char *buffer_name, int64_t actual_size, int64_t max_size) {
    error(user_context)
        << "Product of extents for buffer " << buffer_name
        << " is " << actual_size
        << ", which exceeds the maximum size of " << max_size;
    return halide_error_code_buffer_extents_too_large;
}

WEAK int halide_error_constraints_make_required_region_smaller(void *user_context, const char *buffer_name,
                                                               int dimension,
                                                               int constrained_min, int constrained_extent,
                                                               int required_min, int required_extent) {
    int required_max = required_min + required_extent - 1;
    int constrained_max = constrained_min + required_extent - 1;
    error(user_context)
        << "Applying the constraints on " << buffer_name
        << " to the required region made it smaller. "
        << "Required size: " << required_min << " to " << required_max << ". "
        << "Constrained size: " << constrained_min << " to " << constrained_max << ".";
    return halide_error_code_constraints_make_required_region_smaller;
}

WEAK int halide_error_constraint_violated(void *user_context, const char *var, int val,
                                          const char *constrained_var, int constrained_val) {
    error(user_context)
        << "Constraint violated: " << var << " (" << val
        << ") == " << constrained_var << " (" << constrained_var << ")";
    return halide_error_code_constraint_violated;
}

WEAK int halide_error_param_too_small_i64(void *user_context, const char *param_name,
                                          int64_t val, int64_t min_val) {
    error(user_context)
        << "Parameter " << param_name
        << " is " << val
        << " but must be at least " << min_val;
    return halide_error_code_param_too_small;
}

WEAK int halide_error_param_too_small_u64(void *user_context, const char *param_name,
                                          uint64_t val, uint64_t min_val) {
    error(user_context)
        << "Parameter " << param_name
        << " is " << val
        << " but must be at least " << min_val;
    return halide_error_code_param_too_small;
}

WEAK int halide_error_param_too_small_f64(void *user_context, const char *param_name,
                                          double val, double min_val) {
    error(user_context)
        << "Parameter " << param_name
        << " is " << val
        << " but must be at least " << min_val;
    return halide_error_code_param_too_small;
}

WEAK int halide_error_param_too_large_i64(void *user_context, const char *param_name,
                                          int64_t val, int64_t max_val) {
    error(user_context)
        << "Parameter " << param_name
        << " is " << val
        << " but must be at most " << max_val;
    return halide_error_code_param_too_large;
}

WEAK int halide_error_param_too_large_u64(void *user_context, const char *param_name,
                                          uint64_t val, uint64_t max_val) {
    error(user_context)
        << "Parameter " << param_name
        << " is " << val
        << " but must be at most " << max_val;
    return halide_error_code_param_too_large;
}

WEAK int halide_error_param_too_large_f64(void *user_context, const char *param_name,
                                          double val, double max_val) {
    error(user_context)
        << "Parameter " << param_name
        << " is " << val
        << " but must be at most " << max_val;
    return halide_error_code_param_too_large;
}

WEAK int halide_error_out_of_memory(void *user_context) {
    // The error message builder uses malloc, so we can't use it here.
    halide_error(user_context, "Out of memory (halide_malloc returned NULL)");
    return halide_error_code_out_of_memory;
}

WEAK int halide_error_buffer_argument_is_null(void *user_context, const char *buffer_name) {
    error(user_context)
        << "Buffer argument " << buffer_name << " is NULL";
    return halide_error_code_buffer_argument_is_null;
}

WEAK int halide_error_debug_to_file_failed(void *user_context, const char *func,
                                           const char *filename, int error_code) {
    error(user_context)
        << "Failed to dump function " << func
        << " to file " << filename
        << " with error " << error_code;
    return halide_error_code_debug_to_file_failed;
}

WEAK int halide_error_unaligned_host_ptr(void *user_context, const char *func,
                                         int alignment) {
    error(user_context)
        << "The host pointer of " << func
        << " is not aligned to a " << alignment
        << " bytes boundary.";
    return halide_error_code_unaligned_host_ptr;
}

WEAK int halide_error_bad_fold(void *user_context, const char *func_name, const char *var_name,
                               const char *loop_name) {
    error(user_context)
        << "The folded storage dimension " << var_name << " of " << func_name
        << " was accessed out of order by loop " << loop_name << ".";
    return halide_error_code_bad_fold;
}

WEAK int halide_error_fold_factor_too_small(void *user_context, const char *func_name, const char *var_name,
                                            int fold_factor, const char *loop_name, int required_extent) {
    error(user_context)
        << "The fold factor (" << fold_factor
        << ") of dimension " << var_name << " of " << func_name
        << " is too small to store the required region accessed by loop "
        << loop_name << " (" << required_extent << ").";
    return halide_error_code_fold_factor_too_small;
}


}  // extern "C"
