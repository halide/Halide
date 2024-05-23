#include "Halide.h"
#include <unistd.h>

using namespace Halide;

bool errored = false;
void my_error(JITUserContext *, const char *msg) {
    // Emitting "error.*:" to stdout or stderr will cause CMake to report the
    // test as a failure on Windows, regardless of error code returned,
    // hence the abbreviation to "err".
    printf("Expected err: %s\n", msg);
    errored = true;
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::Metal)) {
        printf("[SKIP] Metal not enabled\n");
        return 0;
    }

    Func f;
    Var c, x, ci, xi;
    RVar rxi;
    RDom r(0, 1000, -327600, 327600);

    // Create a function that is very costly to execute, resulting in a timeout
    // on the GPU
    f(x, c) = x + 0.1f * c; 
    f(r.x, c) += cos(sin(tan(cosh(tanh(sinh(exp(tanh(exp(log(tan(cos(exp(f(r.x, c) / cos(cosh(sinh(sin(f(r.x, c))))) 
        / tanh(tan(tan(f(r.x, c)))))))))) + cast<float>(cast<uint8_t>(f(r.x, c) / cast<uint8_t>(log(f(r.x, c))))))))))));
  
    f.gpu_tile(x, c, xi, ci, 4, 4);
    f.update(0).gpu_tile(r.x, c, rxi, ci, 4, 4);

    // Because the error handler is invoked from a Metal runtime thread, setting a custom handler for just
    // this pipeline is insufficient.  Instead, we set a custom handler for the JIT runtime
    JITHandlers handlers;
    handlers.custom_error = my_error;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    f.realize({1000, 100}, t);

    if (!errored) {
        printf("There was supposed to be an error\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
