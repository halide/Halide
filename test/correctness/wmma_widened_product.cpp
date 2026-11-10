// The product of two int8s fits in 16 bits, so a matrix multiply that
// computes each product as an int16 and then widens it to the int32 it
// accumulates in is a tensor core multiply of int8 fragments, just as one
// that widens the operands to int32 first is. Likewise the product of two
// uint8s computed as a uint16. A product computed in a 16-bit type of the
// other signedness is not the operands' product (an int8 -1 widened to
// uint16 is 65535; a uint16 product of uint8s above 32767 is negative as an
// int16), so it is not a tensor core multiply. Compiling for CUDA embeds the
// PTX in the host assembly, so no device is needed to check the instructions.

#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

namespace {

const int tile = 16;

// out = A * B on one tile, each product computed in `narrow` and then widened
// to the int32 accumulator.
Func matmul(const Func &A, const Func &B, Type narrow) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom k(0, tile, "k");
    Func prod("prod"), out("out");

    prod(x, y) = 0;
    prod(x, y) += cast<int32_t>(cast(narrow, A(k, y)) * cast(narrow, B(x, k)));
    out(x, y) = prod(x, y);

    out.bound(x, 0, tile)
        .bound(y, 0, tile)
        .tile(x, y, xi, yi, tile, tile)
        .gpu_blocks(x, y)
        .vectorize(xi)
        .vectorize(yi);
    prod.compute_at(out, x)
        .store_in(MemoryType::Tile)
        .vectorize(x)
        .vectorize(y);
    prod.update()
        .reorder(k, x, y)
        .atomic()
        .vectorize(k)
        .vectorize(x)
        .vectorize(y);
    return out;
}

Target cuda_target() {
    return get_host_target()
        .with_feature(Target::CUDA)
        .with_feature(Target::CUDACapability75);
}

std::string compile(Type operand, Type narrow) {
    ImageParam A(operand, 2, "A"), B(operand, 2, "B");
    std::string path = Internal::get_test_tmp_dir() + "wmma_widened_product.s";
    matmul(A, B, narrow).compile_to_assembly(path, {A, B}, "out", cuda_target());
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char **argv) {
    if (compile(Int(8), Int(16)).find("wmma.mma.sync.aligned.row.row.m16n16k16.s32.s8.s8.s32") == std::string::npos) {
        printf("Expected a wmma.mma of int8 fragments in the PTX\n");
        return 1;
    }
    if (compile(UInt(8), UInt(16)).find("wmma.mma.sync.aligned.row.row.m16n16k16.s32.u8.u8.s32") == std::string::npos) {
        printf("Expected a wmma.mma of uint8 fragments in the PTX\n");
        return 1;
    }

#if HALIDE_WITH_EXCEPTIONS
    int failures = 0;
    failures += !expect_user_error("int8_in_uint16", "may not hold", []() { compile(Int(8), UInt(16)); });
    failures += !expect_user_error("uint8_in_int16", "may not hold", []() { compile(UInt(8), Int(16)); });
    if (failures) {
        return 1;
    }
#endif

    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] The instructions are as expected, but running them needs CUDA.\n");
        return 0;
    }
    t.set_feature(Target::CUDACapability75);
    // Operands of both signs, the extremes included.
    Buffer<int8_t> a(tile, tile), b(tile, tile);
    for (int y = 0; y < tile; y++) {
        for (int x = 0; x < tile; x++) {
            a(x, y) = (int8_t)((x * 37 + y * 101) % 256 - 128);
            b(x, y) = (int8_t)(127 - (x * 59 + y * 13) % 256);
        }
    }
    a(0, 0) = b(0, 0) = -128;
    a(1, 0) = -1;
    b(0, 1) = 1;
    Func A("A"), B("B");
    Var u("u"), v("v");
    A(u, v) = a(u, v);
    B(u, v) = b(u, v);
    A.compute_root();
    B.compute_root();
    Buffer<int32_t> result = matmul(A, B, Int(16)).realize({tile, tile}, t);
    result.copy_to_host();
    for (int y = 0; y < tile; y++) {
        for (int x = 0; x < tile; x++) {
            int expected = 0;
            for (int k = 0; k < tile; k++) {
                expected += (int)a(k, y) * (int)b(x, k);
            }
            if (result(x, y) != expected) {
                printf("result(%d, %d) = %d instead of %d\n", x, y, result(x, y), expected);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
