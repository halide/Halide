#include "Halide.h"
#include <stdio.h>

// See https://github.com/halide/Halide/issues/3070

using namespace Halide;

template<typename T>
void test() {
    Param<T> bound;
    ImageParam in(UInt(8), 1);
    Var x;
    Func f;

    f(x) = in(clamp(x, 0, bound * 2 - 1));

    Buffer<uint8_t> foo(10);
    foo.fill(0);
    in.set(foo);
    bound.set(5);

    auto result = f.realize(200);
}

int main(int argc, char **argv) {
    printf("Trying int32_t\n");
    test<int32_t>();
    printf("Trying int16_t\n");
    test<int16_t>();
    printf("Success!\n");
    return 0;
}
