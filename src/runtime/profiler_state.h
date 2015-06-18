// Struct definitions shared between the profiler runtime modules.
namespace Halide { namespace Runtime { namespace Internal {

struct func_stats {
    uint64_t time;
    const char *name;
};

struct pipeline_stats {
    uint64_t time;
    const char *name;
    func_stats *funcs;
    // The following field is a pipeline_stats ptr. However, this type
    // may not be recursive, or copying it from one LLVM module to
    // another goes into an infinite loop.
    void *next;
    int first_func_id, num_funcs;
    int runs;
    int samples;
};

struct profiler_state {
    halide_mutex lock;
    bool started;
    int first_free_id;
    int current_func;
    pipeline_stats *pipelines;
};

struct profiler_token {
    int *dst;
    int offset;
};

// func ids with special meaning
enum {outside_of_halide = -1, please_stop = -2};

}}}
