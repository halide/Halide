#ifndef HALIDE_HEXAGON_REMOTE_PIPELINE_CONTEXT_H
#define HALIDE_HEXAGON_REMOTE_PIPELINE_CONTEXT_H

#include <HalideRuntime.h>

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

    // Shared state
    pipeline_argv_t function;
    void **args;
    int result;
    bool running;

    void thread_main() {
        qurt_mutex_lock(&work_mutex);
        while (running) {
            qurt_cond_wait(&wakeup_thread, &work_mutex);
            if (function) {
                result = function(args);
                function = NULL;
                qurt_cond_signal(&wakeup_caller);
            }
        }
        qurt_mutex_unlock(&work_mutex);
    }

    static void redirect_main(void *data) {
        static_cast<PipelineContext *>(data)->thread_main();
    }

public:
    PipelineContext(int stack_alignment, int stack_size)
        : stack(NULL), function(NULL), args(NULL), running(true) {
        qurt_mutex_init(&work_mutex);
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
        qurt_mutex_lock(&work_mutex);
        running = false;
        qurt_cond_signal(&wakeup_thread);
        qurt_mutex_unlock(&work_mutex);

        int status;
        qurt_thread_join(thread, &status);

        qurt_cond_destroy(&wakeup_thread);
        qurt_cond_destroy(&wakeup_caller);
        qurt_mutex_destroy(&work_mutex);

        free(stack);
    }

    void set_priority(int priority) {
        if (priority > 0xFF) {
            priority = 0xFF;        // Clamp to max priority
        } else if (priority <= 0) {
            return;                 // Ignore settings of zero and below
        }
        qurt_thread_set_priority(thread, priority);
    }

    int run(pipeline_argv_t function, void **args) {
        // get a lock and set up work for the worker.
        qurt_mutex_lock(&work_mutex);
        this->function = function;
        this->args = args;
        // send a signal to the worker.
        qurt_cond_signal(&wakeup_thread);

        // Wait for the worker's signal that it is done.
        while (this->function != NULL) {
            qurt_cond_wait(&wakeup_caller, &work_mutex);
        }
        int result = this->result;
        qurt_mutex_unlock(&work_mutex);
        return result;
    }
};

#endif
