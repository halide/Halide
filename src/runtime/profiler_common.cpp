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
                                                             const int *func_parents) {
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
    debug(0) << "Clearing func stats\n";
    __builtin_memset(p->funcs, 0, func_stats_storage);
    for (int i = 0; i < num_funcs; i++) {
        p->funcs[i].name = (const char *)(func_names[i]);
        p->funcs[i].parent = func_parents[i];
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

// Print `str` to halide_print, wrapping so no line exceeds `max_cols`
// columns. Words are separated by spaces. The first line starts at
// column 0; every subsequent line is preceded by `indent` spaces (counted
// against max_cols). No trailing newline is added on the final line.
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
                                        uint64_t native_vector_bytes,
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
            find_or_create_pipeline(pipeline_name, num_funcs, func_names, func_parents);
        if (!p) {
            // Allocating space to track the statistics failed.
            return halide_error_out_of_memory(user_context);
        }
        p->native_vector_bytes = native_vector_bytes;

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
            func->memory_peak = max(func->memory_peak, instance_func->memory_peak);
            func->stack_peak = max(func->stack_peak, instance_func->stack_peak);
            uint64_t *counter = (&func->stack_peak) + 1;
            uint64_t *end = (uint64_t *)(func + 1);
            const uint64_t *instance = (&instance_func->stack_peak) + 1;
            while (counter != end) {
                *counter++ += *instance++;
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

    int64_t (*compare_fs_fn)(halide_profiler_func_stats *a, halide_profiler_func_stats *b) = nullptr;

    const char *sort_str = getenv("HL_PROFILER_SORT");
    if (sort_str) {
        if (!strcmp(sort_str, "time")) {
            compare_fs_fn = [](halide_profiler_func_stats *a, halide_profiler_func_stats *b) -> int64_t {
                return (int64_t)b->time - (int64_t)a->time;
            };
        } else if (!strcmp(sort_str, "name")) {
            compare_fs_fn = [](halide_profiler_func_stats *a, halide_profiler_func_stats *b) -> int64_t {
                return strcmp(a->name, b->name);
            };
        }
    }

    bool support_colors = false;
    const char *term = getenv("TERM");
    if (term && (strstr(term, "color") || strstr(term, "xterm"))) {
        support_colors = true;
    }

    const char *substr_copy_to_device = " (copy to device)";
    const char *substr_copy_to_host = " (copy to host)";

    // ---- Profiler report formatting --------------------------------------
    //
    // Column-aligned lines are produced from `const char *` templates. A run
    // of identical "marker" characters is a wildcard slot: the marker char
    // picks the slot type (and hence its formatter), and the run length is
    // the slot width. Anything else is literal text. So the templates
    // literally show the layout, including widths -- no width constants, no
    // named tokens. To widen a column, type more of the marker. To
    // rearrange columns, reorder marker runs.
    //
    // Markers:
    //   N name      T time      P percent     H active threads
    //   L loops     K tasks     A allocs      M mem peak / stack peak
    //   V mem avg   R recompute S section label
    //
    // (No lowercase letters are markers, so "|threads|", "  parallel   ",
    // "|recompute]", " ::::]", and the second-line column legend, all pass
    // through verbatim.)

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

    auto emit_percentage = [&](uint64_t numer, uint64_t denom, int width) {
        uint64_t target = sstr.size() + width;
        int perthousand = 0;
        if (denom != 0) {
            perthousand = (1000 * numer) / denom;
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

    // Section label slot: emits "label " then pads to width with `:`. If
    // the label is too long it spills past the slot, pushing later cells to
    // the right.
    auto emit_section_label = [&](const char *label, int width) {
        uint64_t target = sstr.size() + width;
        sstr << label << " ";
        pad_bytes_to(target, '.');
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

    // ---- Templates ----
    // The N slot in func_row absorbs its own 2-space indent, so it is the
    // same width as the S slot in the section headers; the templates line
    // up vertically. The literal column legend on the second line of
    // funcs_section_header is hand-aligned with the column widths on line 1
    // -- if you resize a column, eyeball the legend too. Trailing \n's are
    // added by the caller so color reset (when enabled) can be emitted just
    // before the final newline.
    constexpr const char *horiz_rule =
        "--------------------------------------------------------------------------------------------------------\n";
    constexpr const char *funcs_section_header =
        " SSSSSSSSSSSSSSSSSSSSSSS |TTTTTTTTT PPPPPPPP| active|  parallel   | heap | peak | avg  |recompute|notes|\n"
        "                         |                  |threads| loops| tasks|allocs|  mem |  mem |  ratio  |     |";
    constexpr const char *func_row =
        "NNNNNNNNNNNNNNNNNNNNNNNNN|TTTTTTTTT PPPPPPPP|HHHHHH |LLLLLL|KKKKKK|AAAAAA|MMMMMM|VVVVVV|RRRRRRRR |YYYYY|";
    constexpr const char *copy_section_header =
        " SSSSSSSSSSSSSSSSSSSSSSS |TTTTTTTTT PPPPPPPP|       |      |      |      |      |      |         |     |";

    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        if (!p->runs) {
            continue;
        }

        // Is the pipeline entirely serial?
        uint64_t total_parallel_loops = 0;
        uint64_t total_parallel_tasks = 0;
        for (int i = 0; i < p->num_funcs; i++) {
            total_parallel_loops += p->funcs[i].parallel_loops;
            total_parallel_tasks += p->funcs[i].parallel_tasks;
        }
        bool serial = total_parallel_loops == 0;

        // Pipeline summary (free-form, not column-aligned).
        {
            float total_ms = p->time / 1000000.0f;
            sstr.clear();
            emit_dim(horiz_rule);
            sstr << p->name << "\n"
                 << " total time: " << total_ms << " ms"
                 << "  samples: " << p->samples
                 << "  runs: " << p->runs
                 << "  time per run: " << total_ms / p->runs << " ms\n";
            if (!serial) {
                float threads = p->active_threads_numerator / (p->active_threads_denominator + 1e-10f);
                sstr << " average threads used: " << threads
                     << "  parallel loops: ";
                emit_si(total_parallel_loops / p->runs);
                sstr << "  parallel tasks: ";
                emit_si(total_parallel_tasks / p->runs);
                sstr << "\n";
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

        // Compute each func's depth in the compute_at tree (parent==-1 is a
        // root, otherwise parent is an index into p->funcs), a DFS traversal
        // of that tree, and whether each func is the last child of its
        // parent (used to draw the right tree-art glyph). The DFS order is
        // the default sort so children sit right under their parents; the
        // depth + is_last_sibling drive the per-row indent in emit_name.
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
            uint64_t time;
            uint64_t active_threads_numerator;
            uint64_t active_threads_denominator;
        };
        size_t cum_stats_size = p->num_funcs * sizeof(CumulativeStats);
        CumulativeStats *cum_stats = (CumulativeStats *)__builtin_alloca(cum_stats_size);
        __builtin_memset(cum_stats, 0, cum_stats_size);
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

        // Filter (skip empty bookkeeping slots) in tree-DFS order, and
        // optionally re-sort with the user-requested comparator.
        int f_stats_count = 0;
        halide_profiler_func_stats **f_stats =
            (halide_profiler_func_stats **)__builtin_alloca(p->num_funcs * sizeof(halide_profiler_func_stats *));

        int num_copy_to_device = 0;
        int num_copy_to_host = 0;
        uint64_t total_func_time = 0;
        uint64_t total_copy_to_device_time = 0;
        uint64_t total_copy_to_host_time = 0;
        for (int i = 0; i < p->num_funcs; i++) {
            halide_profiler_func_stats *fs = p->funcs + i;
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

        for (int t = 0; t < tree_count; t++) {
            int i = tree_order[t];
            halide_profiler_func_stats *fs = p->funcs + i;
            // ids 0,1 are overhead and "wait for parallel tasks" -- skip if zero.
            if ((i == 0 || i == 1) && fs->time == 0) {
                continue;
            }
            // ids 2,3 are malloc/free -- skip if no heap allocations happened.
            if ((i == 2 || i == 3) && p->num_allocs == 0) {
                continue;
            }
            f_stats[f_stats_count++] = fs;
        }

        if (compare_fs_fn) {
            // An explicit sort was requested -- the tree-DFS order is about
            // to be scrambled, so the depth-based indentation would no
            // longer correspond to the row layout. Flatten it.
            for (int i = 0; i < p->num_funcs; i++) {
                func_depth[i] = 0;
            }
            for (int i = 1; i < f_stats_count; i++) {
                for (int j = i; j > 0 && compare_fs_fn(f_stats[j - 1], f_stats[j]) > 0; j--) {
                    auto *a = f_stats[j - 1];
                    auto *b = f_stats[j];
                    f_stats[j - 1] = b;
                    f_stats[j] = a;
                }
            }
        }

        // ---- Heuristic warnings -----------------------------------------
        //
        // Many counters are recorded but only a few are shown in the table.
        // For everything else, the `rule` function below scans each func's
        // stats and emits a numbered warning when the schedule looks
        // suspicious. Each func's row gets a "notes" cell listing the
        // numbers of the warnings that apply; the messages themselves
        // print after all the sections.
        //
        // Each rule's trigger condition and message body live in the same
        // case, sharing locally-computed metrics. The function is called
        // with emit=false during the pre-scan to discover firings, and
        // again with emit=true to render each warning's text.

        struct WarningEntry {
            halide_profiler_func_stats *fs;
            CumulativeStats *cs;
            int rule_id;
        };
        constexpr int max_warnings = 256;
        WarningEntry *warnings = (WarningEntry *)__builtin_alloca(max_warnings * sizeof(WarningEntry));
        int num_warnings = 0;

        int num_threads = halide_get_num_threads();
        if (num_threads < 1) {
            num_threads = 1;
        }

        // Returns true if rule `rule_id` fires for `fs`. When `emit` is
        // true, also writes the warning message to sstr (using the same
        // metrics the trigger condition reads). To add a new rule, append
        // a new case here -- both the predicate and the text live together.
        auto rule = [&](halide_profiler_func_stats *fs,
                        CumulativeStats *cs,
                        int rule_id,
                        bool emit) -> bool {
            // Skip anything that takes less than 1% of total runtime (including all children).
            float frac_time = (float)cs->time / (float)(p->time + 1);
            if (frac_time < 0.01) {
                return false;
            }

            float threads_avg = cs->active_threads_numerator /
                                (cs->active_threads_denominator + 1e-10f);

            // fs->time is in ns; per-ms rate = count * 1e6 / ns.
            uint64_t tasks_per_run = fs->parallel_tasks / p->runs;
            uint64_t loops_per_run = fs->parallel_loops / p->runs;
            bool poor_thread_utilization = threads_avg < num_threads * 0.75f;
            float recompute = (fs->points_required_at_realization /
                               (fs->points_required_at_root + 1e-10f));
            uint64_t total_vector_loads = fs->vector_loads + fs->gathers;
            uint64_t total_vector_stores = fs->vector_stores + fs->scatters;
            uint64_t total_stores = fs->scalar_stores + fs->vector_stores + fs->scatters;
            bool vector_loads_or_stores = total_vector_loads + total_vector_stores != 0;
            float ms_per_task = (cs->time / (fs->parallel_tasks + 1e-10f)) * 1e-6f;

            switch (rule_id) {
            case 0:
                // Significant func with low thread utilization that's also
                // launching many parallel loops -- thread pool overhead is
                // probably eating the parallelism gains.
                if (poor_thread_utilization &&
                    loops_per_run > 1) {
                    if (emit) {
                        sstr << fs->name << " launches " << loops_per_run
                             << " parallel loops and shows poor utilization of the "
                             << "thread pool. Ensure the parallel loop is the outermost "
                             << "one. Fuse multiple nested parallel loops into one with "
                             << "Func::fuse. If this Func has multiple update stages, "
                             << "consider them wrapping them in a single parallel outer "
                             << "loop with Func::in.";
                    }
                    return true;
                }
                return false;
            case 1:
                // Very high task launch rate -- the inner loop is too
                // fine-grained, so per-task overhead drowns out the work.
                if (poor_thread_utilization &&
                    tasks_per_run > (uint64_t)num_threads * 4) {
                    if (emit) {
                        sstr << fs->name << " spawns " << tasks_per_run / loops_per_run
                             << " parallel tasks per parallel loop and shows poor utilization"
                             << " of the thread pool. The parallel loop may be too fine-grained."
                             << " Consider splitting it into a parallel outer loop and a serial"
                             << " inner loop. Each task currently takes " << ms_per_task << "ms.";
                    }
                    return true;
                }
                return false;
            case 2:
                // Too few parallel tasks per run to keep the thread pool
                // busy -- the loop is too coarse-grained.
                if (fs->parallel_tasks > 0 &&
                    tasks_per_run < (uint64_t)num_threads) {
                    if (emit) {
                        sstr << fs->name << "'s parallel loop has only " << tasks_per_run
                             << " task" << (tasks_per_run == 1 ? "" : "s")
                             << " per run, fewer than the " << num_threads
                             << " available threads; the loop may be too coarse-grained. "
                             << "Consider either splitting it more finely or finding other "
                             << "loops that can be parallel too. Each task currently takes "
                             << ms_per_task << "ms.";
                    }
                    return true;
                }
                return false;
            case 3:
                // Not parallelized at all
                if (!serial &&
                    fs->parent == -1 &&
                    fs->parallel_loops == 0) {
                    if (emit) {
                        sstr << fs->name << " is compute_root but not parallelized, while "
                             << "some other Funcs are. Consider parallelizing it.";
                    }
                    return true;
                }
                return false;
            default:
                // Continue through to next set
                break;
            }

            // For rules below here, we only care if the self time is more than 1%
            float self_time = fs->time / (float)p->time;
            if (self_time < 0.01) {
                return false;
            }

            switch (rule_id) {
            case 3:
                // High redundant recompute
                if (recompute > 2.0f &&
                    self_time > 0.01 &&
                    fs->parent >= 0) {
                    if (emit) {
                        // TODO: Silence this for wrapper Funcs
                        sstr << fs->name << " redundantly recomputes each value " << recompute
                             << " times on average. Consider a compute_at location further "
                             << "outwards in the parent's loop nest.";
                    }
                    return true;
                }
                return false;
            case 4:
                if (!vector_loads_or_stores) {
                    if (emit) {
                        sstr << fs->name << " performs no vector loads or stores. Ensure it is"
                             << " vectorized.";
                    }
                    return true;
                }
                return false;
            case 5:
                if (fs->gathers > fs->vector_loads) {
                    if (emit) {
                        sstr << fs->name << " performs more vector gathers than dense vector "
                             << "loads (";
                        emit_si(fs->gathers);
                        sstr << " vs ";
                        emit_si(fs->vector_loads);
                        sstr << "). It may be possible to improve performance by vectorizing "
                             << "a different Var, precomputing boundary conditions, or by "
                             << "reordering the storage layout of Funcs that this one calls.";
                    }
                    return true;
                }
                return false;
            case 6:
                if (fs->scatters > fs->vector_stores) {
                    if (emit) {
                        sstr << fs->name << " performs more vector scatters than dense vector "
                             << "stores (";
                        emit_si(fs->scatters);
                        sstr << " vs ";
                        emit_si(fs->vector_stores);
                        sstr << "). It may be possible to improve performance by vectorizing a "
                             << "different Var, or by reordering the storage layout of this Func.";
                    }
                    return true;
                }
                return false;
            case 7:
                if (vector_loads_or_stores &&
                    total_vector_stores <= fs->scalar_stores * 10) {
                    if (emit) {
                        sstr << "A significant fraction of the stores to " << fs->name << " are scalar: ";
                        emit_si(fs->scalar_stores);
                        sstr << " out of ";
                        emit_si(total_vector_stores + fs->scalar_stores);
                        sstr << ". There may be an update definition that was not vectorized.";
                    }
                    return true;
                }
                return false;
            case 8:
                if (total_vector_stores > fs->scalar_stores * 10 &&
                    fs->bytes_stored < total_vector_stores * p->native_vector_bytes) {
                    if (emit) {
                        sstr << "Stores to " << fs->name << " only write an average of "
                             << fs->bytes_stored / total_stores << " bytes each. "
                             << "This is less than the machine native vector width. Consider "
                             << "using wider vectors.";
                    }
                    return true;
                }
                return false;

            default:
                return false;
            }
        };
        constexpr int num_rules = 9;

        for (int f = 0; f < f_stats_count; f++) {
            halide_profiler_func_stats *fs = f_stats[f];
            int idx = (int)(fs - p->funcs);
            CumulativeStats *cs = cum_stats + idx;
            // Skip the bookkeeping slots (overhead, thread idle, malloc, free)
            // and the synthesized buffer-copy funcs -- the rules don't apply.
            if (idx < 4) {
                continue;
            }
            // Skip host-device copy entries
            if (strstr(fs->name, substr_copy_to_device) ||
                strstr(fs->name, substr_copy_to_host)) {
                continue;
            }
            for (int r = 0; r < num_rules; r++) {
                if (rule(fs, cs, r, /*emit=*/false) && num_warnings < max_warnings) {
                    warnings[num_warnings++] = {fs, cs, r};
                }
            }
        }

        // Func name slot. Draws the schedule's compute_at tree using
        // box-drawing glyphs in the same dark gray (xterm 238) used for the
        // column separators. One column per tree level:
        //   "  "    leading indent (so root names sit just past " [")
        //   "│"     ancestor whose subtree continues (more siblings to come)
        //   " "     ancestor whose subtree is finished
        //   "├"     current func, with siblings still to come
        //   "└"     current func, last sibling
        // Each glyph is 1 visible column but 3 UTF-8 bytes, and ANSI color
        // codes are non-visible too, so the byte target for the slot is
        // bumped by the count of non-visible bytes for each glyph emitted.
        auto emit_name = [&](halide_profiler_func_stats *fs, const char *suffix_cut, int width) {
            uint64_t target = sstr.size() + width;
            sstr << "  ";
            int idx = (int)(fs - p->funcs);
            int depth = func_depth[idx];
            if (depth > 0) {
                // Walk up the parent chain to record each ancestor (and the
                // func itself at the deepest slot).
                int lineage[64];
                int j = idx;
                for (int k = depth; k > 0; k--) {
                    lineage[k - 1] = j;
                    j = p->funcs[j].parent;
                }
                // Emit a single 1-column tree-art glyph in dark gray.
                // Returns the count of bytes emitted that don't count toward
                // visible width.
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
            if (suffix_cut) {
                sstr.erase(strlen(suffix_cut));
            }
            truncate_bytes_to(target);
            pad_bytes_to(target);
        };

        // Render a func-table row by walking `func_row` and dispatching on
        // the marker char of each run. Width is the run length.
        auto print_func_row = [&](halide_profiler_func_stats *fs,
                                  CumulativeStats *cs,
                                  const char *suffix_cut) {
            sstr.clear();
            apply_template(func_row, [&](char c, int w) {
                switch (c) {
                case 'N':
                    emit_name(fs, suffix_cut, w);
                    break;
                case 'T':
                    emit_time(fs->time, p->runs, w);
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
                case 'L':
                    emit_normalized_counter(fs->parallel_loops, p->runs, w);
                    break;
                case 'K':
                    emit_normalized_counter(fs->parallel_tasks, p->runs, w);
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
                case 'R':
                    if (fs->points_required_at_root) {
                        float ratio = fs->points_required_at_realization / (float)(fs->points_required_at_root);
                        emit_float(ratio, w);
                    } else {
                        pad_bytes_to(sstr.size() + w);
                    }
                    break;
                case 'Y': {
                    // Notes cell: comma-separated numbers of the warnings
                    // that apply to this func. Truncated if more would-be
                    // notes than the cell can hold.
                    uint64_t target = sstr.size() + w;
                    bool first = true;
                    for (int wi = 0; wi < num_warnings; wi++) {
                        if (warnings[wi].fs == fs) {
                            if (!first && sstr.size() + 1 < target) {
                                sstr << ",";
                            }
                            if (sstr.size() < target) {
                                sstr << (wi + 1);
                            }
                            first = false;
                        }
                    }
                    truncate_bytes_to(target);
                    pad_bytes_to(target);
                    break;
                }
                case '|':
                    // Column separator in a very dark gray (xterm 256-color
                    // 238) so it sits well behind the data values.
                    for (int i = 0; i < w; i++) {
                        if (support_colors) {
                            sstr << "\033[38;5;238m|\033[39m";
                        } else {
                            sstr << "|";
                        }
                    }
                    break;
                default:
                    emit_literal_run(c, w);
                    break;
                }
            });
            sstr << "\n";
            halide_print(user_context, sstr.str());
        };

        // Render a section header. ANSI color codes wrap the entire
        // (multi-line) header; they're applied here rather than via the
        // template so they don't perturb source-column alignment. While
        // rendering the header we suppress support_colors so emit_dim
        // doesn't double-up (the whole header is already dim italic).
        auto print_section_header = [&](const char *tmpl, const char *label, uint64_t section_time) {
            sstr.clear();
            bool saved = support_colors;
            if (saved) {
                sstr << "\033[38;5;245m\033[3m";
            }
            support_colors = false;
            apply_template(tmpl, [&](char c, int w) {
                switch (c) {
                case 'S':
                    emit_section_label(label, w);
                    break;
                case 'T':
                    emit_time(section_time, p->runs, w);
                    break;
                case 'P':
                    emit_percentage(section_time, p->time, w);
                    break;
                case '|':
                    // Match the very dark gray separators used in func rows.
                    // Switch back to the section header's color afterwards so
                    // the subsequent label text stays dim italic grey.
                    for (int i = 0; i < w; i++) {
                        if (saved) {
                            sstr << "\033[38;5;238m|\033[38;5;245m";
                        } else {
                            sstr << "|";
                        }
                    }
                    break;
                default:
                    emit_literal_run(c, w);
                    break;
                }
            });
            support_colors = saved;
            if (saved) {
                sstr << "\033[0m";
            }
            sstr << "\n";
            halide_print(user_context, sstr.str());
        };

        if (num_copy_to_host == 0 && num_copy_to_device == 0) {
            for (int i = 0; i < f_stats_count; i++) {
                halide_profiler_func_stats *fs = f_stats[i];
                CumulativeStats *cs = cum_stats + (fs - p->funcs);
                print_func_row(fs, cs, nullptr);
            }
        } else {
            print_section_header(funcs_section_header, "funcs", total_func_time);
            for (int i = 0; i < f_stats_count; i++) {
                halide_profiler_func_stats *fs = f_stats[i];
                CumulativeStats *cs = cum_stats + (fs - p->funcs);
                if (!strstr(fs->name, substr_copy_to_device) && !strstr(fs->name, substr_copy_to_host)) {
                    print_func_row(fs, cs, nullptr);
                }
            }
            if (total_copy_to_device_time) {
                print_section_header(copy_section_header, "copies to device", total_copy_to_device_time);
                for (int i = 0; i < f_stats_count; i++) {
                    halide_profiler_func_stats *fs = f_stats[i];
                    CumulativeStats *cs = cum_stats + (fs - p->funcs);
                    if (strstr(fs->name, substr_copy_to_device)) {
                        print_func_row(fs, cs, substr_copy_to_device);
                    }
                }
            }
            if (total_copy_to_host_time) {
                print_section_header(copy_section_header, "copies to host", total_copy_to_host_time);
                for (int i = 0; i < f_stats_count; i++) {
                    halide_profiler_func_stats *fs = f_stats[i];
                    CumulativeStats *cs = cum_stats + (fs - p->funcs);
                    if (strstr(fs->name, substr_copy_to_host)) {
                        print_func_row(fs, cs, substr_copy_to_host);
                    }
                }
            }
        }

        // ---- Warning messages -------------------------------------------
        // Render each warning collected during the pre-scan; the same
        // `rule` function used to detect firings now emits the message.
        // Wrap at the table width so warnings don't extend past the right
        // edge of the table above.

        // Warn if not enough pipeline samples
        bool too_few_samples = p->samples < 100;

        // Warn if more than 10% of the time was spent in unnamed Funcs and
        // there are at least three of them.
        uint64_t anon_time = 0;
        int anon_funcs = 0;
        for (int i = 0; i < p->num_funcs; i++) {
            const char *name = p->funcs[i].name;
            bool anon = name[0] == 'f';
            for (int i = 1; name[i]; i++) {
                anon &= name[i] >= '0' && name[i] <= '9';
            }
            if (anon) {
                anon_funcs++;
                anon_time += p->funcs[i].time;
            }
        }
        bool too_many_anon_funcs = anon_funcs >= 3 && anon_time * 10 > p->time;

        // Warn if the pipeline allocates at least 100MB and spends at least 10%
        // of its time freeing it.
        bool expensive_free =
            p->memory_peak > 100 * 1000 * 1000 &&
            p->funcs[3].time * 10 > p->time;

        if (num_warnings || too_few_samples || too_many_anon_funcs || expensive_free) {
            halide_print(user_context, " Performance warnings:\n");
            int max_cols = (int)strlen(func_row);
            // print_wrapped doesn't understand non-printing characters
            bool old = support_colors;
            support_colors = false;

            if (too_many_anon_funcs) {
                sstr.clear();
                sstr << "  - " << anon_funcs << " Funcs have auto-generated names and "
                     << "collectively take up a significant fraction of the total runtime. "
                     << "Consider giving them explicit names by passing a string to the "
                     << "Func constructor. This will make this profile easier to read.\n";
                print_wrapped(user_context, 4, max_cols, sstr.str());
            }
            if (too_few_samples) {
                sstr.clear();
                sstr << "  - Only " << p->samples
                     << " profiling samples taken. Consider running the "
                     << "pipeline more times in a loop for more accurate results.\n";
                print_wrapped(user_context, 4, max_cols, sstr.str());
            }
            if (expensive_free) {
                sstr.clear();
                sstr << "  - The pipeline allocates a significant amount of memory, and a "
                     << "lot of time is spent freeing it. Either fuse stages more aggressively "
                     << "to use less memory, or consider a using caching allocator with "
                     << "retention enabled to make freeing it cheaper.\n";
                print_wrapped(user_context, 4, max_cols, sstr.str());
            }
            for (int w = 0; w < num_warnings; w++) {
                sstr.clear();
                sstr << "  " << (w + 1) << ") ";
                rule(warnings[w].fs, warnings[w].cs, warnings[w].rule_id, /*emit=*/true);
                sstr << "\n";
                print_wrapped(user_context, 5, max_cols, sstr.str());
            }
            support_colors = old;
        }
        sstr.clear();
        emit_dim(horiz_rule);
        halide_print(user_context, sstr.str());
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
    // called during process shutdown. Hence this routine doesn't attempt
    // to clean up state as the destructor does on other platforms.

    // Print results. Avoid locking as it will cause problems and
    // nothing should be running.
    halide_profiler_report_unlocked(nullptr, s);
}
#endif
}  // namespace

}  // extern "C"
