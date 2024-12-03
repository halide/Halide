#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_atomics.h"
#include "scoped_mutex_lock.h"

// Note: The profiler thread may out-live any valid user_context, or
// be used across many different user_contexts, so nothing it calls
// can depend on the user context.

extern "C" {
// Returns the address of the global halide_profiler state
WEAK halide_profiler_state *halide_profiler_get_state() {
    static halide_profiler_state s = {
        {{0}},    // The mutex
        nullptr,  // pipeline stats
        nullptr,  // sampling thread
        nullptr,  // running instances
        nullptr,  // get_remote_profiler_state callback
        1000,     // Sampling rate in us
        0         // Flag that tells us to shutdown when it turns to 1
    };

    return &s;
}

#if TIMER_PROFILING
extern "C" void halide_start_timer_chain();
extern "C" void halide_disable_timer_interrupt();
extern "C" void halide_enable_timer_interrupt();
#endif

WEAK void halide_profiler_lock(struct halide_profiler_state *state) {
#if TIMER_PROFILING
    halide_disable_timer_interrupt();
#endif
    halide_mutex_lock(&state->lock);
}

WEAK void halide_profiler_unlock(struct halide_profiler_state *state) {
#if TIMER_PROFILING
    halide_enable_timer_interrupt();
#endif
    halide_mutex_unlock(&state->lock);
}
}

