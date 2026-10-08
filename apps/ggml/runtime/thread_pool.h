#pragma once

// The API that the app's copy of Halide's thread pool (README.md) adds to
// Halide's runtime. Upstream, these are proposed as halide_thread_pool_keep_awake
// and Halide::Runtime::ThreadPoolKeepAwake (halide/Halide#9526).

extern "C" {

/** Acquire (keep_awake = true) or release (false) a reference on the thread
 * pool's process-wide keep-awake count, and return the new count. While it is
 * positive, idle workers (up to halide_get_num_threads() - 1) and threads
 * waiting for parallel loops they started to finish poll for work instead of
 * sleeping, and idle workers aren't demoted to the B team, so back-to-back
 * parallel loops don't pay to wake threads. An idle thread sleeps anyway after
 * 4096 polls without work (a few ms). Acquiring the first reference wakes all
 * idle workers. Releasing an unheld reference calls halide_error and returns
 * halide_error_code_generic_error. The count survives
 * halide_shutdown_thread_pool, and is independent of halide_set_num_threads. */
int ggml_halide_thread_pool_keep_awake(bool keep_awake);

}  // extern "C"

namespace ggml_halide {

/** Holds a reference on the keep-awake count for its lifetime. */
class ThreadPoolKeepAwake {
public:
    ThreadPoolKeepAwake() {
        (void)ggml_halide_thread_pool_keep_awake(true);
    }
    ~ThreadPoolKeepAwake() {
        (void)ggml_halide_thread_pool_keep_awake(false);
    }
    ThreadPoolKeepAwake(const ThreadPoolKeepAwake &) = delete;
    ThreadPoolKeepAwake &operator=(const ThreadPoolKeepAwake &) = delete;
    ThreadPoolKeepAwake(ThreadPoolKeepAwake &&) = delete;
    ThreadPoolKeepAwake &operator=(ThreadPoolKeepAwake &&) = delete;
};

}  // namespace ggml_halide
