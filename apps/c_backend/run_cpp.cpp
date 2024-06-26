#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "pipeline_cpp_cpp.h"
#include "pipeline_cpp_native.h"

using namespace Halide::Runtime;

extern "C" int an_extern_c_func(int a1, float a2) {
    return (int)(a1 + a2);
}

int cpp_extern_toplevel(int a1, float a2) {
    return (int)(a1 + a2);
}

namespace namespace1 {
int cpp_extern(int a1, float a2) {
    return (int)(a1 + a2);
}
}  // namespace namespace1

namespace namespace2 {
int cpp_extern_1(int a1, float a2) {
    return (int)(a1 + a2);
}
int cpp_extern_2(int a1, float a2) {
    return (int)(a1 + a2);
}
int cpp_extern_3(int a1, float a2) {
    return (int)(a1 + a2);
}
}  // namespace namespace2

namespace namespace_outer {
int cpp_extern(int a1, float a2) {
    return (int)(a1 + a2);
}

namespace namespace_inner {
int cpp_extern(int a1, float a2) {
    return (int)(a1 + a2);
}
}  // namespace namespace_inner
}  // namespace namespace_outer

namespace namespace_shared_outer {
int cpp_extern_1(int a1, float a2) {
    return (int)(a1 + a2);
}
int cpp_extern_2(int a1, float a2) {
    return (int)(a1 + a2);
}

namespace inner {
int cpp_extern_1(int a1, float a2) {
    return (int)(a1 + a2);
}
int cpp_extern_2(int a1, float a2) {
    return (int)(a1 + a2);
}
}  // namespace inner

}  // namespace namespace_shared_outer

int main(int argc, char **argv) {
    Buffer<uint16_t, 2> in(100, 100);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            in(x, y) = (uint16_t)rand();
        }
    }

    Buffer<uint16_t, 2> out_native(100, 100);
    Buffer<uint16_t, 2> out_c(100, 100);

    pipeline_cpp_native(in, out_native);

    pipeline_cpp_cpp(in, out_c);

    for (int y = 0; y < out_native.height(); y++) {
        for (int x = 0; x < out_native.width(); x++) {
            if (out_native(x, y) != out_c(x, y)) {
                printf("out_native(%d, %d) = %d, but out_c(%d, %d) = %d\n",
                       x, y, out_native(x, y),
                       x, y, out_c(x, y));
            }
        }
    }

    printf("Success!\n");
    return 0;
}