namespace Halide {
namespace Runtime {

namespace Internal {

class LockProfiler {
    halide_profiler_state *state;

public:
    explicit LockProfiler(halide_profiler_state *s)
        : state(s) {
        halide_profiler_lock(s);
    }
    ~LockProfiler() {
        halide_profiler_unlock(state);
    }
};

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
    s->pipelines = p;
    return p;
}

WEAK void update_running_instance(halide_profiler_instance_state *instance, uint64_t time) {
    halide_profiler_func_stats *f = instance->funcs + instance->current_func;
    f->time += time;
    f->active_threads_numerator += instance->active_threads;
    f->active_threads_denominator += 1;
    instance->samples++;
    instance->active_threads_numerator += instance->active_threads;
    instance->active_threads_denominator += 1;
    instance->billed_time += time;
}

extern "C" WEAK int halide_profiler_sample(struct halide_profiler_state *s, uint64_t *prev_t) {
    if (!s->instances) {
        // No Halide code is currently running
        return 0;
    }
    halide_profiler_instance_state *instance = s->instances;

    if (s->get_remote_profiler_state) {
        // Execution has disappeared into remote code running
        // on an accelerator (e.g. Hexagon DSP)

        // It shouldn't be possible to get into a state where multiple
        // pipelines are being profiled and one or both of them uses
        // get_remote_profiler_state.
        halide_debug_assert(nullptr, s->instances->next == nullptr);

        s->get_remote_profiler_state(&(instance->current_func), &(instance->active_threads));
    }

    uint64_t t_now = halide_current_time_ns(nullptr);
    uint64_t dt = t_now - *prev_t;
    while (instance) {
        update_running_instance(instance, dt);
        instance = instance->next;
    }
    *prev_t = t_now;
    return 0;
}

WEAK void sampling_profiler_thread(void *) {
    halide_profiler_state *s = halide_profiler_get_state();

    // grab the lock
    halide_mutex_lock(&s->lock);

    uint64_t t1 = halide_current_time_ns(nullptr);
    uint64_t t = t1;
    while (!s->shutdown || s->instances) {
        int err = halide_profiler_sample(s, &t);
        if (err < 0) {
            break;
        }
        // Release the lock, sleep, reacquire.
        halide_mutex_unlock(&s->lock);
        halide_sleep_us(nullptr, s->sleep_time);
        halide_mutex_lock(&s->lock);
    }

    halide_mutex_unlock(&s->lock);
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

namespace {

template<typename T>
void sync_compare_max_and_swap(T *ptr, T val) {
    using namespace Halide::Runtime::Internal::Synchronization;

    T old_val = *ptr;
    while (val > old_val) {
        if (atomic_cas_strong_sequentially_consistent(ptr, &old_val, &val)) {
            return;
        }
    }
}

}  // namespace

extern "C" {
// Returns the address of the pipeline state associated with pipeline_name.
WEAK halide_profiler_pipeline_stats *halide_profiler_get_pipeline_state(const char *pipeline_name) {
    halide_profiler_state *s = halide_profiler_get_state();

    LockProfiler lock(s);

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

// Populates the instance state struct
WEAK int halide_profiler_instance_start(void *user_context,
                                        const char *pipeline_name,
                                        int num_funcs,
                                        const uint64_t *func_names,
                                        halide_profiler_instance_state *instance) {
    // Tell the instance where we stashed the per-func state - just after the
    // instance itself.

    // First check that the layout agrees with the amount of stack space
    // allocated in the pipeline
    static_assert((sizeof(halide_profiler_func_stats) & 7) == 0);
    halide_profiler_func_stats *funcs = (halide_profiler_func_stats *)(instance + 1);

    // Zero initialize the instance and func state
    memset(instance, 0, (uint8_t *)(funcs + num_funcs) - (uint8_t *)instance);

    instance->funcs = funcs;

    halide_profiler_state *s = halide_profiler_get_state();
    {
        LockProfiler lock(s);

        // Push this instance onto the running instances list
        if (s->instances) {
            s->instances->prev_next = &(instance->next);

            // If there was something already running using the remote polling
            // method, we can't profile something else at the same time.
            if (s->get_remote_profiler_state) {
                error(user_context) << "Cannot profile pipeline " << pipeline_name
                                    << " while pipeline " << s->instances->pipeline_stats->name
                                    << " is running, because it is running on a device.";
                return halide_error_code_cannot_profile_pipeline;
            }
        }
        instance->next = s->instances;
        instance->prev_next = &(s->instances);
        s->instances = instance;

        // Find or create the pipeline statistics for this pipeline.
        halide_profiler_pipeline_stats *p =
            find_or_create_pipeline(pipeline_name, num_funcs, func_names);
        if (!p) {
            // Allocating space to track the statistics failed.
            return halide_error_out_of_memory(user_context);
        }

        // Tell the instance the pipeline to which it belongs.
        instance->pipeline_stats = p;

        if (!s->sampling_thread) {
#if TIMER_PROFILING
            halide_start_clock(user_context);
            halide_start_timer_chain();
            s->sampling_thread = (halide_thread *)1;
#else
            halide_start_clock(user_context);
            s->sampling_thread = halide_spawn_thread(sampling_profiler_thread, nullptr);
#endif
        }
    }

    instance->start_time = halide_current_time_ns(user_context);

    return 0;
}

WEAK int halide_profiler_instance_end(void *user_context, halide_profiler_instance_state *instance) {
    uint64_t end_time = halide_current_time_ns(user_context);
    halide_profiler_state *s = halide_profiler_get_state();
    LockProfiler lock(s);

    if (instance->should_collect_statistics) {

        uint64_t true_duration = end_time - instance->start_time;
        halide_profiler_pipeline_stats *p = instance->pipeline_stats;

        // Retire the instance, accumulating statistics onto the statistics for this
        // pipeline. Fields related to memory usages are tracked in the pipeline stats
        p->samples += instance->samples;
        p->time += true_duration;
        p->active_threads_numerator += instance->active_threads_numerator;
        p->active_threads_denominator += instance->active_threads_denominator;
        p->memory_total += instance->memory_total;
        p->memory_peak = max(p->memory_peak, instance->memory_peak);
        p->num_allocs += instance->num_allocs;
        p->runs++;

        // Compute an adjustment factor to account for the fact that the billed
        // time is not equal to the duration between start and end calls. We
        // could avoid this by just making sure there is a sampling event a the
        // start and end of the pipeline, but this would overcount whatever the
        // last value of current_func is at the end of the pipeline, and is
        // likely to undercount time spent in the first func in a
        // pipeline. Sampling events need to happen independently (in the random
        // variable sense) of any changes in current_func.
        double adjustment = 1;
        if (instance->billed_time > 0) {
            adjustment = (double)true_duration / instance->billed_time;
        }

        for (int f = 0; f < p->num_funcs; f++) {
            halide_profiler_func_stats *func = p->funcs + f;
            const halide_profiler_func_stats *instance_func = instance->funcs + f;
            // clang-tidy wants me to use a c standard library function to do
            // the rounding below, but those aren't guaranteed to be available
            // when compiling the runtime.
            func->time += (uint64_t)(instance_func->time * adjustment + 0.5);  // NOLINT
            func->active_threads_numerator += instance_func->active_threads_numerator;
            func->active_threads_denominator += instance_func->active_threads_denominator;
            func->num_allocs += instance_func->num_allocs;
            func->stack_peak = max(func->stack_peak, instance_func->stack_peak);
            func->memory_peak = max(func->memory_peak, instance_func->memory_peak);
            func->memory_total += instance_func->memory_total;
        }
    }

    // Remove myself from the doubly-linked list
    *(instance->prev_next) = instance->next;
    if (instance->next) {
        instance->next->prev_next = instance->prev_next;
    }
    return 0;
}

WEAK void halide_profiler_stack_peak_update(void *user_context,
                                            halide_profiler_instance_state *instance,
                                            uint64_t *f_values) {
    // Note: Update to the counter is done without grabbing the state's lock to
    // reduce lock contention. One potential issue is that other call that frees the
    // pipeline and function stats structs may be running in parallel. However, the
    // current desctructor (called on profiler shutdown) does not free the structs
    // unless user specifically calls halide_profiler_reset().

    // Update per-func memory stats
    for (int i = 0; i < instance->pipeline_stats->num_funcs; ++i) {
        if (f_values[i] != 0) {
            sync_compare_max_and_swap(&(instance->funcs[i]).stack_peak, f_values[i]);
        }
    }
}

WEAK void halide_profiler_memory_allocate(void *user_context,
                                          halide_profiler_instance_state *instance,
                                          int func_id,
                                          uint64_t incr) {
    using namespace Halide::Runtime::Internal::Synchronization;

    // It's possible to have 'incr' equal to zero if the allocation is not
    // executed conditionally.
    if (incr == 0) {
        return;
    }

    halide_abort_if_false(user_context, instance != nullptr);
    halide_abort_if_false(user_context, func_id >= 0);
    halide_abort_if_false(user_context, func_id < instance->pipeline_stats->num_funcs);

    halide_profiler_func_stats *func = &instance->funcs[func_id];

    // Note: Update to the counter is done without grabbing the state's lock to
    // reduce lock contention. One potential issue is that another call that
    // frees the pipeline and function stats structs may be running in
    // parallel. However, the current destructor (called on profiler shutdown)
    // does not free the structs unless user specifically calls
    // halide_profiler_reset().

    // Update per-instance memory stats
    atomic_add_fetch_sequentially_consistent(&instance->num_allocs, 1);
    atomic_add_fetch_sequentially_consistent(&instance->memory_total, incr);
    uint64_t p_mem_current = atomic_add_fetch_sequentially_consistent(&instance->memory_current, incr);
    sync_compare_max_and_swap(&instance->memory_peak, p_mem_current);

    // Update per-func memory stats
    atomic_add_fetch_sequentially_consistent(&func->num_allocs, 1);
    atomic_add_fetch_sequentially_consistent(&func->memory_total, incr);
    uint64_t f_mem_current = atomic_add_fetch_sequentially_consistent(&func->memory_current, incr);
    sync_compare_max_and_swap(&func->memory_peak, f_mem_current);
}

WEAK void halide_profiler_memory_free(void *user_context,
                                      halide_profiler_instance_state *instance,
                                      int func_id,
                                      uint64_t decr) {
    using namespace Halide::Runtime::Internal::Synchronization;

    // It's possible to have 'decr' equal to zero if the allocation is not
    // executed conditionally.
    if (decr == 0) {
        return;
    }

    halide_abort_if_false(user_context, instance != nullptr);
    halide_abort_if_false(user_context, func_id >= 0);
    halide_abort_if_false(user_context, func_id < instance->pipeline_stats->num_funcs);

    halide_profiler_func_stats *func = &instance->funcs[func_id];

    // Note: Update to the counter is done without grabbing the state's lock to
    // reduce lock contention. One potential issue is that other call that frees the
    // pipeline and function stats structs may be running in parallel. However, the
    // current destructor (called on profiler shutdown) does not free the structs
    // unless user specifically calls halide_profiler_reset().

    // Update per-pipeline memory stats
    atomic_sub_fetch_sequentially_consistent(&instance->memory_current, decr);

    // Update per-func memory stats
    atomic_sub_fetch_sequentially_consistent(&func->memory_current, decr);
}

WEAK void halide_profiler_report_unlocked(void *user_context, halide_profiler_state *s) {
    StringStreamPrinter<1024> sstr(user_context);

    int64_t (*compare_fs_fn)(halide_profiler_func_stats *a, halide_profiler_func_stats *b) = nullptr;

    const char *sort_str = getenv("HL_PROFILER_SORT");
    if (sort_str) {
        if (!strcmp(sort_str, "time")) {
            // Sort by descending time
            compare_fs_fn = [](halide_profiler_func_stats *a, halide_profiler_func_stats *b) -> int64_t {
                return (int64_t)b->time - (int64_t)a->time;
            };
        } else if (!strcmp(sort_str, "name")) {
            // Sort by ascending name
            compare_fs_fn = [](halide_profiler_func_stats *a, halide_profiler_func_stats *b) -> int64_t {
                return strcmp(a->name, b->name);
            };
        }
    }
    bool support_colors = false;
    const char *term = getenv("TERM");
    if (term) {
        // Check if the terminal supports colors
        if (strstr(term, "color") || strstr(term, "xterm")) {
            support_colors = true;
        }
    }

    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        float total_time = p->time / 1000000.0f;
        if (!p->runs) {
            continue;
        }
        sstr.clear();
        bool serial = p->active_threads_numerator == p->active_threads_denominator;
        float threads = p->active_threads_numerator / (p->active_threads_denominator + 1e-10);
        sstr << p->name << "\n"
             << " total time: " << total_time << " ms"
             << "  samples: " << p->samples
             << "  runs: " << p->runs
             << "  time per run: " << total_time / p->runs << " ms\n";
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
            int f_stats_count = 0;
            halide_profiler_func_stats **f_stats = (halide_profiler_func_stats **)__builtin_alloca(p->num_funcs * sizeof(halide_profiler_func_stats *));
            const char *substr_copy_to_device = " (copy to device)";
            const char *substr_copy_to_host = " (copy to host)";

            int max_func_name_length = 23;  // length of the section header
            int num_copy_to_device = 0;
            int num_copy_to_host = 0;

            uint64_t total_func_time = 0;
            uint64_t total_copy_to_device_time = 0;
            uint64_t total_copy_to_host_time = 0;
            for (int i = 0; i < p->num_funcs; i++) {
                halide_profiler_func_stats *fs = p->funcs + i;
                int name_len = strlen(fs->name);
                if (name_len > max_func_name_length) {
                    max_func_name_length = name_len;
                }
                if (strstr(fs->name, substr_copy_to_device)) {
                    num_copy_to_device++;
                    total_copy_to_device_time += fs->time;
                } else if (strstr(fs->name, substr_copy_to_host)) {
                    num_copy_to_host++;
                    total_copy_to_host_time += fs->time;
                } else {
                    total_func_time += fs->time;
                }
            }

            for (int i = 0; i < p->num_funcs; i++) {
                halide_profiler_func_stats *fs = p->funcs + i;

                // The first id is always a catch-all overhead slot (notably containing the asserts).
                // The second id is always the "wait for parallel tasks" slot.
                // Only report these time if it's non-zero
                if ((i == 0 || i == 1) && fs->time == 0) {
                    continue;
                }

                // These two ids are malloc and free. Don't print them if there
                // were no heap allocations.
                if ((i == 2 || i == 3) && p->num_allocs == 0) {
                    continue;
                }

                f_stats[f_stats_count++] = fs;
            }

            if (compare_fs_fn) {
                for (int i = 1; i < f_stats_count; i++) {
                    for (int j = i; j > 0 && compare_fs_fn(f_stats[j - 1], f_stats[j]) > 0; j--) {
                        auto *a = f_stats[j - 1];
                        auto *b = f_stats[j];
                        f_stats[j - 1] = b;
                        f_stats[j] = a;
                    }
                }
            }

            const auto print_time_and_percentage = [&sstr, p](uint64_t time, size_t &cursor, bool light) {
                float ft = time / (p->runs * 1000000.0f);
                if (ft < 10000) {
                    sstr << " ";
                }
                if (ft < 1000) {
                    sstr << " ";
                }
                if (ft < 100) {
                    sstr << " ";
                }
                if (ft < 10) {
                    sstr << " ";
                }
                sstr << ft;
                // We don't need 6 sig. figs.
                sstr.erase(3);
                sstr << "ms";
                cursor += 12;
                while (sstr.size() < cursor) {
                    sstr << " ";
                }

                int perthousand = 0;
                if (p->time != 0) {
                    perthousand = (1000 * time) / p->time;
                }
                sstr << "(";
                if (perthousand < 100) {
                    sstr << " ";
                }
                int percent = perthousand / 10;
                sstr << percent << "." << (perthousand - percent * 10) << "%)";
                if (!light) {
                    cursor += 10;
                    while (sstr.size() < cursor) {
                        sstr << " ";
                    }
                }
            };

            auto print_report_entry = [&](halide_profiler_func_stats *fs, const char *suffix_cut) {
                size_t cursor = 0;
                sstr.clear();

                sstr << "    " << fs->name;
                if (suffix_cut) {
                    sstr.erase(strlen(suffix_cut));
                }
                sstr << ": ";
                cursor += max_func_name_length + 7;
                while (sstr.size() < cursor) {
                    sstr << " ";
                }

                print_time_and_percentage(fs->time, cursor, false);

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
            };

            if (num_copy_to_host == 0 && num_copy_to_device == 0) {
                for (int i = 0; i < f_stats_count; i++) {
                    halide_profiler_func_stats *fs = f_stats[i];
                    print_report_entry(fs, nullptr);
                }
            } else {
                const auto print_section_header = [&](const char *name, uint64_t total_time) {
                    size_t cursor = 0;
                    sstr.clear();
                    sstr << "  ";
                    if (support_colors) {
                        sstr << "\033[90m\033[3m";
                        cursor += 9;
                    }
                    sstr << "[" << name << " ";
                    cursor += max_func_name_length + 7;
                    while (sstr.size() < cursor) {
                        sstr << ":";
                    }
                    print_time_and_percentage(total_time, cursor, true);
                    sstr << " ::::]";
                    if (support_colors) {
                        sstr << "\033[0m";
                    }
                    sstr << "\n";
                    halide_print(user_context, sstr.str());
                };

                print_section_header("funcs", total_func_time);
                for (int i = 0; i < f_stats_count; i++) {
                    halide_profiler_func_stats *fs = f_stats[i];
                    if (!strstr(fs->name, substr_copy_to_device) && !strstr(fs->name, substr_copy_to_host)) {
                        print_report_entry(fs, nullptr);
                    }
                }
                if (num_copy_to_device) {
                    print_section_header("buffer copies to device", total_copy_to_device_time);
                    for (int i = 0; i < f_stats_count; i++) {
                        halide_profiler_func_stats *fs = f_stats[i];
                        if (strstr(fs->name, substr_copy_to_device)) {
                            print_report_entry(fs, substr_copy_to_device);
                        }
                    }
                }
                if (num_copy_to_host) {
                    print_section_header("buffer copies to host", total_copy_to_host_time);
                    for (int i = 0; i < f_stats_count; i++) {
                        halide_profiler_func_stats *fs = f_stats[i];
                        if (strstr(fs->name, substr_copy_to_host)) {
                            print_report_entry(fs, substr_copy_to_host);
                        }
                    }
                }
            }
        }
    }
}

WEAK void halide_profiler_report(void *user_context) {
    halide_profiler_state *s = halide_profiler_get_state();
    LockProfiler lock(s);
    halide_profiler_report_unlocked(user_context, s);
}

WEAK void halide_profiler_reset_unlocked(halide_profiler_state *s) {
    while (s->pipelines) {
        halide_profiler_pipeline_stats *p = s->pipelines;
        s->pipelines = (halide_profiler_pipeline_stats *)(p->next);
        free(p->funcs);
        free(p);
    }
}

WEAK void halide_profiler_reset() {
    // WARNING: Do not call this method while any other halide
    // pipeline is running; halide_profiler_memory_allocate/free and
    // halide_profiler_stack_peak_update update the profiler pipeline's
    // state without grabbing the global profiler state's lock.
    halide_profiler_state *s = halide_profiler_get_state();
    LockProfiler lock(s);
    halide_abort_if_false(nullptr, s->instances == nullptr);
    halide_profiler_reset_unlocked(s);
}

#ifndef WINDOWS
__attribute__((destructor))
#endif
WEAK void
halide_profiler_shutdown() {
    using namespace Halide::Runtime::Internal::Synchronization;

    halide_profiler_state *s = halide_profiler_get_state();
    if (!s->sampling_thread) {
        return;
    }

    int one = 1;
    atomic_store_relaxed(&(s->shutdown), &one);

#if TIMER_PROFILING
    // Wait for timer interrupt to fire and notice things are shutdown.
    // volatile should be the right tool to use to wait for storage to be
    // modified in a signal handler.
    typedef struct halide_thread *temp_t;
    volatile temp_t *storage = (volatile temp_t *)&s->sampling_thread;
    while (*storage != nullptr) {
    }
#else
    halide_join_thread(s->sampling_thread);
    s->sampling_thread = nullptr;
#endif

    // The join_thread should have waited for any running instances to
    // terminate.
    halide_debug_assert(nullptr, s->instances == nullptr);

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
    // called during process shutdown. Hence this routine doesn't attmept
    // to clean up state as the destructor does on other platforms.

    // Print results. Avoid locking as it will cause problems and
    // nothing should be running.
    halide_profiler_report_unlocked(nullptr, s);
}
#endif
}  // namespace

}  // extern "C"
