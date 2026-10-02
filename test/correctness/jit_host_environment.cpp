#include "Halide.h"
#include <cstdlib>
#include <stdio.h>

using namespace Halide;

void set_env(const char *name, const char *val) {
#ifdef _WIN32
    _putenv_s(name, val);
#else
    setenv(name, val, 1);
#endif
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT can't call host C library functions.\n");
        return 0;
    }

    // JIT-compiled code calling getenv should see variables the host process
    // set after startup.
    set_env("HL_JIT_HOST_ENVIRONMENT_TEST", "1");

    Expr value = Internal::Call::make(Handle(), "getenv", {Expr("HL_JIT_HOST_ENVIRONMENT_TEST")}, Internal::Call::Extern);
    Func f;
    f() = select(reinterpret<uint64_t>(value) != 0, 1, 0);
    Buffer<int> result = f.realize();

    if (result() != 1) {
        printf("JIT-compiled getenv did not see HL_JIT_HOST_ENVIRONMENT_TEST\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
