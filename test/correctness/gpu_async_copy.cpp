#include "Halide.h"
#include <cstdio>

using namespace Halide;

// A Func stored in GPUSharedAsync is staged into shared memory by the copy
// engine rather than by loading it into registers and storing it back out.
// The copy engine moves 4, 8 or 16 bytes per thread, so the cases below cover
// each of those widths at several element sizes, as well as the shapes a
// staged input tends to take: a two-dimensional tile, more than one input
// staged into the same kernel, and a wrapper made with Func::in.

namespace {

int failures = 0;

template<typename T>
void check_result(const char *name, Buffer<T> &result, Buffer<T> &input) {
    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            T correct = (T)(input(x, y) * 2);
            if (result(x, y) != correct) {
                printf("[%s] result(%d, %d) = %f instead of %f\n",
                       name, x, y, (double)result(x, y), (double)correct);
                failures++;
                return;
            }
        }
    }
    printf("[%s] OK\n", name);
}

template<typename T>
Buffer<T> make_input(int w, int h) {
    Buffer<T> in(w, h);
    in.fill([](int x, int y) { return (T)((x + y * 3) % 32); });
    return in;
}

// Stage an input through shared memory with the copy engine, vectorized by
// `vec` elements, and check the consumer sees the right values.
template<typename T>
void test_width(const char *name, int vec) {
    const int W = 256, H = 32;
    Buffer<T> input = make_input<T>(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = input(x, y);
    out(x, y) = cast<T>(stage(x, y) * 2);

    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, vec);

    Buffer<T> result(W, H);
    out.realize(result);
    result.copy_to_host();
    check_result(name, result, input);
}

// The staged tile spread over threads in both dimensions.
void test_2d_tile() {
    const int W = 256, H = 64;
    Buffer<float> input = make_input<float>(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi"), xii("xii");
    Func stage("stage"), out("out");
    stage(x, y) = input(x, y);
    out(x, y) = stage(x, y) * 2;

    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .split(x, x, xii, 4)
        .vectorize(xii)
        .gpu_threads(x, y);

    Buffer<float> result(W, H);
    out.realize(result);
    result.copy_to_host();
    check_result("2d_tile", result, input);
}

// Padding the rows to dodge bank conflicts, with a stride that stays a
// multiple of the vector width so the rows are still aligned.
void test_padded_storage() {
    const int W = 256, H = 32;
    Buffer<float> input = make_input<float>(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = input(x, y);
    out(x, y) = stage(x, y) * 2;

    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .align_storage(x, 8)
        .gpu_threads(y)
        .vectorize(x, 4);

    Buffer<float> result(W, H);
    out.realize(result);
    result.copy_to_host();
    check_result("padded_storage", result, input);
}

// Two inputs staged into the same kernel, so more than one copy is in flight
// before the barrier that waits for them.
void test_two_inputs() {
    const int W = 256, H = 32;
    Buffer<float> a = make_input<float>(W, H);
    Buffer<float> b = make_input<float>(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func sa("sa"), sb("sb"), out("out");
    sa(x, y) = a(x, y);
    sb(x, y) = b(x, y);
    out(x, y) = sa(x, y) + sb(x, y);

    out.gpu_tile(x, y, xi, yi, 64, 8);
    sa.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, 4);
    sb.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, 4);

    Buffer<float> result(W, H);
    out.realize(result);
    result.copy_to_host();
    check_result("two_inputs", result, a);
}

// Func::in is the idiomatic way to get a Func that is a plain copy, and is
// what the error message points users at.
void test_wrapper() {
    const int W = 256, H = 32;
    Buffer<float> input = make_input<float>(W, H);

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func in_f("in_f"), out("out");
    in_f(x, y) = input(x, y);
    out(x, y) = in_f(x, y) * 2;

    out.gpu_tile(x, y, xi, yi, 64, 8);
    in_f.in()
        .compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, 4);
    in_f.compute_root();

    Buffer<float> result(W, H);
    out.realize(result);
    result.copy_to_host();
    check_result("wrapper", result, input);
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] No CUDA target enabled.\n");
        return 0;
    }
    if (target.get_cuda_capability_lower_bound() < 80) {
        printf("[SKIP] Asynchronous copies need compute capability 8.0 or above.\n");
        return 0;
    }

    // Each of the three copy widths the hardware supports, at several element
    // sizes.
    test_width<float>("f32_x1_4b", 1);
    test_width<float>("f32_x2_8b", 2);
    test_width<float>("f32_x4_16b", 4);
    test_width<uint8_t>("u8_x4_4b", 4);
    test_width<uint8_t>("u8_x8_8b", 8);
    test_width<uint8_t>("u8_x16_16b", 16);
    test_width<uint16_t>("u16_x2_4b", 2);
    test_width<uint16_t>("u16_x8_16b", 8);
    test_width<int32_t>("i32_x4_16b", 4);

    test_2d_tile();
    test_padded_storage();
    test_two_inputs();
    test_wrapper();

    if (failures) {
        printf("%d case(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
