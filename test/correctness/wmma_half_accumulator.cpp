// A matrix multiply of halves into a half accumulator, as Func::change_type()
// writes a float32 reduction retyped to half: each product computed as a
// float32 and rounded to half. The product of two halves is exact in
// float32, so that is a tensor core multiply of half fragments into a half
// accumulator, just as one of the halves' own products is. The accumulator is
// then widened to a float32 one in registers, elementwise, and stored as
// float32: a lane holds the same elements of a half accumulator as of a
// float32 one (two to a register). Nothing documents that, so the result is
// checked on the device for every tile shape, on matrices that differ in
// every row and column. Compiling for CUDA embeds the PTX in the host
// assembly, so no device is needed to check the instructions.

#include "Halide.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

namespace {

int count(const std::string &s, const std::string &pattern) {
    int n = 0;
    for (size_t i = s.find(pattern); i != std::string::npos; i = s.find(pattern, i + 1)) {
        n++;
    }
    return n;
}

// out = A * B on one tile of tm rows and tn columns, over K = 32: the half
// accumulator prod, then wide, its float32 copy.
Func matmul(const Func &A, const Func &B, int tm, int tn) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom k(0, 32, "k");
    RVar ko("ko"), ki("ki");
    Func prod("prod"), wide("wide"), out("out");

    prod(x, y) = cast<float16_t>(0);
    prod(x, y) += cast<float16_t>(cast<float>(A(k, y)) * cast<float>(B(x, k)));
    wide(x, y) = cast<float>(prod(x, y));
    out(x, y) = wide(x, y);

    out.bound(x, 0, tn)
        .bound(y, 0, tm)
        .tile(x, y, xi, yi, tn, tm)
        .gpu_blocks(x, y)
        .vectorize(xi)
        .vectorize(yi);
    for (Func f : {prod, wide}) {
        f.compute_at(out, x)
            .store_in(MemoryType::Tile)
            .vectorize(x)
            .vectorize(y);
    }
    prod.update()
        .split(k, ko, ki, 16)
        .reorder(ki, x, y, ko)
        .unroll(ko)
        .atomic()
        .vectorize(ki)
        .vectorize(x)
        .vectorize(y);
    return out;
}

bool test(int tm, int tn) {
    const std::string shape = "m" + std::to_string(tm) + "n" + std::to_string(tn) + "k16";
    ImageParam pa(Float(16), 2, "A"), pb(Float(16), 2, "B");
    std::string path = Internal::get_test_tmp_dir() + "wmma_half_accumulator.s";
    Target cuda = get_host_target().with_feature(Target::CUDA).with_feature(Target::CUDACapability75);
    matmul(pa, pb, tm, tn).compile_to_assembly(path, {pa, pb}, "out", cuda);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string assembly = ss.str();
    const int mma = count(assembly, "wmma.mma.sync.aligned.row.row." + shape + ".f16.f16");
    const int stores = count(assembly, "wmma.store.d.sync.aligned.row." + shape);
    const int float_stores = count(assembly, "wmma.store.d.sync.aligned.row." + shape + ".f32");
    if (mma != 2 || stores != 1 || float_stores != 1) {
        printf("%s: expected 2 multiplies of halves into a half accumulator and 1 store, "
               "of a float32 tile, but got %d, %d and %d\n",
               shape.c_str(), mma, stores, float_stores);
        return false;
    }

    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        return true;
    }
    t.set_feature(Target::CUDACapability75);
    // Small integers: every product and partial sum is exact in half, so the
    // result is whatever order the tensor cores add in.
    Buffer<float16_t> a(32, tm), b(tn, 32);
    a.fill([](int x, int y) { return float16_t((float)((x * 3 + y * 5) % 7 - 3)); });
    b.fill([](int x, int y) { return float16_t((float)(2 - (x * 5 + y) % 5)); });
    Func A("A"), B("B");
    Var u("u"), v("v");
    A(u, v) = a(u, v);
    B(u, v) = b(u, v);
    A.compute_root();
    B.compute_root();
    Buffer<float> result = matmul(A, B, tm, tn).realize({tn, tm}, t);
    result.copy_to_host();
    for (int y = 0; y < tm; y++) {
        for (int x = 0; x < tn; x++) {
            float expected = 0;
            for (int k = 0; k < 32; k++) {
                expected += (float)a(k, y) * (float)b(x, k);
            }
            if (result(x, y) != expected) {
                printf("%s: result(%d, %d) = %f instead of %f\n", shape.c_str(), x, y, result(x, y), expected);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    for (auto [tm, tn] : {std::pair{16, 16}, std::pair{32, 8}, std::pair{8, 32}}) {
        if (!test(tm, tn)) {
            return 1;
        }
    }
    if (!get_jit_target_from_environment().has_feature(Target::CUDA)) {
        printf("[SKIP] The instructions are as expected, but running them needs CUDA.\n");
        return 0;
    }
    printf("Success!\n");
    return 0;
}
