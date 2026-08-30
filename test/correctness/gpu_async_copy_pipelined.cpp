#include "Halide.h"
#include <cstdio>

using namespace Halide;

// A Func staged into shared memory by the copy engine can be told to run some
// iterations ahead of the consumer that reads it, so that the copies for a
// later iteration are in flight while this one computes. The copies waited
// for are then several batches back, and the ones issued since are left
// outstanding on purpose.

namespace {

const int W = 128, H = 64, K = 32;

bool check(const Buffer<float> &result, int stages) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float correct = 0;
            for (int k = 0; k < K; k++) {
                correct += (float)((x + k * 7) % 13) + y;
                if (stages == 2) {
                    correct += (float)((x * 3 + k) % 11);
                }
            }
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %f instead of %f\n", x, y,
                       (double)result(x, y), (double)correct);
                return false;
            }
        }
    }
    return true;
}

// One staged Func, run `depth` iterations ahead.
bool test_one(int depth) {
    Buffer<float> in(W, K);
    in.fill([](int x, int k) { return (float)((x + k * 7) % 13); });

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    RDom r(0, K, "r");

    stage(x, y) = in(x, y);
    out(x, y) = 0.f;
    out(x, y) += stage(x, r) + y;

    out.gpu_tile(x, y, xi, yi, 32, 8);
    out.update().gpu_tile(x, y, xi, yi, 32, 8).reorder(xi, yi, r, x, y);
    stage.store_at(out, x)
        .compute_at(out, r)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(x)
        .slide(out, r, depth);

    Buffer<float> result(W, H);
    out.realize(result);
    result.copy_to_host();
    return check(result, 1);
}

// Two staged Funcs in the same loop, each with its own depth. Every stage in
// the loop closes one batch per iteration, so the batches to leave in flight
// is the depth times the number of them.
bool test_two(int d1, int d2) {
    Buffer<float> in1(W, K), in2(W, K);
    in1.fill([](int x, int k) { return (float)((x + k * 7) % 13); });
    in2.fill([](int x, int k) { return (float)((x * 3 + k) % 11); });

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func s1("s1"), s2("s2"), out("out");
    RDom r(0, K, "r");

    s1(x, y) = in1(x, y);
    s2(x, y) = in2(x, y);
    out(x, y) = 0.f;
    out(x, y) += s1(x, r) + s2(x, r) + y;

    out.gpu_tile(x, y, xi, yi, 32, 8);
    out.update().gpu_tile(x, y, xi, yi, 32, 8).reorder(xi, yi, r, x, y);
    for (auto p : {std::make_pair(&s1, d1), std::make_pair(&s2, d2)}) {
        p.first->store_at(out, x)
            .compute_at(out, r)
            .store_in(MemoryType::GPUSharedAsync)
            .gpu_threads(x)
            .slide(out, r, p.second);
    }

    Buffer<float> result(W, H);
    out.realize(result);
    result.copy_to_host();
    return check(result, 2);
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] No CUDA target enabled.\n");
        return 0;
    }

    for (int depth = 0; depth <= 3; depth++) {
        if (!test_one(depth)) {
            printf("Failed with one staged Func at depth %d\n", depth);
            return 1;
        }
    }

    const int depths[][2] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {1, 2}, {2, 1}};
    for (const auto &d : depths) {
        if (!test_two(d[0], d[1])) {
            printf("Failed with two staged Funcs at depths %d and %d\n", d[0], d[1]);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
