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

WEAK halide_profiler_pipeline_stats *find_or_create_pipeline(const char *pipeline_name,
                                                             int num_funcs,
                                                             const uint64_t *func_names,
                                                             const int *func_parents,
                                                             const int *func_canonical_ids,
                                                             const int *func_kinds,
                                                             const int *func_buffer_func_ids) {
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
    __builtin_memset(p, 0, sizeof(halide_profiler_pipeline_stats));
    p->next = s->pipelines;
    p->name = pipeline_name;
    p->num_funcs = num_funcs;
    size_t func_stats_storage = num_funcs * sizeof(halide_profiler_func_stats);
    p->funcs = (halide_profiler_func_stats *)malloc(func_stats_storage);
    if (!p->funcs) {
        free(p);
        return nullptr;
    }
    __builtin_memset(p->funcs, 0, func_stats_storage);
    for (int i = 0; i < num_funcs; i++) {
        p->funcs[i].name = (const char *)(func_names[i]);
        p->funcs[i].parent = func_parents[i];
        p->funcs[i].canonical_id = func_canonical_ids[i];
        p->funcs[i].kind = (halide_profiler_func_kind)func_kinds[i];
        p->funcs[i].buffer_func_id = func_buffer_func_ids[i];
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

// Word-wrap `str` to `max_cols` columns; every line after the first is
// preceded by `indent` spaces.
WEAK void print_wrapped(void *user_context, int indent, int max_cols, const char *str) {
    const char *start = str;
    char *line_buf = (char *)__builtin_alloca(max_cols + 2);
    int spaces = 0;
    while (true) {
        for (int i = 0; i < spaces; i++) {
            line_buf[i] = ' ';
        }
        int line_end = 0;
        for (int j = 0; j < max_cols - spaces; j++) {
            line_buf[spaces + j] = start[j];
            if (start[j] == '\0') {
                halide_print(user_context, line_buf);
                return;
            }
            if (start[j] == ' ') {
                line_end = spaces + j;
            }
        }
        if (line_end == 0) {
            line_end = max_cols;
        }
        line_buf[line_end] = '\n';
        line_buf[line_end + 1] = '\0';
        halide_print(user_context, line_buf);
        start += line_end - spaces + 1;
        spaces = indent;
    }
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
                                        const int *func_parents,
                                        const int *func_canonical_ids,
                                        const int *func_kinds,
                                        const int *func_buffer_func_ids,
                                        halide_profiler_instance_state *instance) {
    // Tell the instance where we stashed the per-func state - just after the
    // instance itself.

    // First check that the layout agrees with the amount of stack space
    // allocated in the pipeline
    static_assert((sizeof(halide_profiler_func_stats) & 7) == 0);
    halide_profiler_func_stats *funcs = (halide_profiler_func_stats *)(instance + 1);

    // Zero initialize the instance and func state
    __builtin_memset(instance, 0, (uint8_t *)(funcs + num_funcs) - (uint8_t *)instance);

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
            find_or_create_pipeline(pipeline_name, num_funcs,
                                    func_names, func_parents, func_canonical_ids,
                                    func_kinds, func_buffer_func_ids);
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

        // Retire the instance, accumulating statistics onto the statistics
        // for this pipeline. Memory and per-Func counter fields accumulate
        // regardless of whether the run produced samples — those counters
        // were updated by the Halide code itself, not by the sampler, so
        // they're valid even for unsampled runs.
        p->active_threads_numerator += instance->active_threads_numerator;
        p->active_threads_denominator += instance->active_threads_denominator;
        p->memory_total += instance->memory_total;
        p->memory_peak = max(p->memory_peak, instance->memory_peak);
        p->num_allocs += instance->num_allocs;
        p->runs++;
        p->samples += instance->samples;

        // Per-Func *counter* fields accumulate every run. Per-Func *time*
        // fields only get a meaningful contribution from runs that the
        // sampler actually hit (billed_time > 0): the time-accounting math
        // in this function relies on a non-zero billed_time, and a run
        // that completes between two sampler ticks has nothing useful to
        // contribute. Including such runs in p->time would make the sum
        // of fs->time across Funcs less than p->time, breaking percentage
        // math in the report.

        // First, the per-Func counter fields. Their layout: stack_peak is
        // a per-instance "max" field; everything after it is a uint64_t
        // counter we sum across instances.
        for (int f = 0; f < p->num_funcs; f++) {
            halide_profiler_func_stats *func = p->funcs + f;
            const halide_profiler_func_stats *instance_func = instance->funcs + f;
            func->memory_peak = max(func->memory_peak, instance_func->memory_peak);
            func->stack_peak = max(func->stack_peak, instance_func->stack_peak);
            uint64_t *counter = (&func->stack_peak) + 1;
            uint64_t *end = (uint64_t *)(func + 1);
            const uint64_t *instance = (&instance_func->stack_peak) + 1;
            while (counter != end) {
                *counter++ += *instance++;
            }
        }

        // Then per-Func time, only for runs the sampler reached. Compute
        // an adjustment factor to account for the fact that the billed
        // time is not exactly the duration between start and end calls.
        // We could avoid this by force-sampling at the start and end of
        // the pipeline, but that would overcount whatever the last value
        // of current_func happens to be at the end, and undercount time
        // spent in the first Func. Sampling events need to happen
        // independently (in the random-variable sense) of any changes to
        // current_func.
        if (instance->billed_time > 0) {
            p->time += true_duration;
            p->billed_runs++;
            double adjustment = (double)true_duration / instance->billed_time;
            for (int f = 0; f < p->num_funcs; f++) {
                halide_profiler_func_stats *func = p->funcs + f;
                const halide_profiler_func_stats *instance_func = instance->funcs + f;
                // clang-tidy wants me to use a c standard library function
                // to do the rounding below, but those aren't guaranteed to
                // be available when compiling the runtime.
                func->time += (uint64_t)(instance_func->time * adjustment + 0.5);  // NOLINT
            }
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
    // current destructor (called on profiler shutdown) does not free the structs
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

    bool support_colors = false;
    const char *term = getenv("TERM");
    if (term && (strstr(term, "color") || strstr(term, "xterm"))) {
        support_colors = true;
    }

    // Column-aligned rows are produced from `const char *` templates. A
    // run of an uppercase marker char is a slot — the marker picks the
    // formatter (see apply_template's switch below), and the run length
    // is the slot width. Anything else is literal. To widen a column,
    // type more of the marker.

    auto pad_bytes_to = [&](uint64_t target, char pad_char = ' ') {
        char buf[2] = {pad_char, 0};
        while (sstr.size() < target) {
            sstr << buf;
        }
    };

    auto truncate_bytes_to = [&](uint64_t target) {
        if (sstr.size() > target) {
            sstr.erase(sstr.size() - target);
        }
    };

    auto emit_literal_run = [&](char c, int n) {
        char buf[2] = {c, 0};
        for (int i = 0; i < n; i++) {
            sstr << buf;
        }
    };

    // Emit `text` with the SGR "faint" attribute when the terminal supports
    // it, otherwise plain. Faint composes with whatever foreground color is
    // active (so it works correctly inside the dim-italic section header
    // too). Returns the number of ANSI bytes emitted -- bytes that don't
    // count toward visible column width, so callers add it to their pad
    // target to keep columns aligned.
    auto emit_dim = [&](const char *text) -> uint64_t {
        if (support_colors) {
            uint64_t before = sstr.size();
            sstr << "\033[2m" << text << "\033[22m";
            return sstr.size() - before - strlen(text);
        }
        sstr << text;
        return 0;
    };

    // Each emit_* below fills exactly `width` visible bytes.

    // (emit_name lives inside the per-pipeline loop below; it needs access
    // to the pipeline's tree structure to draw the indent.)

    auto emit_time = [&](uint64_t time_ns, uint32_t runs, int width) {
        uint64_t target = sstr.size() + width;
        float val = time_ns / (runs * 1000000.0f);
        // Switch from ms to s above 1 s so values fit a tighter column.
        const char *unit = "ms";
        if (val >= 1000) {
            val /= 1000.0f;
            unit = "s ";
        }
        if (val < 1000) {
            sstr << " ";
        }
        if (val < 100) {
            sstr << " ";
        }
        if (val < 10) {
            sstr << " ";
        }
        sstr << val;
        sstr.erase(4);  // halide_double_to_string emits 6 decimals; trim to 2.
        target += emit_dim(unit);
        pad_bytes_to(target);
    };

    auto emit_percentage = [&](uint64_t numerator, uint64_t denom, int width) {
        uint64_t target = sstr.size() + width;
        int perthousand = 0;
        if (denom != 0) {
            perthousand = (1000 * numerator) / denom;
        }
        sstr << "(";
        if (perthousand < 100) {
            sstr << " ";
        }
        int percent = perthousand / 10;
        sstr << percent << "." << (perthousand - percent * 10) << "%)";
        pad_bytes_to(target);
    };

    // SI-suffixed integer with no padding, for free-form summary text.
    // Matches emit_counter's scaling but emits no leading or trailing padding
    // and shows zero as "0" rather than blank.
    auto emit_si = [&](uint64_t x) {
        const char *suffixes[] = {"", "K", "M", "G", "T", "P", "E"};
        int scale = 0;
        while (x >= 10000) {
            scale++;
            x = (x + 499) / 1000;
        }
        sstr << x;
        if (scale > 0) {
            emit_dim(suffixes[scale]);
        }
    };

    // SI-suffixed counter (10000 -> 10K, 1e6 -> 1.0M, ...). Zero is blank.
    auto emit_counter = [&](uint64_t x, int width) {
        uint64_t target = sstr.size() + width;
        if (x) {
            const char *suffixes[] = {" ", "K", "M", "G", "T", "P", "E"};
            for (int i = 6; i < width; i++) {
                sstr << " ";
            }
            int scale = 0;
            while (x >= 10000) {
                scale++;
                x = (x + 499) / 1000;
            }
            for (uint64_t y = x; y < 10000; y *= 10) {
                sstr << " ";
            }
            sstr << x;
            target += emit_dim(suffixes[scale]);
        }
        pad_bytes_to(target);
    };

    // Positive float, up to two decimal places. Falls back to emit_counter
    // for values that don't fit.
    auto emit_float = [&](float x, int width) {
        if (x >= 10000) {
            emit_counter((uint64_t)x, width);
            return;
        }
        uint64_t target = sstr.size() + width;
        int left_pad = width - 7;
        left_pad += x < 10;
        left_pad += x < 100;
        left_pad += x < 1000;
        pad_bytes_to(sstr.size() + left_pad);
        sstr << x;
        pad_bytes_to(target);
        truncate_bytes_to(target);
    };

    // A counter accumulated over `runs` runs. Renders the per-run value if
    // constant per run, otherwise the average. Zero is blank.
    auto emit_normalized_counter = [&](uint64_t x, uint32_t runs, int width) {
        if (x % runs == 0) {
            emit_counter(x / runs, width);
        } else {
            emit_float((float)x / runs, width);
        }
    };

    // Walk `tmpl`, calling `resolve(c, n)` for each maximal run of
    // identical chars. The resolver decides what to emit.
    auto apply_template = [&](const char *tmpl, auto resolve) {
        while (*tmpl) {
            char c = *tmpl;
            int n = 0;
            while (tmpl[n] == c) {
                n++;
            }
            resolve(c, n);
            tmpl += n;
        }
    };

    constexpr const char *horiz_rule =
        "--------------------------------------------------------------------------------------------------------\n";
    constexpr const char *func_row =
        "NNNNNNNNNNNNNNNNNNNNNNNNN|TTTTTTTTT PPPPPPPP|HHHHHH |AAAAAA|MMMMMM|VVVVVV|";
    constexpr const char *allocation_func_row =
        "NNNNNNNNNNNNNNNNNNNNNNNNN|ZZZZZZZZZZZZZZZZZZ|       |AAAAAA|MMMMMM|VVVVVV|";
    // Hand-aligned with func_row above; resize together.
    constexpr const char *column_legend_row_1 =
        "  name                   | time     percent | active| heap | peak | avg  |";
    constexpr const char *column_legend_row_2 =
        "                         |                  |threads|allocs|  mem |  mem |";

    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        if (!p->runs) {
            continue;
        }

        // Pipeline summary (free-form, not column-aligned). Times are
        // averaged over billed_runs (runs that produced samples), not
        // total runs — see halide_profiler_instance_end for why.
        {
            float total_ms = p->time / 1000000.0f;
            int time_runs = p->billed_runs ? p->billed_runs : 1;
            float threads = p->active_threads_numerator / (p->active_threads_denominator + 1e-10f);
            sstr.clear();
            emit_dim(horiz_rule);
            sstr << p->name << "\n"
                 << " total time: " << total_ms << " ms"
                 << "  samples: " << p->samples
                 << "  runs: " << p->runs;
            if (p->billed_runs != p->runs) {
                sstr << " (" << p->billed_runs << " timed)";
            }
            sstr << "  time per run: " << total_ms / time_runs << " ms\n";
            if (threads > 1.01f) {
                sstr << " average threads used: " << threads << "\n";
            }
            sstr << " heap allocations: " << p->num_allocs
                 << "  peak heap usage: ";
            emit_si(p->memory_peak);
            sstr << "\n";
            halide_print(user_context, sstr.str());
        }

        // Decide whether to emit the per-func table at all.
        bool print_f_states = p->time || p->memory_total;
        if (!print_f_states) {
            for (int i = 0; i < p->num_funcs; i++) {
                if (p->funcs[i].stack_peak) {
                    print_f_states = true;
                    break;
                }
            }
        }
        if (!print_f_states) {
            continue;
        }

        // DFS over the compute_at tree (parent == -1 is a root). The
        // order becomes the row order; depth + is_last_sibling drive
        // the tree-art glyphs in emit_name.
        int *func_depth = (int *)__builtin_alloca(p->num_funcs * sizeof(int));
        int *tree_order = (int *)__builtin_alloca(p->num_funcs * sizeof(int));
        bool *visited = (bool *)__builtin_alloca(p->num_funcs * sizeof(bool));
        bool *is_last_sibling = (bool *)__builtin_alloca(p->num_funcs * sizeof(bool));
        __builtin_memset(visited, 0, p->num_funcs * sizeof(bool));
        int tree_count = 0;
        auto dfs = [&](auto &self, int parent_idx, int depth) -> void {
            int last = -1;
            for (int i = 0; i < p->num_funcs; i++) {
                if (p->funcs[i].parent == parent_idx && !visited[i]) {
                    last = i;
                }
            }
            for (int i = 0; i < p->num_funcs; i++) {
                if (p->funcs[i].parent == parent_idx && !visited[i]) {
                    visited[i] = true;
                    func_depth[i] = depth;
                    is_last_sibling[i] = (i == last);
                    tree_order[tree_count++] = i;
                    self(self, i, depth + 1);
                }
            }
        };
        dfs(dfs, -1, 0);
        // Any orphans (e.g. parent points outside the array) get appended at
        // the end at depth 0 rather than being silently dropped.
        for (int i = 0; i < p->num_funcs; i++) {
            if (!visited[i]) {
                func_depth[i] = 0;
                is_last_sibling[i] = true;
                tree_order[tree_count++] = i;
            }
        }

        // Use the tree order to compute some cumulative stats
        struct CumulativeStats {
            // Time taken by this func and all children
            uint64_t time;
            // Average threads active for this func and all children
            uint64_t active_threads_numerator;
            uint64_t active_threads_denominator;
        };
        size_t cum_stats_size = p->num_funcs * sizeof(CumulativeStats);
        CumulativeStats *cum_stats = (CumulativeStats *)__builtin_alloca(cum_stats_size);
        __builtin_memset(cum_stats, 0, cum_stats_size);
        // Propagation to parents
        for (int i = p->num_funcs - 1; i >= 0; i--) {
            int j = tree_order[i];
            cum_stats[j].time += p->funcs[j].time;
            cum_stats[j].active_threads_numerator += p->funcs[j].active_threads_numerator;
            cum_stats[j].active_threads_denominator += p->funcs[j].active_threads_denominator;
            int parent = p->funcs[j].parent;
            if (parent >= 0) {
                cum_stats[parent].time += cum_stats[j].time;
                cum_stats[parent].active_threads_numerator += cum_stats[j].active_threads_numerator;
                cum_stats[parent].active_threads_denominator += cum_stats[j].active_threads_denominator;
            }
        }

        // Rows to print, in tree-DFS order, skipping bookkeeping slots
        // that would be noise (no time, no allocs).
        int f_stats_count = 0;
        const halide_profiler_func_stats **f_stats =
            (const halide_profiler_func_stats **)__builtin_alloca(p->num_funcs * sizeof(const halide_profiler_func_stats *));

        for (int t = 0; t < tree_count; t++) {
            int i = tree_order[t];
            const halide_profiler_func_stats *fs = p->funcs + i;
            if ((fs->kind == halide_profiler_func_kind_overhead ||
                 fs->kind == halide_profiler_func_kind_thread_idle) &&
                fs->time == 0) {
                continue;
            }
            if ((fs->kind == halide_profiler_func_kind_malloc ||
                 fs->kind == halide_profiler_func_kind_free) &&
                p->num_allocs == 0) {
                continue;
            }
            f_stats[f_stats_count++] = fs;
        }

        // Func name slot, including a tree-art indent. One column per
        // tree level: │ continues an ancestor's subtree; ├/└ are this
        // row's connector. Glyphs and ANSI escapes contribute zero
        // visible width but multiple bytes, hence the byte-target dance.
        auto emit_name = [&](const halide_profiler_func_stats *fs, int width) {
            uint64_t target = sstr.size() + width;
            sstr << "  ";
            int idx = (int)(fs - p->funcs);
            int depth = func_depth[idx];
            if (depth > 0) {
                int lineage[64];
                int j = idx;
                for (int k = depth; k > 0; k--) {
                    lineage[k - 1] = j;
                    j = p->funcs[j].parent;
                }
                // Returns the count of bytes emitted that don't count
                // toward visible width.
                auto emit_glyph = [&](const char *glyph) -> uint64_t {
                    uint64_t before = sstr.size();
                    if (support_colors) {
                        sstr << "\033[38;5;238m" << glyph << "\033[39m";
                    } else {
                        sstr << glyph;
                    }
                    return sstr.size() - before - 1;
                };
                for (int k = 0; k < depth - 1; k++) {
                    if (is_last_sibling[lineage[k]]) {
                        sstr << " ";
                    } else {
                        target += emit_glyph("\xe2\x94\x82");  // │
                    }
                }
                if (is_last_sibling[lineage[depth - 1]]) {
                    target += emit_glyph("\xe2\x94\x94");  // └
                } else {
                    target += emit_glyph("\xe2\x94\x9c");  // ├
                }
            }
            sstr << fs->name;
            truncate_bytes_to(target);
            pad_bytes_to(target);
        };

        auto print_func_row = [&](const halide_profiler_func_stats *fs,
                                  const CumulativeStats *cs) {
            sstr.clear();
            const char *row_template = func_row;
            if (fs->kind == halide_profiler_func_kind_allocation) {
                row_template = allocation_func_row;
            }
            apply_template(row_template, [&](char c, int w) {
                switch (c) {
                case 'N':
                    emit_name(fs, w);
                    break;
                case 'T':
                    emit_time(fs->time, p->billed_runs ? p->billed_runs : 1, w);
                    break;
                case 'P':
                    emit_percentage(fs->time, p->time, w);
                    break;
                case 'H':
                    if (cs->time) {
                        // NB: cumulative
                        float t = (cs->active_threads_numerator /
                                   (cs->active_threads_denominator + 1e-10f));
                        emit_float(t, w);
                    } else {
                        pad_bytes_to(sstr.size() + w);
                    }
                    break;
                case 'A':
                    emit_normalized_counter(fs->num_allocs, p->runs, w);
                    break;
                case 'M':
                    emit_counter(fs->num_allocs ? fs->memory_peak : fs->stack_peak, w);
                    break;
                case 'V':
                    if (fs->num_allocs) {
                        emit_counter(fs->memory_total / fs->num_allocs, w);
                    } else {
                        pad_bytes_to(sstr.size() + w);
                    }
                    break;
                case '|':
                    // Column separator, dimmed so the data stands out.
                    for (int i = 0; i < w; i++) {
                        if (support_colors) {
                            sstr << "\033[38;5;238m\xe2\x94\x82\033[39m";
                        } else {
                            sstr << "\xe2\x94\x82";
                        }
                    }
                    break;
                case 'Z': {
                    // Centered "(allocation)" placeholder in the time
                    // column for hoist_storage allocation rows.
                    uint64_t target = sstr.size() + w;
                    const char *text = "(allocation)";
                    int text_len = 0;
                    while (text[text_len]) {
                        text_len++;
                    }
                    int pad_left = (w > text_len) ? (w - text_len) / 2 : 0;
                    for (int i = 0; i < pad_left; i++) {
                        sstr << " ";
                    }
                    if (support_colors) {
                        uint64_t before = sstr.size();
                        sstr << "\033[38;5;245m" << text << "\033[39m";
                        target += (sstr.size() - before) - text_len;
                    } else {
                        sstr << text;
                    }
                    truncate_bytes_to(target);
                    pad_bytes_to(target);
                    break;
                }
                default:
                    emit_literal_run(c, w);
                    break;
                }
            });
            sstr << "\n";
            halide_print(user_context, sstr.str());
        };

        auto print_legend_row = [&](const char *row) {
            sstr.clear();
            if (support_colors) {
                sstr << "\033[38;5;245m\033[3m";
            }
            char buf[2] = {0, 0};
            for (const char *c = row; *c; c++) {
                if (*c == '|') {
                    if (support_colors) {
                        sstr << "\033[38;5;238m\xe2\x94\x82\033[38;5;245m";
                    } else {
                        sstr << "\xe2\x94\x82";
                    }
                } else {
                    buf[0] = *c;
                    sstr << buf;
                }
            }
            if (support_colors) {
                sstr << "\033[0m";
            }
            sstr << "\n";
            halide_print(user_context, sstr.str());
        };
        print_legend_row(column_legend_row_1);
        print_legend_row(column_legend_row_2);

        for (int i = 0; i < f_stats_count; i++) {
            const halide_profiler_func_stats *fs = f_stats[i];
            const CumulativeStats *cs = cum_stats + (fs - p->funcs);
            print_func_row(fs, cs);
        }

        sstr.clear();
        emit_dim(horiz_rule);
        halide_print(user_context, sstr.str());
    }

    if (const char *raw_str = getenv("HL_PROFILER_JSON_OUTPUT")) {
        // Dump the raw stats to a JSON file for offline analysis.
        void *f = halide_fopen(raw_str, "w");
        if (f) {
            StringStreamPrinter<4096> json(user_context);

            auto flush = [&]() {
                if (json.size() > 0) {
                    fwrite(json.str(), json.size(), 1, f);
                    json.clear();
                }
            };

            // Emit a JSON-escaped string literal (handles " and \).
            auto str = [&](const char *s) {
                json << "\"";
                char one[2] = {0, 0};
                for (const char *q = s; *q; q++) {
                    if (*q == '"' || *q == '\\') {
                        json << "\\";
                    }
                    one[0] = *q;
                    json << one;
                }
                json << "\"";
            };

            // Helper for emitting a uint64 counter as a JSON field.
            auto field_u64 = [&](const char *indent, const char *name, uint64_t v, bool last = false) {
                json << indent;
                str(name);
                json << ": " << v;
                json << (last ? "\n" : ",\n");
            };
            auto field_i = [&](const char *indent, const char *name, int v, bool last = false) {
                json << indent;
                str(name);
                json << ": " << v;
                json << (last ? "\n" : ",\n");
            };
            auto field_str = [&](const char *indent, const char *name, const char *v, bool last = false) {
                json << indent;
                str(name);
                json << ": ";
                str(v);
                json << (last ? "\n" : ",\n");
            };

            json << "{\n  \"pipelines\": [";
            bool first_pipeline = true;
            for (halide_profiler_pipeline_stats *pp = s->pipelines; pp;
                 pp = (halide_profiler_pipeline_stats *)(pp->next)) {
                json << (first_pipeline ? "\n" : ",\n");
                first_pipeline = false;

                json << "    {\n";
                field_str("      ", "name", pp->name);
                field_i("      ", "runs", pp->runs);
                field_i("      ", "billed_runs", pp->billed_runs);
                field_i("      ", "samples", pp->samples);
                field_i("      ", "num_allocs", pp->num_allocs);
                field_u64("      ", "time_ns", pp->time);
                field_u64("      ", "memory_current", pp->memory_current);
                field_u64("      ", "memory_peak", pp->memory_peak);
                field_u64("      ", "memory_total", pp->memory_total);
                field_u64("      ", "active_threads_numerator", pp->active_threads_numerator);
                field_u64("      ", "active_threads_denominator", pp->active_threads_denominator);
                json << "      \"funcs\": [";

                for (int i = 0; i < pp->num_funcs; i++) {
                    json << (i == 0 ? "\n" : ",\n");
                    const halide_profiler_func_stats *fs = &pp->funcs[i];
                    json << "        {\n";
                    field_str("          ", "name", fs->name);
                    field_i("          ", "parent", fs->parent);
                    field_i("          ", "canonical_id", fs->canonical_id);
                    field_i("          ", "kind", fs->kind);
                    field_i("          ", "buffer_func_id", fs->buffer_func_id);
                    field_u64("          ", "time_ns", fs->time);
                    field_u64("          ", "memory_current", fs->memory_current);
                    field_u64("          ", "memory_peak", fs->memory_peak);
                    field_u64("          ", "memory_total", fs->memory_total);
                    field_u64("          ", "stack_peak", fs->stack_peak);
                    field_u64("          ", "active_threads_numerator", fs->active_threads_numerator);
                    field_u64("          ", "active_threads_denominator", fs->active_threads_denominator);
                    field_u64("          ", "num_allocs", fs->num_allocs, true);
                    json << "        }";

                    // Flush periodically so we don't overflow the buffer for
                    // pipelines with many funcs.
                    if (json.size() > 2048) {
                        flush();
                    }
                }
                json << "\n      ]\n";
                json << "    }";
            }
            json << (first_pipeline ? "" : "\n") << "  ]\n}\n";
            flush();
            fclose(f);
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
    // called during process shutdown. Hence this routine doesn't attempt
    // to clean up state as the destructor does on other platforms.

    // Print results. Avoid locking as it will cause problems and
    // nothing should be running.
    halide_profiler_report_unlocked(nullptr, s);
}
#endif
}  // namespace

}  // extern "C"
