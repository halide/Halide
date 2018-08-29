#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <condition_variable>
#include <queue>
#include <mutex>
#include <future>

// This code is 64-bit x86 only due to inline asm for coroutine implementation.
#ifdef __x86_64__

#include "HalideRuntime.h"
#include "HalideBuffer.h"
#include "halide_benchmark.h"
#include "async_coroutine.h"

// This test runs an async pipeline that requires multiple execution
// contexts using a single OS thread and coroutines. We start with a
// basic x86-64 coroutine implementation.

struct execution_context {
    char *stack_bottom = NULL;
    char *stack = NULL;
    int priority = 0;

    // Used to ensure we only have one thread in a context at a
    // time. Two threads executing on the same stack is bad.
    bool occupied = true;
};

// Track the number of context switches
int context_switches = 0;

void switch_context(execution_context *from, execution_context *to) {
    context_switches++;

    //printf("Context switch from %p to %p\n", from, to);

    from->occupied = false;
    assert(!to->occupied);
    to->occupied = true;

    // To switch contexts, we'll push a return address onto our own
    // stack, switch to the target stack, and then issue a ret
    // instruction, which will pop the desired return address off the
    // target stack and jump to it.
    asm volatile (
        // We need to save all callee-saved registers, plus any
        // registers that might be used inside this function after the
        // asm block. The caller of switch_context will take care of
        // caller-saved registers. Saving all GPRs is more than
        // sufficient.
        "pushq %%rax\n"
        "pushq %%rbx\n"
        "pushq %%rcx\n"
        "pushq %%rdx\n"
        "pushq %%rbp\n"
        "pushq %%rsi\n"
        "pushq %%rdi\n"
        "pushq %%r8\n"
        "pushq %%r9\n"
        "pushq %%r10\n"
        "pushq %%r11\n"
        "pushq %%r12\n"
        "pushq %%r13\n"
        "pushq %%r14\n"
        "pushq %%r15\n"
        "leaq return_loc%=(%%rip), %%r15\n"
        "pushq %%r15\n"
        "movq 8(%%rsp), %%r15\n"
        "movq %%rsp, (%0)\n" // Save the stack pointer for the 'from' context
        "movq %1, %%rsp\n"   // Restore the stack pointer for the 'to' context
        "ret\n"              // Return into the 'to' context
        "return_loc%=:\n"    // When we re-enter the 'from' context we start here
        "popq %%r15\n"       // Restore all registers
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%r11\n"
        "popq %%r10\n"
        "popq %%r9\n"
        "popq %%r8\n"
        "popq %%rdi\n"
        "popq %%rsi\n"
        "popq %%rbp\n"
        "popq %%rdx\n"
        "popq %%rcx\n"
        "popq %%rbx\n"
        "popq %%rax\n"
        : // No outputs
        : "r"(&(from->stack)), "r"(to->stack)
        : "memory"
        );
}

// Track the number of stacks allocated
int stacks_allocated = 0;
int stacks_high_water = 0;

void call_in_new_context(execution_context *from,
                         execution_context *to,
                         void (*f)(execution_context *, execution_context *, void *),
                         void *arg) {
    // Allocate a 128k stack
    size_t sz = 128 * 1024;
    to->stack_bottom = (char *)malloc(sz);
    stacks_allocated++;
    stacks_high_water = std::max(stacks_allocated, stacks_high_water);

    // Zero it to aid debugging
    memset(to->stack_bottom, 0, sz);

    // Set up the stack pointer
    to->stack = to->stack_bottom + sz;
    to->stack = (char *)(((uintptr_t)to->stack) & ~((uintptr_t)(15)));

    //printf("Calling %p(%p, %p, %p) in a new context at %p\n", f, from, to, arg, to->stack);

    from->occupied = false;
    to->occupied = true;

    // Switching to a new context is much like switching to an
    // existing one, except we have to set up some arguments and we
    // use a callq instruction instead of a ret.
    asm volatile (
        "pushq %%rax\n"
        "pushq %%rbx\n"
        "pushq %%rcx\n"
        "pushq %%rdx\n"
        "pushq %%rbp\n"
        "pushq %%rsi\n"
        "pushq %%rdi\n"
        "pushq %%r8\n"
        "pushq %%r9\n"
        "pushq %%r10\n"
        "pushq %%r11\n"
        "pushq %%r12\n"
        "pushq %%r13\n"
        "pushq %%r14\n"
        "pushq %%r15\n"
        "leaq return_loc%=(%%rip), %%r15\n"
        "pushq %%r15\n"
        "movq 8(%%rsp), %%r15\n"
        "movq %%rsp, (%0)\n" // Save the stack pointer for the 'from' context
        "movq %1, %%rsp\n"   // Restore the stack pointer for the 'to' context
        "movq %2, %%rdi\n"   // Set the args for the function call
        "movq %3, %%rsi\n"
        "movq %4, %%rdx\n"
        "callq *%5\n"        // Call the function inside the 'to' context
        "int $3\n"           // The function should never return, instead it should switch contexts elsewhere.
        "return_loc%=:\n"    // When we re-enter the 'from' context we start here
        "popq %%r15\n"       // Restore all registers
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%r11\n"
        "popq %%r10\n"
        "popq %%r9\n"
        "popq %%r8\n"
        "popq %%rdi\n"
        "popq %%rsi\n"
        "popq %%rbp\n"
        "popq %%rdx\n"
        "popq %%rcx\n"
        "popq %%rbx\n"
        "popq %%rax\n"
        : // No outputs
        : "r"(&(from->stack)), "r"(to->stack), "r"(from), "r"(to), "r"(arg), "r"(f)
        : "memory", "rdi", "rsi", "rdx"
        );
}

