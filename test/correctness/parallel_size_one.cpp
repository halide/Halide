#include "Halide.h"

#include <atomic>
#include <stdio.h>

using namespace Halide;

// Count how many times halide_do_task is invoked.
static std::atomic<int> task_count{0};

static int counting_do_task(JITUserContext *user_context,
                            int (*f)(JITUserContext *, int, uint8_t *),
                            int idx, uint8_t *closure) {
    task_count++;
    return f(user_context, idx, closure);
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support threads.\n");
        return 0;
    }

    // f is computed per x of g, so f's own parallel loop over x has a
    // statically-known extent of one. Such a loop must still be dispatched
    // through the thread pool via halide_do_task, rather than being simplified
    // away into a serial loop body.
    Var x("x");
    Func f("f"), g("g");
    f(x) = x;
    g(x) = f(x);

    f.compute_at(g, x).parallel(x);

    JITUserContext context;
    context.handlers.custom_do_task = counting_do_task;
    g.realize(&context, {1024});

    if (task_count.load() == 0) {
        printf("halide_do_task was not called for a size-one parallel loop. "
               "The loop was incorrectly simplified into a serial body.\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
