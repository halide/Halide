#include "HalideRuntime.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

// Note: The profiler thread may out-live any valid user_context, or
// be used across many different user_contexts, so nothing it calls
// can depend on the user context.

#define DEFAULT_COUNTERS 0xc00c003

extern "C" {
// Returns the address of the global halide_profiler state
WEAK halide_profiler_state *halide_profiler_get_state() {
    #ifdef HARDWARE_COUNTERS
    static int fds[halide_profiler_hardware_counter_end] = {0};
    static halide_profiler_state s = {{{0}}, 1, 0, 0, 0, 0, NULL, NULL, fds, DEFAULT_COUNTERS};
    #else
    static halide_profiler_state s = {{{0}}, 1, 0, 0, 0, 0, NULL, NULL, NULL, 0};
    #endif
    return &s;
}
}

namespace Halide { namespace Runtime { namespace Internal {

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
    if (!p) return NULL;
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
    uint64_t bytes_per_func = sizeof(halide_profiler_func_stats);
    #ifdef HARDWARE_COUNTERS
    // Also allocate space to track the hardware counters
    bytes_per_func += sizeof(halide_profiler_hardware_counter) * halide_profiler_hardware_counter_end;
    #endif
    p->funcs = (halide_profiler_func_stats *)malloc(num_funcs * bytes_per_func);
    #ifdef HARDWARE_COUNTERS
    halide_profiler_hardware_counter *counters = (halide_profiler_hardware_counter *)(p->funcs + num_funcs);
    memset(counters, 0, num_funcs * sizeof(halide_profiler_hardware_counter) * halide_profiler_hardware_counter_end);
    #endif
    if (!p->funcs) {
        free(p);
        return NULL;
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
        #ifdef HARDWARE_COUNTERS
        p->funcs[i].hardware_counters = counters + i;
        #else
        p->funcs[i].hardware_counters = NULL;
        #endif
    }
    s->first_free_id += num_funcs;
    s->pipelines = p;
    return p;
}

#ifdef HARDWARE_COUNTERS
WEAK void bill_hardware_counters(halide_profiler_func_stats *f,
                                 uint64_t active_counters,
                                 halide_profiler_hardware_counter *before,
                                 halide_profiler_hardware_counter *after) {
    if (f->hardware_counters && active_counters) {
        halide_profiler_hardware_counter *dst = (halide_profiler_hardware_counter *)(f->hardware_counters);
        halide_profiler_hardware_counter *b = (halide_profiler_hardware_counter *)before;
        halide_profiler_hardware_counter *a = (halide_profiler_hardware_counter *)after;
        while (active_counters) {
            uint64_t i = __builtin_ctzl(active_counters);
            active_counters &= ~(((uint64_t)1) << i);

            // On a 64-core 4GHz machine, the counters that track
            // cycles are going to overflow for a pipeline that
            // runs for more than 2 years, so we don't bother
            // worrying about overflow of those, but we do discard
            // the samples where the value overflows.
            dst[i].enabled += a[i].enabled - b[i].enabled;
            if (a[i].count > b[i].count) {
                dst[i].running += a[i].running - b[i].running;
                dst[i].count += a[i].count - b[i].count;
            }
        }
    }
}

#if BITS_64
#define __NR_perf_event_open 298
#else
#define __NR_perf_event_open 336
#endif

extern "C" int syscall(int num, ...);
extern "C" ssize_t read(int, void *, size_t);
extern "C" int ioctl(int fd, unsigned long req, ...);
extern "C" int getpid();

WEAK long perf_event_open(struct perf_event_attr *hw_event, int pid,
                          int cpu, int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, hw_event, pid, cpu,
                   group_fd, flags);
}

// from perf_events.h
struct perf_event_attr {
    uint32_t type;
    uint32_t size;
    uint64_t config;
    union {
        uint64_t sample_period;
        uint64_t sample_freq;
    };
    uint64_t sample_type;
    uint64_t read_format;
    uint64_t
        disabled       :  1,
        inherit        :  1,
        pinned         :  1,
        exclusive      :  1,
        exclude_user   :  1,
        exclude_kernel :  1,
        exclude_hv     :  1,
        exclude_idle   :  1,
        mmap           :  1,
        comm           :  1,
        freq           :  1,
        inherit_stat   :  1,
        enable_on_exec :  1,
        task           :  1,
        watermark      :  1,
        precise_ip     :  2,
        mmap_data      :  1,
        sample_id_all  :  1,
        exclude_host   :  1,
        exclude_guest  :  1,
        exclude_callchain_kernel : 1,
        exclude_callchain_user   : 1,
        mmap2          :  1,
        comm_exec      :  1,
        use_clockid    :  1,
        context_switch :  1,
        __reserved_1   : 37;
    union {
        uint32_t wakeup_events;
        uint32_t wakeup_watermark;
    };
    uint32_t bp_type;
    union {
        uint64_t bp_addr;
        uint64_t config1;
    };
    union {
        uint64_t bp_len;
        uint64_t config2;
    };
    uint64_t branch_sample_type;
    uint64_t sample_regs_user;
    uint32_t sample_stack_user;
    int32_t clockid;
    uint64_t sample_regs_intr;
    uint32_t aux_watermark;
    uint32_t __reserved_2;
};

struct counter_info {
    const char *name;
    uint32_t type, config;
};
const static counter_info info[halide_profiler_hardware_counter_end] =
    {{"cpu_cycles"             , 0, 0},
     {"instructions"           , 0, 1},
     {"cache_refs"             , 0, 2},
     {"cache_misses"           , 0, 3},
     {"branches"               , 0, 4},
     {"branch_mispredictions"  , 0, 5},
     {"bus_cycles"             , 0, 6},
     {"stalled_cycles_frontend", 0, 7},
     {"stalled_cycles_backend" , 0, 8},
     {"cpu_clock"              , 1, 0},
     {"task_clock"             , 1, 1},
     {"page_faults"            , 1, 2},
     {"context_switches"       , 1, 3},
     {"migrations"             , 1, 4},
     {"l1d_read_accesses"      , 3, 0 | (0 << 8) | (0 << 16)},
     {"l1d_read_misses"        , 3, 0 | (0 << 8) | (1 << 16)},
     {"l1d_write_accesses"     , 3, 0 | (1 << 8) | (0 << 16)},
     {"l1d_write_misses"       , 3, 0 | (1 << 8) | (1 << 16)},
     {"l1d_prefetch_accesses"  , 3, 0 | (2 << 8) | (0 << 16)},
     {"l1d_prefetch_misses"    , 3, 0 | (2 << 8) | (1 << 16)},
     {"l1i_read_accesses"      , 3, 1 | (0 << 8) | (0 << 16)},
     {"l1i_read_misses"        , 3, 1 | (0 << 8) | (1 << 16)},
     {"l1i_write_accesses"     , 3, 1 | (1 << 8) | (0 << 16)},
     {"l1i_write_misses"       , 3, 1 | (1 << 8) | (1 << 16)},
     {"l1i_prefetch_accesses"  , 3, 1 | (2 << 8) | (0 << 16)},
     {"l1i_prefetch_misses"    , 3, 1 | (2 << 8) | (1 << 16)},
     {"llc_read_accesses"      , 3, 2 | (0 << 8) | (0 << 16)},
     {"llc_read_misses"        , 3, 2 | (0 << 8) | (1 << 16)},
     {"llc_write_accesses"     , 3, 2 | (1 << 8) | (0 << 16)},
     {"llc_write_misses"       , 3, 2 | (1 << 8) | (1 << 16)},
     {"llc_prefetch_accesses"  , 3, 2 | (2 << 8) | (0 << 16)},
     {"llc_prefetch_misses"    , 3, 2 | (2 << 8) | (1 << 16)},
     {"dtlb_read_accesses"     , 3, 3 | (0 << 8) | (0 << 16)},
     {"dtlb_read_misses"       , 3, 3 | (0 << 8) | (1 << 16)},
     {"dtlb_write_accesses"    , 3, 3 | (1 << 8) | (0 << 16)},
     {"dtlb_write_misses"      , 3, 3 | (1 << 8) | (1 << 16)},
     {"dtlb_prefetch_accesses" , 3, 3 | (2 << 8) | (0 << 16)},
     {"dtlb_prefetch_misses"   , 3, 3 | (2 << 8) | (1 << 16)},
     {"itlb_read_accesses"     , 3, 4 | (0 << 8) | (0 << 16)},
     {"itlb_read_misses"       , 3, 4 | (0 << 8) | (1 << 16)},
     {"itlb_write_accesses"    , 3, 4 | (1 << 8) | (0 << 16)},
     {"itlb_write_misses"      , 3, 4 | (1 << 8) | (1 << 16)},
     {"itlb_prefetch_accesses" , 3, 4 | (2 << 8) | (0 << 16)},
     {"itlb_prefetch_misses"   , 3, 4 | (2 << 8) | (1 << 16)},
     {"bpu_read_accesses"      , 3, 5 | (0 << 8) | (0 << 16)},
     {"bpu_read_misses"        , 3, 5 | (0 << 8) | (1 << 16)},
     {"bpu_write_accesses"     , 3, 5 | (1 << 8) | (0 << 16)},
     {"bpu_write_misses"       , 3, 5 | (1 << 8) | (1 << 16)},
     {"bpu_prefetch_accesses"  , 3, 5 | (2 << 8) | (0 << 16)},
     {"bpu_prefetch_misses"    , 3, 5 | (2 << 8) | (1 << 16)},
     {"node_read_accesses"     , 3, 6 | (0 << 8) | (0 << 16)},
     {"node_read_misses"       , 3, 6 | (0 << 8) | (1 << 16)},
     {"node_write_accesses"    , 3, 6 | (1 << 8) | (0 << 16)},
     {"node_write_misses"      , 3, 6 | (1 << 8) | (1 << 16)},
     {"node_prefetch_accesses" , 3, 6 | (2 << 8) | (0 << 16)},
     {"node_prefetch_misses"   , 3, 6 | (2 << 8) | (1 << 16)}};

WEAK void get_hardware_counters(halide_profiler_state *s,
                                halide_profiler_hardware_counter *counters) {
    // The counters may not be initialized
    if (s->hardware_counter_fds == NULL) return;

    static int pid = -1;

    if (pid < 0) {
        pid = getpid();
        // Check the environment variable to set the active mask
        if (const char *set = getenv("HL_PROFILER_COUNTERS")) {
            s->active_counters = 0; // Environment variable takes precedence.
            const char *start = set, *end = set;
            while (*end) {
                while (*end && *end != ',') end++;
                for (int i = 0; i < halide_profiler_hardware_counter_end; i++) {
                    bool match = true;
                    for (int j = 0; match && start + j < end; j++) {
                        match &= (start[j] == info[i].name[j]);
                    }
                    if (match) {
                        s->active_counters |= ((uint64_t)1) << i;
                    }
                }
                start = end+1;
                end = start;
            }
        }
    }

    uint64_t a = s->active_counters;

    for (int i = 0; i < halide_profiler_hardware_counter_end; i++, a >>= 1) {
        bool active = ((a & 1) == 1);
        int *fd = s->hardware_counter_fds + i;
        halide_profiler_hardware_counter *c = counters + i;
        if (active) {
            if (*fd == 0) {
                // Enable the counter
                struct perf_event_attr pe = {0};
                pe.size = sizeof(pe);
                pe.exclude_kernel = 1;
                pe.exclude_hv = 1;
                pe.type = info[i].type;
                pe.config = info[i].config;
                pe.read_format = 3; // enabled and running counters in addition to the value, so we can scale
                *fd = perf_event_open(&pe, pid, -1, -1, 0);
                ioctl(*fd, 9216, 0); // reset
                // ioctl(*fd, 9219, 0); // enable (already enabled because pe.disabled == 0)
            }
            if (*fd > 0) {
                read(*fd, c, sizeof(halide_profiler_hardware_counter));
            }
        } else if (*fd > 0) {
            // Disable the counter
            ioctl(*fd, 9217, 0); // disable
            close(*fd);
            *fd = 0;
        }
    }
}
#else
WEAK void bill_hardware_counters(halide_profiler_func_stats *,
                                 uint64_t,
                                 halide_profiler_hardware_counter *,
                                 halide_profiler_hardware_counter *) {
}
WEAK void get_hardware_counters(halide_profiler_state *,
                                halide_profiler_hardware_counter *) {
}
#endif

WEAK halide_profiler_func_stats *bill_func(halide_profiler_state *s, int func_id, uint64_t time, int active_threads) {
    halide_profiler_pipeline_stats *p_prev = NULL;
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
            return f;
        }
        p_prev = p;
    }
    // Someone must have called reset_state while a kernel was running. Do nothing.
    return NULL;
}

