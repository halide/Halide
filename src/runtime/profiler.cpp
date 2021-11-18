#include "HalideRuntime.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

// Note: The profiler thread may out-live any valid user_context, or
// be used across many different user_contexts, so nothing it calls
// can depend on the user context.

extern "C" {
// Returns the address of the global halide_profiler state
WEAK halide_profiler_state *halide_profiler_get_state() {
    static halide_profiler_state s = {{{0}}, 1, 0, 0, 0, nullptr, nullptr, nullptr};
    return &s;
}
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK halide_profiler_pipeline_stats *find_or_create_pipeline(const char *pipeline_name, int num_funcs, const uint64_t *func_names) {
    halide_profiler_state *s = halide_profiler_get_state();

    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        // The same pipeline will deliver the same global constant
        // string, so they can be compared by pointer.
        if (p->name == pipeline_name &&
            p->num_funcs == num_funcs) {
            return p;
        }
    }
    // Create a new pipeline stats entry.
    halide_profiler_pipeline_stats *p =
        (halide_profiler_pipeline_stats *)malloc(sizeof(halide_profiler_pipeline_stats));
    if (!p) {
        return nullptr;
    }
    p->next = s->pipelines;
    p->name = pipeline_name;
    p->first_func_id = s->first_free_id;
    p->num_funcs = num_funcs;
    p->runs = 0;
    p->time = 0;
    p->samples = 0;
    p->memory_current = 0;
    p->memory_peak = 0;
    p->memory_total = 0;
    p->num_allocs = 0;
    p->active_threads_numerator = 0;
    p->active_threads_denominator = 0;
    p->funcs = (halide_profiler_func_stats *)malloc(num_funcs * sizeof(halide_profiler_func_stats));
    if (!p->funcs) {
        free(p);
        return nullptr;
    }
    for (int i = 0; i < num_funcs; i++) {
        p->funcs[i].time = 0;
        p->funcs[i].name = (const char *)(func_names[i]);
        p->funcs[i].memory_current = 0;
        p->funcs[i].memory_peak = 0;
        p->funcs[i].memory_total = 0;
        p->funcs[i].num_allocs = 0;
        p->funcs[i].stack_peak = 0;
        p->funcs[i].active_threads_numerator = 0;
        p->funcs[i].active_threads_denominator = 0;
    }
    s->first_free_id += num_funcs;
    s->pipelines = p;
    return p;
}

WEAK void bill_func(halide_profiler_state *s, int func_id, uint64_t time, int active_threads) {
    halide_profiler_pipeline_stats *p_prev = nullptr;
    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        if (func_id >= p->first_func_id && func_id < p->first_func_id + p->num_funcs) {
            if (p_prev) {
                // Bubble the pipeline to the top to speed up future queries.
                p_prev->next = (halide_profiler_pipeline_stats *)(p->next);
                p->next = s->pipelines;
                s->pipelines = p;
            }
            halide_profiler_func_stats *f = p->funcs + func_id - p->first_func_id;
            f->time += time;
            f->active_threads_numerator += active_threads;
            f->active_threads_denominator += 1;
            p->time += time;
            p->samples++;
            p->active_threads_numerator += active_threads;
            p->active_threads_denominator += 1;
            return;
        }
        p_prev = p;
    }
    // Someone must have called reset_state while a kernel was running. Do nothing.
}