// That's the end of the coroutines implementation. Next we need a
// task scheduler and semaphore implementation that plays nice with
// them.

// We'll throw one big lock around this whole thing. It's only
// released by a thread when inside of Halide code.
halide_mutex big_lock = { { 0 } };
halide_cond wake_workers = { { 0 } };

struct my_semaphore {
    int count = 0;
    execution_context *waiter = nullptr;
};

// We'll use a priority queue of execution contexts to decide what to
// schedule next.
struct compare_contexts {
    bool operator()(execution_context *a, execution_context *b) const {
        return a->priority < b->priority;
    }
};

std::priority_queue<execution_context *,
                    std::vector<execution_context *>,
                    compare_contexts> runnable_contexts;

// Instead of returning, finished contexts push themselves here and
// switch contexts to the scheduler. I would make them clean
// themselves up, but it's hard to free your own stack while you're
// executing on it.
std::vector<execution_context *> dead_contexts;

// Contexts for idle worker threads to hang out in
std::vector<execution_context *> idle_worker_contexts;

// The scheduler execution context. Switch to this when stalled.
execution_context scheduler_context;
void scheduler(execution_context *parent, execution_context *this_context, void *arg) {
    // The first time this is called is just to set up the scheduler's
    // context, so we immediately transfer control back to the parent.
    switch_context(this_context, parent);

    while (1) {
        // Clean up any finished contexts
        for (execution_context *ctx: dead_contexts) {
            if (ctx->stack_bottom) {
                stacks_allocated--;
                free(ctx->stack_bottom);
            }
            delete ctx;
        }
        dead_contexts.clear();

        // Run the next highest-priority context
        execution_context *next;
        if (!runnable_contexts.empty()) {
            next = runnable_contexts.top();
            runnable_contexts.pop();
        } else {
            if (idle_worker_contexts.empty()) {
                printf("Out of idle worker contexts!\n");
                abort();
            }
            // There's nothing interesting to do, go become an idle worker
            next = idle_worker_contexts.back();
            idle_worker_contexts.pop_back();
        }
        if (!runnable_contexts.empty()) {
            //printf("Waking a worker\n");
            halide_cond_signal(&wake_workers);
        }
        switch_context(this_context, next);
    }
}

// Implementations of the required Halide semaphore calls
int semaphore_init(halide_semaphore_t *s, int count) {
    my_semaphore *sema = (my_semaphore *)s;
    sema->count = count;
    sema->waiter = nullptr;
    return count;
}


int semaphore_release_already_locked(halide_semaphore_t *s, int count) {
    my_semaphore *sema = (my_semaphore *)s;
    sema->count += count;
    if (sema->waiter && sema->count > 0) {
        // Re-enqueue the blocked context
        runnable_contexts.push(sema->waiter);
        sema->waiter = nullptr;
    }
    return sema->count;
}

int semaphore_release(halide_semaphore_t *s, int count) {
    halide_mutex_lock(&big_lock);
    int result = semaphore_release_already_locked(s, count);
    halide_mutex_unlock(&big_lock);
    return result;
}

// A blocking version of semaphore acquire that enters the task system
void semaphore_acquire(execution_context *this_context, halide_semaphore_t *s, int count) {
    my_semaphore *sema = (my_semaphore *)s;
    while (sema->count < count) {
        if (sema->waiter) {
            // We don't generate IR with competing acquires
            printf("Semaphore contention %p vs %p!\n", sema->waiter, this_context);
            abort();
        }
        sema->waiter = this_context;
        switch_context(this_context, &scheduler_context);
    }
    sema->count -= count;
}

struct do_one_task_arg {
    halide_parallel_task_t *task;
    halide_semaphore_t *completion_semaphore;
};

// Do one of the tasks in a do_parallel_tasks call. Intended to be
// called in a fresh context.
void do_one_task(execution_context *parent, execution_context *this_context, void *arg) {
    do_one_task_arg *task_arg = (do_one_task_arg *)arg;
    halide_parallel_task_t *task = task_arg->task;
    halide_semaphore_t *completion_sema = task_arg->completion_semaphore;
    this_context->priority = -(task->min_threads);

    // Treat all loops as serial for now.
    for (int i = task->min; i < task->min + task->extent; i++) {
        // Try to acquire the semaphores
        for (int j = 0; j < task->num_semaphores; j++) {
            //printf("Acquiring task semaphore\n");
            semaphore_acquire(this_context, task->semaphores[j].semaphore, task->semaphores[j].count);
        }
        halide_mutex_unlock(&big_lock);
        task->fn(nullptr, i, 1, task->closure, nullptr);
        halide_mutex_lock(&big_lock);
    }
    semaphore_release_already_locked(completion_sema, 1);
    dead_contexts.push_back(this_context);
    switch_context(this_context, &scheduler_context);
    printf("Scheduled dead context!\n");
    abort();
}