WEAK void sampling_profiler_thread(void *) {
    halide_profiler_state *s = halide_profiler_get_state();

    // grab the lock
    halide_mutex_lock(&s->lock);

    // Double-buffered log of the hardware counters
    halide_profiler_hardware_counter counters_buf[2][halide_profiler_hardware_counter_end];
    halide_profiler_hardware_counter *counters = &counters_buf[0][0];
    halide_profiler_hardware_counter *counters_now = &counters_buf[1][0];
    memset(counters_buf, 0, sizeof(counters_buf));

    while (s->current_func != halide_profiler_please_stop) {
        uint64_t t1 = halide_current_time_ns(NULL);
        uint64_t t = t1;
        get_hardware_counters(s, counters);
        while (1) {
            int func, active_threads;
            if (s->get_remote_profiler_state) {
                // Execution has disappeared into remote code running
                // on an accelerator (e.g. Hexagon DSP)
                s->get_remote_profiler_state(&func, &active_threads);
            } else {
                func = s->current_func;
                active_threads = s->active_threads;
            }
            uint64_t t_now = halide_current_time_ns(NULL);
            get_hardware_counters(s, counters_now);

            if (func == halide_profiler_please_stop) {
                break;
            } else if (func >= 0) {
                // Assume all time since I was last awake is due to
                // the currently running func.
                halide_profiler_func_stats *f = bill_func(s, func, t_now - t, active_threads);
                if (f) {
                    bill_hardware_counters(f, s->active_counters, counters, counters_now);
                }
            }
            t = t_now;

            halide_profiler_hardware_counter *tmp = counters;
            counters = counters_now;
            counters_now = tmp;

            // Release the lock, sleep, reacquire.
            int sleep_ms = s->sleep_time;
            halide_mutex_unlock(&s->lock);
            halide_sleep_ms(NULL, sleep_ms);
            halide_mutex_lock(&s->lock);
        }
    }



    halide_mutex_unlock(&s->lock);
}

}}}

