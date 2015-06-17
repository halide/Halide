#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "profiler_state.h"

extern "C" {
// Returns the address of the halide_profiler state
WEAK profiler_state *halide_profiler_get_state(void *user_context) {
    static profiler_state s;
    return &s;
}
}

namespace Halide { namespace Runtime { namespace Internal {

WEAK char *copy_string(void *user_context, const char *src) {
    int l = strlen(src) + 1;
    char *dst = (char *)halide_malloc(user_context, l);
    for (int i = 0; i < l; i++) {
        dst[i] = src[i];
    }
    return dst;
}

WEAK pipeline_stats *find_or_create_pipeline(void *user_context, const char *pipeline_name,
                                             int num_funcs, const char **func_names) {
    for (pipeline_stats *p = halide_profiler_get_state(user_context)->pipelines; p; p = (pipeline_stats *)(p->next)) {
        if (strcmp(p->name, pipeline_name) == 0 &&
            p->num_funcs == num_funcs) {
            return p;
        }
    }
    // Create a new pipeline stats entry.
    pipeline_stats *p = (pipeline_stats *)halide_malloc(user_context, sizeof(pipeline_stats));
    p->next = halide_profiler_get_state(user_context)->pipelines;
    halide_profiler_get_state(user_context)->pipelines = p;
    p->name = copy_string(user_context, pipeline_name);
    p->first_func_id = halide_profiler_get_state(user_context)->first_free_id;
    p->num_funcs = num_funcs;
    p->runs = 0;
    p->time = 0;
    p->samples = 0;
    p->funcs = (func_stats *)halide_malloc(user_context, num_funcs * sizeof(func_stats));
    for (int i = 0; i < num_funcs; i++) {
        p->funcs[i].time = 0;
        p->funcs[i].name = copy_string(user_context, func_names[i]);
    }
    halide_profiler_get_state(user_context)->first_free_id += num_funcs;
    return p;
}

WEAK void bill_func(void *user_context, int func_id, uint64_t time) {
    pipeline_stats *p_prev = NULL;
    for (pipeline_stats *p = halide_profiler_get_state(user_context)->pipelines; p; p = (pipeline_stats *)(p->next)) {
        if (func_id >= p->first_func_id && func_id < p->first_func_id + p->num_funcs) {
            if (p_prev) {
                // Bubble the pipeline to the top to speed up future queries.
                p_prev->next = (pipeline_stats *)(p->next);
                p->next = halide_profiler_get_state(user_context)->pipelines;
                halide_profiler_get_state(user_context)->pipelines = p;
            }
            p->funcs[func_id - p->first_func_id].time += time;
            p->time += time;
            p->samples++;
            return;
        }
        p_prev = p;
    }
    // panic!
    error(NULL) << "Internal error in profiler\n";
}

extern "C" int sched_yield();

WEAK void sampling_profiler_thread(void *user_context, void *) {
    // grab the lock
    halide_mutex_lock(&halide_profiler_get_state(user_context)->lock);

    while (halide_profiler_get_state(user_context)->current_func != please_stop) {

        uint64_t t1 = halide_current_time_ns(user_context);
        uint64_t t = t1;
        while (1) {
            uint64_t t_now = halide_current_time_ns(user_context);
            uint64_t delta = t_now - t;
            // Querying the state too frequently causes cache misses
            // in the main process every time it goes to set
            // current_func. If not much time has elapsed since we
            // last checked it, delay.
            if (delta > 100000) {
                int func = halide_profiler_get_state(user_context)->current_func;
                if (func == please_stop) {
                    break;
                } else if (func >= 0) {
                    // Assume all time since I was last awake is due to
                    // the currently running func.
                    bill_func(user_context, func, t_now - t);
                }
                t = t_now;
            }
            // Release the lock, give up my time slice, reacquire.
            halide_mutex_unlock(&halide_profiler_get_state(user_context)->lock);
            sched_yield();
            halide_mutex_lock(&halide_profiler_get_state(user_context)->lock);
        }
    }

    halide_profiler_get_state(user_context)->started = false;

    halide_mutex_unlock(&halide_profiler_get_state(user_context)->lock);
}

}}}

extern "C" {

// Returns a token identifying this pipeline instance.
WEAK int halide_profiler_pipeline_start(void *user_context,
                                        const char *pipeline_name,
                                        int num_funcs,
                                        const char **func_names) {
    halide_mutex_lock(&halide_profiler_get_state(user_context)->lock);

    if (!halide_profiler_get_state(user_context)->started) {
        halide_spawn_thread(user_context, sampling_profiler_thread, NULL);
        halide_profiler_get_state(user_context)->started = true;
    }

    pipeline_stats *p = find_or_create_pipeline(user_context, pipeline_name, num_funcs, func_names);
    p->runs++;

    int tok = p->first_func_id;

    halide_mutex_unlock(&halide_profiler_get_state(user_context)->lock);
    return tok;
}

WEAK void halide_profiler_report(void *user_context) {
    halide_mutex_lock(&halide_profiler_get_state(user_context)->lock);
    for (pipeline_stats *p = halide_profiler_get_state(user_context)->pipelines; p; p = (pipeline_stats *)(p->next)) {
        float t = p->time / 1000000.0f;
        print(NULL) << p->name
                    << "  total time: " << t << " ms"
                    << "  samples: " << p->samples
                    << "  runs: " << p->runs
                    << "  time per run: " << t / p->runs << " ms\n";
        if (p->time) {
            for (int i = 0; i < p->num_funcs; i++) {
                stringstream sstr(NULL);
                func_stats *fs = p->funcs + i;
                sstr << "  " << fs->name << ": ";
                while (sstr.size() < 25) sstr << " ";

                float ft = fs->time / (p->runs * 1000000.0f);
                sstr << ft << "ms";
                while (sstr.size() < 40) sstr << " ";

                int percent = fs->time / (p->time / 100);
                sstr << "(" << percent << "%)\n";

                halide_print(NULL, sstr.str());
            }
        }
    }
    halide_mutex_unlock(&halide_profiler_get_state(user_context)->lock);
}

WEAK void halide_profiler_reset(void *user_context) {
    halide_mutex_lock(&halide_profiler_get_state(user_context)->lock);

    while (halide_profiler_get_state(user_context)->pipelines) {
        pipeline_stats *p = halide_profiler_get_state(user_context)->pipelines;
        halide_profiler_get_state(user_context)->pipelines = (pipeline_stats *)(p->next);
        for (int i = 0; i < p->num_funcs; i++) {
            halide_free(user_context, p->funcs[i].name);
        }
        halide_free(user_context, p->funcs);
        halide_free(user_context, p->name);
        halide_free(user_context, p);
    }
    halide_profiler_get_state(user_context)->first_free_id = 0;
    halide_mutex_unlock(&halide_profiler_get_state(user_context)->lock);
}

namespace {
__attribute__((destructor))
WEAK void halide_profiler_shutdown() {
    halide_profiler_get_state(NULL)->current_func = please_stop;
    do {
        // Memory barrier.
        __sync_synchronize(&halide_profiler_get_state(NULL)->started,
                           &halide_profiler_get_state(NULL)->current_func);
    } while (halide_profiler_get_state(NULL)->started);
    halide_profiler_get_state(NULL)->current_func = outside_of_halide;

    // print results
    halide_profiler_report(NULL);

    // free memory
    halide_profiler_reset(NULL);
}
}

WEAK int halide_profiler_pipeline_end(void *user_context, int tok) {
    halide_profiler_get_state(user_context)->current_func = outside_of_halide;
    return 0;
}

}
