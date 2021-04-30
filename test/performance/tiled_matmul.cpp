#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <iomanip>
#include <iostream>

using namespace Halide;

struct make_uint_t {
    template<typename... Args>
    Type operator()(Args &&...args) const {
        return UInt(static_cast<Args &&>(args)...);
    }
};

struct make_int_t {
    template<typename... Args>
    Type operator()(Args &&...args) const {
        return Int(static_cast<Args &&>(args)...);
    }
};

template<typename IntT>
void fill_buffer_a(Buffer<IntT> &buf, int row, int acc) {
    for (int iy = 0; iy < row; iy++) {
        for (int ix = 0; ix < acc; ix++) {
            buf(ix, iy) = rand() % 256 + std::numeric_limits<IntT>::min();
        }
    }
}

template<typename IntT>
void fill_buffer_b(Buffer<IntT> &buf, int col, int acc) {
    for (int iy = 0; iy < acc / 4; iy++) {
        for (int ix = 0; ix < col; ix++) {
            for (int ik = 0; ik < 4; ++ik) {
                buf(ik, ix, iy) = rand() % 256 + std::numeric_limits<IntT>::min();
            }
        }
    }
}

template<typename LhsInt8, typename RhsInt8>
bool matmul() {
    constexpr bool lhs_signed = std::is_signed<LhsInt8>::value;
    constexpr bool rhs_signed = std::is_signed<RhsInt8>::value;

    auto lhs = typename std::conditional<lhs_signed, make_int_t, make_uint_t>::type{};
    auto rhs = typename std::conditional<rhs_signed, make_int_t, make_uint_t>::type{};

    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::AVX512_SapphireRapids)) {
        std::cout << "[SKIP] The tiled matmul test is only designed to test AMX support.\n";
        return true;
    }
    const int row = 16;
    const int col = 16;
    const int acc = 16;

    Var x("x"), y("y");
    ImageParam A(lhs(8), 2, "lhs");
    // NB the RHS matrix in AMX instructions should be tiled in "VNNI format",
    // where instead of being (cols, rows) where rows are adjacent in memory it
    // should be (4, cols, rows / 4) for int8, or (2, cols, rows / 2) for bf16.
    // This means that the rows must always be divisible by 4 (or 2 for bf16).
    ImageParam B(rhs(8), 3, "rhs");

    RDom r(0, acc);

    Func mm("matmul");
    mm(y, x) = cast<int32_t>(0);
    mm(y, x) += cast<int32_t>(A(r.x, x)) * B(r.x % 4, y, r.x / 4);

    // Ensure all (x, y) tile sizes are the same so that loops are fused.
    int tile_y = 8;
    int tile_x = 6;
    int tile_r = 4;

    // Schedule the reduction
    Var rxi("rxi"), ryi("ryi");
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
    Var mmxi("mmxi"), mmyi("mmyi");
    mm.in()
        .tile(y, x, mmyi, mmxi, tile_y, tile_x)
        .vectorize(mmyi)
        .vectorize(mmxi);

    Buffer<LhsInt8> a_buf(acc, row);
    fill_buffer_a(a_buf, row, acc);
    A.set(a_buf);

    Buffer<RhsInt8> b_buf(4, col, acc / 4);
    fill_buffer_b(b_buf, col, acc);
    B.set(b_buf);

    Buffer<int32_t> out(col, row);

    Func result = mm.in();

    // Uncomment to check the asm
    //result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.ll", {A, B}, target);
    //result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.s", {A, B}, target);

    auto time = Tools::benchmark(20, 20, [&]() {
        result.realize(out);
    });
    std::cout << "Exec time: " << time << "\n";

    for (int j = 0; j < row; ++j) {
        for (int i = 0; i < col; ++i) {
            int32_t val = 0;
            for (int k = 0; k < acc; ++k) {
                val += a_buf(k, j) * b_buf(k % 4, i, k / 4);
            }
            if (val != out(i, j)) {
                std::cerr << "Invalid result at " << i << ", " << j << "\n"
                          << out(i, j) << " != " << val << "\n";
                return false;
            }
        }
    }
    std::cout << "Success!\n";
    return true;
}

auto matmul_ss = &matmul<int8_t, int8_t>;
auto matmul_us = &matmul<uint8_t, int8_t>;
auto matmul_su = &matmul<int8_t, uint8_t>;
auto matmul_uu = &matmul<uint8_t, uint8_t>;

int main(int argc, char **argv) {
    matmul_ss();
    matmul_us();
    matmul_su();
    matmul_uu();
    return 0;
}
