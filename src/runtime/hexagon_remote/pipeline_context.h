#ifndef HALIDE_HEXAGON_REMOTE_PIPELINE_CONTEXT_H
#define HALIDE_HEXAGON_REMOTE_PIPELINE_CONTEXT_H

#include <qurt.h>

// We can't control the stack size on the thread which receives our
// FastRPC calls. To work around this, we make our own thread, and
// forward the calls to that thread.

typedef int (*pipeline_argv_t)(void **);

class PipelineContext {
    void *stack;
    qurt_thread_t thread;
    qurt_cond_t wakeup_thread;
    qurt_cond_t wakeup_caller;
    qurt_mutex_t work_mutex;
    qurt_mutex_t wakeup_master_mutex;

    // Shared state
    pipeline_argv_t function;
    void **args;
    int result;

    void thread_main() {
        while (true) {
            // Lock and wait for work.
            qurt_mutex_lock(&work_mutex);
            qurt_cond_wait(&wakeup_thread, &work_mutex);
            if (!function) {
                break;
            }
            result = function(args);
            qurt_mutex_unlock(&work_mutex);
            qurt_cond_signal(&wakeup_caller);
        }
        qurt_cond_signal(&wakeup_caller);
    }

    static void redirect_main(void *data) {
        static_cast<PipelineContext *>(data)->thread_main();
    }

public:
    PipelineContext(int stack_alignment, int stack_size)
        : stack(NULL), function(NULL), args(NULL) {
        qurt_mutex_init(&work_mutex);
        qurt_mutex_init(&wakeup_master_mutex);
        qurt_cond_init(&wakeup_thread);
        qurt_cond_init(&wakeup_caller);

        // Allocate the stack for this thread.
        stack = memalign(stack_alignment, stack_size);

        qurt_thread_attr_t thread_attr;
        qurt_thread_attr_init(&thread_attr);
        qurt_thread_attr_set_stack_addr(&thread_attr, stack);
        qurt_thread_attr_set_stack_size(&thread_attr, stack_size);
        qurt_thread_attr_set_priority(&thread_attr, 100);
        qurt_thread_create(&thread, &thread_attr, redirect_main, this);
    }

    ~PipelineContext() {
        // Running a null function kills the thread.
        run(NULL, NULL);

        int status;
        qurt_thread_join(thread, &status);

        free(stack);
    }

    int run(pipeline_argv_t function, void **args) {
        // get a lock and set up work for the worker.
        qurt_mutex_lock(&work_mutex);
        this->function = function;
        this->args = args;
        qurt_mutex_unlock(&work_mutex);
        // send a signal to the worker.
        qurt_cond_signal(&wakeup_thread);

        // Wait for the worker's signal that it is done.
        qurt_mutex_lock(&wakeup_master_mutex);
        qurt_cond_wait(&wakeup_caller, &wakeup_master_mutex);
        qurt_mutex_unlock(&wakeup_master_mutex);
        return result;
    }
};

#endif
