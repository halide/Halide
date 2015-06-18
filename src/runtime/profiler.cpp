#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "scoped_mutex_lock.h"

// Note: The profiler thread may out-live any valid user_context, or
// be used across many different user_contexts, so nothing it calls
// can depend on the user context.

extern "C" {
// Returns the address of the global halide_profiler state
WEAK halide_profiler_state *halide_profiler_get_state() {
    static halide_profiler_state s = {{{0}}, NULL, 1000000, 0, 0, false};
    return &s;
}
}

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_profiler_pipeline_stats *find_or_create_pipeline(const char *pipeline_name, int num_funcs, const char **func_names) {
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
        (halide_profiler_pipeline_stats *)halide_malloc(NULL, sizeof(halide_profiler_pipeline_stats));
    if (!p) return NULL;
    p->next = s->pipelines;
    p->name = pipeline_name;
    p->first_func_id = s->first_free_id;
    p->num_funcs = num_funcs;
    p->runs = 0;
    p->time = 0;
    p->samples = 0;
    p->funcs = (halide_profiler_func_stats *)halide_malloc(NULL, num_funcs * sizeof(halide_profiler_func_stats));
    if (!p->funcs) {
        halide_free(NULL, p);
        return NULL;
    }
    for (int i = 0; i < num_funcs; i++) {
        p->funcs[i].time = 0;
        p->funcs[i].name = func_names[i];
    }
    s->first_free_id += num_funcs;
    s->pipelines = p;
    return p;
}

WEAK void bill_func(halide_profiler_state *s, int func_id, uint64_t time) {
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
            p->funcs[func_id - p->first_func_id].time += time;
            p->time += time;
            p->samples++;
            return;
        }
        p_prev = p;
    }
    // panic!
    print(NULL)
        << "Internal error in profiler: \n"
        << "Could not find pipeline for func_id " << func_id << "\n"
        << "Pipelines:\n";
    for (halide_profiler_pipeline_stats *p = s->pipelines; p; p = (halide_profiler_pipeline_stats *)(p->next)) {
        print(NULL) << p->name << " : " << p->first_func_id << ", " << p->num_funcs << "\n";
    }
    error(NULL) << "Could not proceed.\n";
}

// TODO: make this something like halide_nanosleep so that it can be implemented per OS
extern "C" void usleep(int);

WEAK void *sampling_profiler_thread(void *) {
    halide_profiler_state *s = halide_profiler_get_state();

    // grab the lock
    halide_mutex_lock(&s->lock);

    while (s->current_func != halide_profiler_please_stop) {

        uint64_t t1 = halide_current_time_ns(NULL);
        uint64_t t = t1;
        while (1) {
            uint64_t t_now = halide_current_time_ns(NULL);
            int func = s->current_func;
            if (func == halide_profiler_please_stop) {
                break;
            } else if (func >= 0) {
               // Assume all time since I was last awake is due to
               // the currently running func.
               bill_func(s, func, t_now - t);
            }
            t = t_now;

            // Release the lock, sleep, reacquire.
            uint64_t sleep_ns = s->sleep_time;
            halide_mutex_unlock(&s->lock);
            usleep(sleep_ns/1000);
            halide_mutex_lock(&s->lock);
        }
    }

    s->started = false;

    halide_mutex_unlock(&s->lock);
    return NULL;
}

}}}

extern "C" {

// Returns a token identifying this pipeline instance.
WEAK int halide_profiler_pipeline_start(void *user_context,
                                        const char *pipeline_name,
                                        int num_funcs,
                                        const char **func_names) {
    halide_profiler_state *s = halide_profiler_get_state();

    ScopedMutexLock lock(&s->lock);

    if (!s->started) {
        halide_spawn_thread(user_context, sampling_profiler_thread, NULL);
        s->started = true;
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

WEAK void halide_profiler_report(void *user_context) {
    halide_profiler_state *s = halide_profiler_get_state();

    char line_buf[160];
    Printer<StringStreamPrinter, sizeof(line_buf)> sstr(user_context, line_buf);

    ScopedMutexLock lock(&s->lock);

    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        float t = p->time / 1000000.0f;
        if (!p->runs) continue;
        sstr.clear();
        sstr << p->name
             << "  total time: " << t << " ms"
             << "  samples: " << p->samples
             << "  runs: " << p->runs
             << "  time per run: " << t / p->runs << " ms\n";
        halide_print(user_context, sstr.str());
        if (p->time) {
            for (int i = 0; i < p->num_funcs; i++) {
                sstr.clear();
                halide_profiler_func_stats *fs = p->funcs + i;
                sstr << "  " << fs->name << ": ";
                while (sstr.size() < 25) sstr << " ";

                float ft = fs->time / (p->runs * 1000000.0f);
                sstr << ft << "ms";
                while (sstr.size() < 40) sstr << " ";

                int percent = fs->time / (p->time / 100);
                sstr << "(" << percent << "%)\n";

                halide_print(user_context, sstr.str());
            }
        }
    }
}

WEAK void halide_profiler_reset() {
    halide_profiler_state *s = halide_profiler_get_state();

    ScopedMutexLock lock(&s->lock);

    while (s->pipelines) {
        halide_profiler_pipeline_stats *p = s->pipelines;
        s->pipelines = (halide_profiler_pipeline_stats *)(p->next);
        halide_free(NULL, p->funcs);
        halide_free(NULL, p);
    }
    s->first_free_id = 0;
}

namespace {
__attribute__((destructor))
WEAK void halide_profiler_shutdown() {
    halide_profiler_state *s = halide_profiler_get_state();
    s->current_func = halide_profiler_please_stop;
    do {
        // Memory barrier.
        __sync_synchronize(&s->started,
                           &s->current_func);
    } while (s->started);
    s->current_func = halide_profiler_outside_of_halide;

    // print results
    halide_profiler_report(NULL);

    // free memory
    halide_profiler_reset();
}
}

WEAK void halide_profiler_pipeline_end(void *user_context, void *state) {
    ((halide_profiler_state *)state)->current_func = halide_profiler_outside_of_halide;
}

}
