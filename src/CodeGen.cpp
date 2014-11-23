#include "CodeGen.h"

namespace Halide {

namespace Internal {

// Returns true if the given function name is one of the Halide runtime
// functions that takes a user_context pointer as its first parameter.
bool function_takes_user_context(const std::string &name) {
    static const char *user_context_runtime_funcs[] = {
        "halide_copy_to_host",
        "halide_copy_to_dev",
        "halide_current_time_ns",
        "halide_debug_to_file",
        "halide_dev_free",
        "halide_dev_malloc",
        "halide_dev_run",
        "halide_dev_sync",
        "halide_do_par_for",
        "halide_do_task",
        "halide_error",
        "halide_free",
        "halide_init_kernels",
        "halide_malloc",
        "halide_print",
        "halide_profiling_timer",
        "halide_release",
        "halide_start_clock",
        "halide_trace",
        "halide_memoization_cache_lookup",
        "halide_memoization_cache_store"
    };
    const int num_funcs = sizeof(user_context_runtime_funcs) /
        sizeof(user_context_runtime_funcs[0]);
    for (int i = 0; i < num_funcs; ++i) {
        if (name == user_context_runtime_funcs[i]) {
            return true;
        }
    }
    return false;
}

}

}