namespace {

template <typename T>
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

}

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
    return NULL;
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
        s->sampling_thread = halide_spawn_thread(sampling_profiler_thread, NULL);
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
    halide_profiler_pipeline_stats *p_stats = (halide_profiler_pipeline_stats *) pipeline_state;
    halide_assert(user_context, p_stats != NULL);

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

    halide_profiler_pipeline_stats *p_stats = (halide_profiler_pipeline_stats *) pipeline_state;
    halide_assert(user_context, p_stats != NULL);
    halide_assert(user_context, func_id >= 0);
    halide_assert(user_context, func_id < p_stats->num_funcs);

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

    halide_profiler_pipeline_stats *p_stats = (halide_profiler_pipeline_stats *) pipeline_state;
    halide_assert(user_context, p_stats != NULL);
    halide_assert(user_context, func_id >= 0);
    halide_assert(user_context, func_id < p_stats->num_funcs);

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

namespace {
WEAK double extrapolate_counter(const halide_profiler_hardware_counter *c) {
    if (c->running == 0) return 0.0;
    return ((double)(c->count) * c->enabled) / c->running;
}
}

WEAK void halide_profiler_report_unlocked(void *user_context, halide_profiler_state *s) {

    char line_buf[1024];
    Printer<StringStreamPrinter, sizeof(line_buf)> sstr(user_context, line_buf);

    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        float t = p->time / 1000000.0f;
        if (!p->runs) continue;
        sstr.clear();
        int alloc_avg = 0;
        if (p->num_allocs != 0) {
            alloc_avg = p->memory_total/p->num_allocs;
        }
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
                if (i == 0 && fs->time == 0) continue;

                sstr << "  " << fs->name << ": ";
                cursor += 25;
                while (sstr.size() < cursor) sstr << " ";

                float ft = fs->time / (p->runs * 1000000.0f);
                sstr << ft;
                // We don't need 6 sig. figs.
                sstr.erase(3);
                sstr << "ms";
                cursor += 10;
                while (sstr.size() < cursor) sstr << " ";

                int percent = 0;
                if (p->time != 0) {
                    percent = (100*fs->time) / p->time;
                }
                sstr << "(" << percent << "%)";
                cursor += 8;
                while (sstr.size() < cursor) sstr << " ";

                if (!serial) {
                    float threads = fs->active_threads_numerator / (fs->active_threads_denominator + 1e-10);
                    sstr << "threads: " << threads;
                    sstr.erase(3);
                    cursor += 15;
                    while (sstr.size() < cursor) sstr << " ";
                }

                int alloc_avg = 0;
                if (fs->num_allocs != 0) {
                    alloc_avg = fs->memory_total/fs->num_allocs;
                }

                if (fs->memory_peak) {
                    cursor += 15;
                    sstr << " heap: " << fs->memory_peak;
                    while (sstr.size() < cursor) sstr << " ";
                    sstr << " allocs: " << fs->num_allocs;
                    cursor += 15;
                    while (sstr.size() < cursor) sstr << " ";
                    sstr << " avg: " << alloc_avg;
                }
                if (fs->stack_peak > 0) {
                    sstr << " stack: " << fs->stack_peak;
                }
                sstr << "\n";

                halide_print(user_context, sstr.str());

                #ifdef HARDWARE_COUNTERS
                sstr.clear();
                if (fs->hardware_counters) {
                    const halide_profiler_hardware_counter *h = fs->hardware_counters;
                    if (s->active_counters == DEFAULT_COUNTERS) {
                        const double instructions = extrapolate_counter(h + halide_profiler_hardware_counter_instructions);
                        const double cpu_cycles = extrapolate_counter(h + halide_profiler_hardware_counter_cpu_cycles);
                        const uint64_t ipc_times_100 = (uint64_t)((100.0 * instructions) / (cpu_cycles + 1e-20) + 0.5);
                        const uint64_t ipc_hundreds = ipc_times_100 / 100;
                        const uint64_t ipc_frac = ipc_times_100 - ipc_hundreds*100;
                        const uint64_t ipc_tens = ipc_frac / 10;
                        const uint64_t ipc_ones = ipc_frac % 10;
                        const double l1_accesses = extrapolate_counter(h + halide_profiler_hardware_counter_l1d_read_accesses);
                        const double l1_misses = extrapolate_counter(h + halide_profiler_hardware_counter_l1d_read_misses);
                        const uint64_t l1_hit_rate = 100 - (uint64_t)((100.0 * l1_misses) / (l1_accesses + 1e-20) + 0.5);
                        const double llc_accesses = extrapolate_counter(h + halide_profiler_hardware_counter_llc_read_accesses);
                        const double llc_misses = extrapolate_counter(h + halide_profiler_hardware_counter_llc_read_misses);
                        const uint64_t llc_hit_rate = 100 - (uint64_t)((100.0 * llc_misses) / (llc_accesses + 1e-20) + 0.5);

                        double l1_bytes = l1_accesses * 64.0;
                        double llc_bytes = llc_accesses * 64.0;

                        l1_bytes /= p->runs;
                        llc_bytes /= p->runs;

                        const char *suffixes[] = {"B", "K", "M", "G", "T", NULL};
                        int l1_suffix = 0, llc_suffix = 0;
                        while (l1_bytes >= 10000 && suffixes[l1_suffix + 1]) {
                            l1_suffix ++;
                            l1_bytes /= 1000;
                        }
                        while (llc_bytes >= 10000 && suffixes[llc_suffix + 1]) {
                            llc_suffix ++;
                            llc_bytes /= 1000;
                        }

                        sstr << "                      "
                             << "   IPC: " << ipc_hundreds << "." << ipc_tens << ipc_ones
                             << "   L1: " << (uint64_t)(l1_bytes + 0.5) << suffixes[l1_suffix] << " (" << l1_hit_rate << "%)"
                             << "   LLC: " << (uint64_t)(llc_bytes + 0.5) << suffixes[llc_suffix] << " (" << llc_hit_rate << "%)"
                             << "\n";
                        halide_print(user_context, sstr.str());
                    } else {
                        // Just print the counter values
                        uint64_t a = s->active_counters;
                        halide_profiler_hardware_counter *c = (halide_profiler_hardware_counter *)(fs->hardware_counters);
                        while (a) {
                            uint64_t i = __builtin_ctzl(a);
                            a &= ~(((uint64_t)1) << i);
                            if (s->hardware_counter_fds[i] <= 0) continue;
                            sstr << "                         " << info[i].name << ": ";
                            while (sstr.size() < 50) sstr << " ";
                            sstr << (extrapolate_counter(c + i) / p->runs) << "\n";
                            halide_print(user_context, sstr.str());
                            sstr.clear();
                        }
                    }
                }
                #endif
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
    if (s->hardware_counter_fds) {
        for (int i = 0; ;i++) {
            if (s->hardware_counter_fds[i] == 0) break;
            close(s->hardware_counter_fds[i]);
            s->hardware_counter_fds[i] = 0;
        }
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
WEAK void halide_profiler_shutdown() {
    halide_profiler_state *s = halide_profiler_get_state();
    if (!s->sampling_thread) {
        return;
    }

    s->current_func = halide_profiler_please_stop;
    halide_join_thread(s->sampling_thread);
    s->sampling_thread = NULL;
    s->current_func = halide_profiler_outside_of_halide;

    // Print results. No need to lock anything because we just shut
    // down the thread.
    halide_profiler_report_unlocked(NULL, s);

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
    halide_profiler_report_unlocked(NULL, s);
}
#endif
}

WEAK void halide_profiler_pipeline_end(void *user_context, void *state) {
    ((halide_profiler_state *)state)->current_func = halide_profiler_outside_of_halide;
}

} // extern "C"
