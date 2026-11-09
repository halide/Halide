#include "Halide.h"
#include <cstdlib>
#include <stdio.h>
#include <string>

using namespace Halide;

void set_env(const char *name, const char *val) {
#ifdef _WIN32
    _putenv_s(name, val);
#else
    setenv(name, val, 1);
#endif
}

void unset_env(const char *name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

std::string error_message;

void record_error(JITUserContext *, const char *msg) {
    error_message += msg;
}

int stores = 0;

int32_t fail_on_second_store(JITUserContext *, const halide_trace_event_t *e) {
    if (e->event == halide_trace_store && ++stores == 2) {
        return -5;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom trace handlers.\n");
        return 0;
    }

    Func f("f");
    Var x;
    f(x) = x;
    f.trace_stores();
    Callable c = f.compile_to_callable({});
    Buffer<int> out(10);

    // A trace file that can't be opened is reported as an error.
    {
        set_env("HL_TRACE_FILE", "/nonexistent_dir/trace.bin");
        JITUserContext ctx;
        ctx.handlers.custom_error = record_error;
        int result = c(&ctx, out);
        unset_env("HL_TRACE_FILE");
        if (result != halide_error_code_trace_failed) {
            printf("Expected halide_error_code_trace_failed, got %d\n", result);
            return 1;
        }
        if (error_message.find("Failed to open trace file") == std::string::npos) {
            printf("Unexpected error message: %s\n", error_message.c_str());
            return 1;
        }
    }

    // A trace handler returning a negative value fails the pipeline with
    // that value.
    {
        JITUserContext ctx;
        ctx.handlers.custom_trace = fail_on_second_store;
        ctx.handlers.custom_error = record_error;
        int result = c(&ctx, out);
        if (result != -5) {
            printf("Expected -5, got %d\n", result);
            return 1;
        }
        if (stores != 2) {
            printf("Expected the pipeline to stop after two stores, but saw %d\n", stores);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
