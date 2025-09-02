#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f{"f"};

    // Can't have a device extern stage if the target doesn't support it.
    f.define_extern("extern", {}, Halide::type_of<int32_t>(), 1,
                    NameMangling::Default,
                    Halide::DeviceAPI::OpenCL);
    f.compile_jit(Target{"host"});

    printf("Success!\n");
    return 0;
}