WEAK void sampling_profiler_thread(void *) {
    halide_profiler_state *s = halide_profiler_get_state();

    // grab the lock
    halide_mutex_lock(&s->lock);

    while (s->current_func != halide_profiler_please_stop) {

        uint64_t t1 = halide_current_time_ns(nullptr);
        uint64_t t = t1;
        while (true) {
            int func, active_threads;
            if (s->get_remote_profiler_state) {
                // Execution has disappeared into remote code running
                // on an accelerator (e.g. Hexagon DSP)
                s->get_remote_profiler_state(&func, &active_threads);
            } else {
                func = s->current_func;
                active_threads = s->active_threads;
            }
            uint64_t t_now = halide_current_time_ns(nullptr);
            if (func == halide_profiler_please_stop) {
                break;
            } else if (func >= 0) {
                // Assume all time since I was last awake is due to
                // the currently running func.
                bill_func(s, func, t_now - t, active_threads);
            }
            t = t_now;

            // Release the lock, sleep, reacquire.
            int sleep_ms = s->sleep_time;
            halide_mutex_unlock(&s->lock);
            halide_sleep_ms(nullptr, sleep_ms);
            halide_mutex_lock(&s->lock);
        }
    }

    halide_mutex_unlock(&s->lock);
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

namespace {

template<typename T>
void sync_compare_max_and_swap(T *ptr, T val) {
    T old_val = *ptr;
    while (val > old_val) {
        T temp = old_val;
        old_val = __sync_val_compare_and_swap(ptr, old_val, val);
        if (temp == old_val) {
            return;
        }
    }
}

}  // namespace

extern "C" {
// Returns the address of the pipeline state associated with pipeline_name.
WEAK halide_profiler_pipeline_stats *halide_profiler_get_pipeline_state(const char *pipeline_name) {
    halide_profiler_state *s = halide_profiler_get_state();

    ScopedMutexLock lock(&s->lock);

    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        // The same pipeline will deliver the same global constant
        // string, so they can be compared by pointer.
        if (p->name == pipeline_name) {
            return p;
        }
    }
    return nullptr;
}

// Returns a token identifying this pipeline instance.
WEAK int halide_profiler_pipeline_start(void *user_context,
                                        const char *pipeline_name,
                                        int num_funcs,
                                        const uint64_t *func_names) {
    halide_profiler_state *s = halide_profiler_get_state();

    ScopedMutexLock lock(&s->lock);

    if (!s->sampling_thread) {
        halide_start_clock(user_context);
        s->sampling_thread = halide_spawn_thread(sampling_profiler_thread, nullptr);
    }

    halide_profiler_pipeline_stats *p =
        find_or_create_pipeline(pipeline_name, num_funcs, func_names);
    if (!p) {
        // Allocating space to track the statistics failed.
        return halide_error_out_of_memory(user_context);
    }
    p->runs++;

    return p->first_func_id;
}

WEAK void halide_profiler_stack_peak_update(void *user_context,
                                            void *pipeline_state,
                                            uint64_t *f_values) {
    halide_profiler_pipeline_stats *p_stats = (halide_profiler_pipeline_stats *)pipeline_state;
    halide_abort_if_false(user_context, p_stats != nullptr);

    // Note: Update to the counter is done without grabbing the state's lock to
    // reduce lock contention. One potential issue is that other call that frees the
    // pipeline and function stats structs may be running in parallel. However, the
    // current desctructor (called on profiler shutdown) does not free the structs
    // unless user specifically calls halide_profiler_reset().

    // Update per-func memory stats
    for (int i = 0; i < p_stats->num_funcs; ++i) {
        if (f_values[i] != 0) {
            sync_compare_max_and_swap(&(p_stats->funcs[i]).stack_peak, f_values[i]);
        }
    }
}

WEAK void halide_profiler_memory_allocate(void *user_context,
                                          void *pipeline_state,
                                          int func_id,
                                          uint64_t incr) {
    // It's possible to have 'incr' equal to zero if the allocation is not
    // executed conditionally.
    if (incr == 0) {
        return;
    }

    halide_profiler_pipeline_stats *p_stats = (halide_profiler_pipeline_stats *)pipeline_state;
    halide_abort_if_false(user_context, p_stats != nullptr);
    halide_abort_if_false(user_context, func_id >= 0);
    halide_abort_if_false(user_context, func_id < p_stats->num_funcs);

    halide_profiler_func_stats *f_stats = &p_stats->funcs[func_id];

    // Note: Update to the counter is done without grabbing the state's lock to
    // reduce lock contention. One potential issue is that other call that frees the
    // pipeline and function stats structs may be running in parallel. However, the
    // current desctructor (called on profiler shutdown) does not free the structs
    // unless user specifically calls halide_profiler_reset().

    // Update per-pipeline memory stats
    __sync_add_and_fetch(&p_stats->num_allocs, 1);
    __sync_add_and_fetch(&p_stats->memory_total, incr);
    uint64_t p_mem_current = __sync_add_and_fetch(&p_stats->memory_current, incr);
    sync_compare_max_and_swap(&p_stats->memory_peak, p_mem_current);

    // Update per-func memory stats
    __sync_add_and_fetch(&f_stats->num_allocs, 1);
    __sync_add_and_fetch(&f_stats->memory_total, incr);
    uint64_t f_mem_current = __sync_add_and_fetch(&f_stats->memory_current, incr);
    sync_compare_max_and_swap(&f_stats->memory_peak, f_mem_current);
}

WEAK void halide_profiler_memory_free(void *user_context,
                                      void *pipeline_state,
                                      int func_id,
                                      uint64_t decr) {
    // It's possible to have 'decr' equal to zero if the allocation is not
    // executed conditionally.
    if (decr == 0) {
        return;
    }

    halide_profiler_pipeline_stats *p_stats = (halide_profiler_pipeline_stats *)pipeline_state;
    halide_abort_if_false(user_context, p_stats != nullptr);
    halide_abort_if_false(user_context, func_id >= 0);
    halide_abort_if_false(user_context, func_id < p_stats->num_funcs);

    halide_profiler_func_stats *f_stats = &p_stats->funcs[func_id];

    // Note: Update to the counter is done without grabbing the state's lock to
    // reduce lock contention. One potential issue is that other call that frees the
    // pipeline and function stats structs may be running in parallel. However, the
    // current destructor (called on profiler shutdown) does not free the structs
    // unless user specifically calls halide_profiler_reset().

    // Update per-pipeline memory stats
    __sync_sub_and_fetch(&p_stats->memory_current, decr);

    // Update per-func memory stats
    __sync_sub_and_fetch(&f_stats->memory_current, decr);
}

WEAK void halide_profiler_report_unlocked(void *user_context, halide_profiler_state *s) {

    char line_buf[1024];
    Printer<StringStreamPrinter, sizeof(line_buf)> sstr(user_context, line_buf);

    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        float t = p->time / 1000000.0f;
        if (!p->runs) {
            continue;
        }
        sstr.clear();
        bool serial = p->active_threads_numerator == p->active_threads_denominator;
        float threads = p->active_threads_numerator / (p->active_threads_denominator + 1e-10);
        sstr << p->name << "\n"
             << " total time: " << t << " ms"
             << "  samples: " << p->samples
             << "  runs: " << p->runs
             << "  time/run: " << t / p->runs << " ms\n";
        if (!serial) {
            sstr << " average threads used: " << threads << "\n";
        }
        sstr << " heap allocations: " << p->num_allocs
             << "  peak heap usage: " << p->memory_peak << " bytes\n";
        halide_print(user_context, sstr.str());

        bool print_f_states = p->time || p->memory_total;
        if (!print_f_states) {
            for (int i = 0; i < p->num_funcs; i++) {
                halide_profiler_func_stats *fs = p->funcs + i;
                if (fs->stack_peak) {
                    print_f_states = true;
                    break;
                }
            }
        }

        if (print_f_states) {
            for (int i = 0; i < p->num_funcs; i++) {
                size_t cursor = 0;
                sstr.clear();
                halide_profiler_func_stats *fs = p->funcs + i;

                // The first func is always a catch-all overhead
                // slot. Only report overhead time if it's non-zero
                if (i == 0 && fs->time == 0) {
                    continue;
                }

                sstr << "  " << fs->name << ": ";
                cursor += 25;
                while (sstr.size() < cursor) {
                    sstr << " ";
                }

                float ft = fs->time / (p->runs * 1000000.0f);
                sstr << ft;
                // We don't need 6 sig. figs.
                sstr.erase(3);
                sstr << "ms";
                cursor += 10;
                while (sstr.size() < cursor) {
                    sstr << " ";
                }

                int percent = 0;
                if (p->time != 0) {
                    percent = (100 * fs->time) / p->time;
                }
                sstr << "(" << percent << "%)";
                cursor += 8;
                while (sstr.size() < cursor) {
                    sstr << " ";
                }

                if (!serial) {
                    float threads = fs->active_threads_numerator / (fs->active_threads_denominator + 1e-10);
                    sstr << "threads: " << threads;
                    sstr.erase(3);
                    cursor += 15;
                    while (sstr.size() < cursor) {
                        sstr << " ";
                    }
                }

                if (fs->memory_peak) {
                    cursor += 15;
                    sstr << " peak: " << fs->memory_peak;
                    while (sstr.size() < cursor) {
                        sstr << " ";
                    }
                    sstr << " num: " << fs->num_allocs;
                    cursor += 15;
                    while (sstr.size() < cursor) {
                        sstr << " ";
                    }
                    int alloc_avg = 0;
                    if (fs->num_allocs != 0) {
                        alloc_avg = fs->memory_total / fs->num_allocs;
                    }
                    sstr << " avg: " << alloc_avg;
                }
                if (fs->stack_peak > 0) {
                    sstr << " stack: " << fs->stack_peak;
                }
                sstr << "\n";

                halide_print(user_context, sstr.str());
            }
        }
    }
}

