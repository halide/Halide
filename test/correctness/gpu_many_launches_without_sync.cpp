#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Runs many dependent GPU launches, uploads and allocations without
// synchronizing in between, and checks that each one sees the results of the
// ones before it.

int main(int argc, char **argv) {
#ifdef WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING
    printf("[SKIP] Serialization won't preserve GPU buffers, skipping.\n");
    return 0;
#endif

    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    const int W = 64, H = 64;
    Var x("x"), y("y"), xi("xi"), yi("yi");

    // out = in + k
    ImageParam in(Int(32), 2, "in"), k(Int(32), 2, "k");
    Func add("add");
    add(x, y) = in(x, y) + k(x, y);
    add.gpu_tile(x, y, xi, yi, 16, 16);
    Callable add_step = add.compile_to_callable({in, k}, target);

    // out = in + bufs[0] + ... + bufs[n - 1], to use many buffers per launch
    const int n = 24;
    std::vector<Buffer<int32_t>> bufs;
    Expr sum = in(x, y);
    for (int i = 0; i < n; i++) {
        bufs.emplace_back(W, H);
        bufs.back().fill(1);
        sum += bufs.back()(x, y);
    }
    Func wide("wide");
    wide(x, y) = sum;
    wide.gpu_tile(x, y, xi, yi, 16, 16);
    Callable wide_step = wide.compile_to_callable({in}, target);

    auto check = [&](Buffer<int32_t> &buf, int expected, const char *what) {
        buf.copy_to_host();
        for (int yy = 0; yy < H; yy++) {
            for (int xx = 0; xx < W; xx++) {
                if (buf(xx, yy) != expected) {
                    printf("%s: buf(%d, %d) = %d instead of %d\n", what, xx, yy, buf(xx, yy), expected);
                    return false;
                }
            }
        }
        return true;
    };

    auto reset = [](Buffer<int32_t> &buf, int value) {
        buf.set_device_dirty(false);
        buf.fill(value);
        buf.set_host_dirty();
    };

    Buffer<int32_t> a(W, H), b(W, H), one(W, H);
    reset(one, 1);

    // A long chain of launches, each reading what the one before wrote.
    {
        reset(a, 0);
        Buffer<int32_t> *src = &a, *dst = &b;
        const int launches = 2000;
        for (int i = 0; i < launches; i++) {
            add_step(*src, one, *dst);
            std::swap(src, dst);
        }
        if (!check(*src, launches, "chain")) {
            return 1;
        }
    }

    // Uploads interleaved with launches that read them.
    {
        Buffer<int32_t> step_k(W, H);
        reset(a, 0);
        Buffer<int32_t> *src = &a, *dst = &b;
        int expected = 0;
        for (int i = 0; i < 500; i++) {
            reset(step_k, i % 7);
            add_step(*src, step_k, *dst);
            expected += i % 7;
            std::swap(src, dst);
        }
        if (!check(*src, expected, "uploads")) {
            return 1;
        }
    }

    // Launches with many buffers each.
    {
        reset(a, 0);
        Buffer<int32_t> *src = &a, *dst = &b;
        const int launches = 300;
        for (int i = 0; i < launches; i++) {
            wide_step(*src, *dst);
            std::swap(src, dst);
        }
        if (!check(*src, launches * n, "many buffers")) {
            return 1;
        }
    }

    // Buffers freed (and their memory reused) right after the launches that use them.
    {
        reset(a, 0);
        Buffer<int32_t> *src = &a, *dst = &b;
        const int iterations = 500;
        for (int i = 0; i < iterations; i++) {
            Buffer<int32_t> tmp(W, H);
            add_step(*src, one, tmp);
            add_step(tmp, one, *dst);
            tmp.device_free();
            std::swap(src, dst);
        }
        if (!check(*src, 2 * iterations, "freed buffers")) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
