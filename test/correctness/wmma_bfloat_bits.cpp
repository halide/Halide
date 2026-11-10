// A float32 built from a bfloat16's bits (the 16 bits shifted to the top of
// a uint32) is that bfloat16 widened, just as a cast of it is, so a matrix
// multiply of operands widened that way is a tensor core multiply of
// bfloat16 fragments. Compiling for CUDA embeds the PTX in the host
// assembly, so no device is needed to check this.

#include "Halide.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    const int tile = 16;

    ImageParam A(BFloat(16), 2, "A"), B(BFloat(16), 2, "B");
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom k(0, tile, "k");
    Func prod("prod"), out("out");

    auto widen = [](const Expr &e) {
        return reinterpret<float>(cast<uint32_t>(reinterpret<uint16_t>(e)) << 16);
    };
    prod(x, y) = 0.f;
    prod(x, y) += widen(A(k, y)) * widen(B(x, k));
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

    Target t = get_host_target()
                   .with_feature(Target::CUDA)
                   .with_feature(Target::CUDACapability80);
    std::string path = Internal::get_test_tmp_dir() + "wmma_bfloat_bits.s";
    out.compile_to_assembly(path, {A, B}, "out", t);
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    if (ss.str().find("wmma.mma.sync.aligned.row.row.m16n16k16.f32.bf16.bf16.f32") == std::string::npos) {
        printf("Expected a wmma.mma of bfloat16 fragments in the PTX\n");
        return 1;
    }
    printf("Success!\n");
    return 0;
}