WEAK void halide_profiler_report(void *user_context) {
    halide_profiler_state *s = halide_profiler_get_state();
    ScopedMutexLock lock(&s->lock);
    halide_profiler_report_unlocked(user_context, s);
}

WEAK void halide_profiler_reset_unlocked(halide_profiler_state *s) {
    while (s->pipelines) {
        halide_profiler_pipeline_stats *p = s->pipelines;
        s->pipelines = (halide_profiler_pipeline_stats *)(p->next);
        free(p->funcs);
        free(p);
    }
    s->first_free_id = 0;
}

WEAK void halide_profiler_reset() {
    // WARNING: Do not call this method while any other halide
    // pipeline is running; halide_profiler_memory_allocate/free and
    // halide_profiler_stack_peak_update update the profiler pipeline's
    // state without grabbing the global profiler state's lock.
    halide_profiler_state *s = halide_profiler_get_state();
    ScopedMutexLock lock(&s->lock);
    halide_profiler_reset_unlocked(s);
}

#ifndef WINDOWS
__attribute__((destructor))
#endif
WEAK void
halide_profiler_shutdown() {
    halide_profiler_state *s = halide_profiler_get_state();
    if (!s->sampling_thread) {
        return;
    }

    s->current_func = halide_profiler_please_stop;
    halide_join_thread(s->sampling_thread);
    s->sampling_thread = nullptr;
    s->current_func = halide_profiler_outside_of_halide;

    // Print results. No need to lock anything because we just shut
    // down the thread.
    halide_profiler_report_unlocked(nullptr, s);

    halide_profiler_reset_unlocked(s);
}

namespace {
#ifdef WINDOWS
WEAK void halide_windows_profiler_shutdown() {
    halide_profiler_state *s = halide_profiler_get_state();
    if (!s->sampling_thread) {
        return;
    }

    // On Windows it is unsafe to do anything with threads or critical
    // sections in a static destructor as it may run after threads
    // have been killed by the OS. Furthermore, may calls, even things
    // like EnterCriticalSection may be set to kill the process if
    // called during process shutdown. Hence kthis routine doesn't attmept
    // to clean up state as the destructor does on other platforms.

    // Print results. Avoid locking as it will cause problems and
    // nothing should be running.
    halide_profiler_report_unlocked(nullptr, s);
}
#endif
}  // namespace

WEAK void halide_profiler_pipeline_end(void *user_context, void *state) {
    ((halide_profiler_state *)state)->current_func = halide_profiler_outside_of_halide;
}

}  // extern "C"
