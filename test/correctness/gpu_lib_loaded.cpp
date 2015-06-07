#include "Halide.h"
#include <iostream>

#ifdef WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

using namespace Halide;

bool loaded_library = false;

void halide_print(void *user_context, const char *str) {
    printf("%s", str);

    // Check if this is logging an attempt to load
    if (strstr(str, "Loaded CUDA runtime") || strstr(str, "Loaded OpenCL runtime")) {
        loaded_library = true;
    }
}

int main(int argc, char *argv[]) {
    // Pre-load the OpenCL and CUDA libraries
    #ifdef WIN32
    LoadLibrary("nvcuda.dll");
    LoadLibrary("opencl.dll");
    #else
    dlopen("libcuda.so", RTLD_GLOBAL);
    dlopen("libcuda.dylib", RTLD_GLOBAL);
    dlopen("libOpenCL.so", RTLD_GLOBAL);

    dlopen("/Library/Frameworks/CUDA.framework/CUDA", RTLD_GLOBAL);
    dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_GLOBAL);
    #endif

    Internal::JITHandlers handlers;
    handlers.custom_print = halide_print;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("Not running test because no gpu target enabled\n");
        return 0;
    }
    // We need gpu_debug to record object creation.
    target.set_feature(Target::Debug);

    Var x;
    Func f;
    f(x) = x;

    f.gpu_tile(x, 32);
    f.set_custom_print(halide_print);

    Image<int32_t> result = f.realize(256, target);
    for (int i = 0; i < 256; i++) {
        if (result(i) != i) {
            std::cout << "Error! " << result(i) << " != " << i << std::endl;
            return -1;
        }
    }

    if (loaded_library) {
        std::cout << "Error! Runtime loaded GPU library, it should have already been loaded.\n";
        return -1;
    }
    std::cout << "Success!" << std::endl;
    return 0;
}
