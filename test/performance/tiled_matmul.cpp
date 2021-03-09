#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"
#include <iomanip>
#include <stdio.h>

using namespace Halide;

#define FUSE 0

int main(int argc, char **argv) {
    const int row = 16;
    const int col = 16;
    const int acc = 16;

    Var x("x"), y("y");
    ImageParam A(Int(8), 2, "lhs");
    ImageParam B(Int(8), 3, "rhs");

    RDom r(0, acc);

    Func mm("matmul");
    mm(y, x) = cast<int32_t>(0);
    mm(y, x) += cast<int16_t>(A(r.x, x)) * B(r.x % 4, y, r.x / 4);

    // Ensure all (x, y) tile sizes are the same so that loops are fused.
    int tile_y = 8;
    int tile_x = 6;
    int tile_r = 4;

    // Schedule the reduction
    Var rxi("rxi"), ryi("ryi"), rz("rz");
    RVar rri("rri"), rro("rro");
    mm.compute_at(mm.in(), y)
        .store_in(MemoryType::AMXTile)
        .update()
        // Split into (x,y) tile
        .tile(y, x, ryi, rxi, tile_y, tile_x, TailStrategy::GuardWithIf)
        // Split reduction dim by tile_r
        .split(r.x, rro, rri, tile_r)
        // Reorder so that the (x,y) tile is inside the inner ro loop
        .reorder({rri, ryi, rxi, rro, y, x})
        .atomic()
        .vectorize(rri)
        .vectorize(ryi)
        .vectorize(rxi);

    // Schedule the initialization
    Var ixi("ixi"), iyi("iyi");
    mm.compute_at(mm.in(), y)
        .tile(y, x, iyi, ixi, tile_y, tile_x)
        .vectorize(iyi)
        .vectorize(ixi);

    // Schedule the consumer
    Var mmxi("mmxi"), mmyi("mmyi"), mmz("mmz");
    mm.in()
        .tile(y, x, mmyi, mmxi, tile_y, tile_x)
        .vectorize(mmyi)
        .vectorize(mmxi);

    int count = 1;
    Buffer<int8_t> a_buf(acc, row);
    for (int iy = 0; iy < row; iy++) {
        for (int ix = 0; ix < acc; ix++) {
            a_buf(ix, iy) = count++;  //rand() % 256 - 128;
        }
    }
    A.set(a_buf);

    Buffer<int8_t> b_buf(4, col, acc / 4);
    count = 1;
    for (int iy = 0; iy < acc / 4; iy++) {
        for (int ix = 0; ix < col; ix++) {
            for (int ik = 0; ik < 4; ++ik) {
                b_buf(ik, ix, iy) = count++;  //rand() % 256 - 128;
            }
        }
    }
    B.set(b_buf);

    Buffer<int32_t> out(col, row);

    Func result = mm.in();

    // Uncomment to check the asm
    Target target = get_jit_target_from_environment();
    result.compile_to_llvm_assembly("matmul.ll", {A, B}, target);
    //result.compile_to_assembly("matmul.s", {A, B}, target);

    auto time = Tools::benchmark(20, 20, [&]() {
        result.realize(out);
    });
    std::cout << "Exec time: " << time << "\n";

    for (int i = 0; i < row; ++i) {
        for (int j = 0; j < acc; ++j) {
            std::cout << std::setw(4) << (int)a_buf(j, i) << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n\n*\n\n";
    for (int i = 0; i < acc; ++i) {
        for (int j = 0; j < col; ++j) {
            std::cout << std::setw(4) << (int)b_buf(i % 4, j, i / 4) << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n\n=\n\n";
    for (int i = 0; i < row; ++i) {
        for (int j = 0; j < col; ++j) {
            std::cout << std::setw(6) << out(j, i) << " ";
        }
        std::cout << "\n";
    }

    for (int j = 0; j < row; ++j) {
        for (int i = 0; i < col; ++i) {
            int32_t val = 0;
            for (int k = 0; k < acc; ++k) {
                val += a_buf(k, j) * b_buf(k % 4, i, k / 4);
            }
            if (val != out(i, j)) {
                std::cerr << "Invalid result at " << i << ", " << j << "\n"
                          << out(i, j) << " != " << val << "\n";
                return 1;
            }
        }
    }
    return 0;
}
