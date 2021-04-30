#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>
#include <memory>

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    // Test that specializing for stride 0 or stride 1 produces good code.
    ImageParam a(UInt(8), 2);
    ImageParam b(UInt(8), 2);
    ImageParam b_broadcastable(UInt(8), 2);
    Func result, result_broadcastable;
    Var x, y;

    result(x, y) = a(x, y) + b(x, y);
    result_broadcastable(x, y) = a(x, y) + b_broadcastable(x, y);

    result.vectorize(x, 16);
    result_broadcastable.vectorize(x, 16);

    b_broadcastable.dim(0).set_stride(Expr());
    result_broadcastable.specialize(b_broadcastable.dim(0).stride() == 1);
    result_broadcastable.specialize(b_broadcastable.dim(0).stride() == 0);

    result.compile_jit();
    result_broadcastable.compile_jit();

    // Test broadcasting b's x dimension.
    const int width = 1 << 10;
    const int height = 1 << 10;
    Buffer<uint8_t> a_image(width, height);
    Buffer<uint8_t> b_image(width, height);
    Buffer<uint8_t> b_broadcast_image(1, height);

    a_image.fill([](int x, int y) { return (x + y) % 32; });
    b_image.fill([](int x, int y) { return y % 32; });
    b_broadcast_image.fill([](int x, int y) { return y % 32; });

    b_broadcast_image.raw_buffer()->dim[0].extent = width;
    b_broadcast_image.raw_buffer()->dim[0].stride = 0;

    a.set(a_image);
    b.set(b_image);
    b_broadcastable.set(b_broadcast_image);

    Buffer<uint8_t> result_image(width, height);
    Buffer<uint8_t> result_broadcastable_image(width, height);
    // Warm up caches, etc.
    result.realize(result_image);
    result_broadcastable.realize(result_broadcastable_image);

    result_image.for_each_element([&](int x, int y) {
        assert(result_image(x, y) == (x + y) % 32 + y % 32);
    });
    result_broadcastable_image.for_each_element([&](int x, int y) {
        assert(result_broadcastable_image(x, y) == (x + y) % 32 + y % 32);
    });

    double t = benchmark([&]() {
        result.realize(result_image);
    });
    printf("Performance %.3e ops/s.\n", (width * height) / t);

    double t_broadcast = benchmark([&]() {
        result_broadcastable.realize(result_broadcastable_image);
    });
    printf("Broadcast performance %.3e ops/s.\n", (width * height) / t_broadcast);

    if (t_broadcast > t * 2) {
        printf("Broadcast timing suspicious: %gx slower\n", t_broadcast / t);
        return -1;
    }

    printf("Success!\n");
}
