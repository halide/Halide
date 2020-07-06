#include "Halide.h"

using namespace Halide;

extern "C" int extern_func() {
    return 0;
}

int main(int argc, char **argv) {
    Func f("f");

    f.define_extern("extern_func", {f}, Int(32), 2);
    f.infer_arguments();

    printf("Success!\n");
    return 0;
}