int do_par_tasks(void *user_context, int num_tasks, halide_parallel_task_t *tasks, void *parent_pass_through) {
    // We're leaving Halide code, so grab the lock until we return
    halide_mutex_lock(&big_lock);

    // Make this context schedulable.
    execution_context *this_context = new execution_context;
    for (int i = 0; i < num_tasks; i++) {
        this_context->priority -= tasks[i].min_threads;
    }

    // Make a semaphore to wake this context when the children are done.
    halide_semaphore_t parent_sema;
    halide_semaphore_init(&parent_sema, 1 - num_tasks);

    // Queue up the children, switching directly to the context of
    // each. Run each up until the first stall.
    for (int i = 0; i < num_tasks; i++) {
        execution_context *ctx = new execution_context;
        do_one_task_arg arg = {tasks + i, &parent_sema};
        runnable_contexts.push(this_context);
        call_in_new_context(this_context, ctx, do_one_task, &arg);
    }

    // Wait until the children are done.
    //printf("Acquiring parent semaphore\n");
    semaphore_acquire(this_context, &parent_sema, 1);

    halide_mutex_unlock(&big_lock);

    // Re-entering Halide code
    return 0;
}

int main(int argc, char **argv) {
    Halide::Runtime::Buffer<int> out(16, 16, 16);

    printf("Getting baseline time.\n");

    // Get a baseline runtime.
    // TODO: this shouldn't deadlock when done with one thread, but sometimes it does!
    double reference_time =
        Halide::Tools::benchmark(3, 3, [&]() {
                printf("Running benchmark...\n");
                async_coroutine(out);
    });

    printf("Installing custom parallel runtime.\n");

    // Now install a custom parallel runtime
    halide_set_custom_parallel_runtime(
        nullptr, // This pipeline shouldn't call do_par_for
        nullptr, // our custom runtime never calls do_task
        halide_default_do_loop_task, // default is fine
        do_par_tasks,
        semaphore_init,
        nullptr, // our custom runtime never calls try_acquire
        semaphore_release);

    // Start up the scheduler
    printf("Starting scheduler context\n");
    execution_context root_context;
    halide_mutex_lock(&big_lock);
    call_in_new_context(&root_context, &scheduler_context, scheduler, nullptr);
    printf("Scheduler running...\n");

    printf("Starting worker threads\n");

    // Add some worker threads to the mix
    std::vector<std::future<void>> futures;
    bool done = false;
    for (int i = 1; i < halide_set_num_threads(0); i++) {
        futures.push_back(
            std::async(std::launch::async, [&](int i) {
                    halide_mutex_lock(&big_lock);
                    execution_context worker_context;
                    while (!done) {
                        idle_worker_contexts.push_back(&worker_context);
                        switch_context(&worker_context, &scheduler_context);
                        if (done) break;
                        halide_cond_wait(&wake_workers, &big_lock);
                    }
                    halide_mutex_unlock(&big_lock);
                }, i));
    }

    double custom_time;
    auto work = [&]() {
        printf("Entering Halide\n");
        custom_time =
            Halide::Tools::benchmark(3, 3, [&]() {
                halide_mutex_unlock(&big_lock);
                async_coroutine(out);
                halide_mutex_lock(&big_lock);
            });
        printf("Left Halide\n");
        done = true;
        halide_cond_broadcast(&wake_workers);
        halide_mutex_unlock(&big_lock);
    };
    std::async(std::launch::async, work).get();

    // Join the workers.
    while (!futures.empty()) {
        futures.back().get();
        futures.pop_back();
    }

    printf("Validating result\n");

    out.for_each_element([&](int x, int y, int z) {
            int correct = 8*(x + y + z);
            if (out(x, y, z) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(-1);
            }
        });

    printf("Context switches: %d\n", context_switches);
    printf("Max stacks allocated: %d\n", stacks_high_water);
    printf("Stacks still allocated: %d (1 expected)\n", stacks_allocated);
    free(scheduler_context.stack_bottom);
    if (stacks_high_water > 50) {
        printf("Runaway stack allocation!\n");
        return -1;
    }
    if (stacks_allocated != 1) {
        printf("Zombie stacks\n");
        return -1;
    }

    printf("Default threadpool time: %f\n", reference_time);
    printf("Custom threadpool time: %f\n", custom_time);

    printf("Success!\n");
    return 0;
}
#else
int main(int argc, char **argv) {
    printf("Test skipped as it is x86_64 specific.\n");
    return 0;
}
#endif
