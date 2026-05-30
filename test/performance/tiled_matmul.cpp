#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <chrono>
#include <iomanip>
#include <iostream>

using namespace Halide;

void fill_buffer_a_bf16(Buffer<bfloat16_t> &buf, int row, int acc) {
    for (int iy = 0; iy < row; ++iy) {
        for (int ix = 0; ix < acc; ++ix) {
            // value between 0 and 100
            bfloat16_t val = bfloat16_t(((float)rand() / (float)(RAND_MAX)) * 100.f);
            buf(ix, iy) = val;
        }
    }
}

void fill_buffer_b_bf16(Buffer<bfloat16_t> &buf, int col, int acc) {
    for (int iy = 0; iy < acc / 2; ++iy) {
        for (int ix = 0; ix < col; ++ix) {
            for (int ik = 0; ik < 2; ++ik) {
                bfloat16_t val = bfloat16_t(((float)rand() / (float)(RAND_MAX)) * 100.f);
                buf(ik, ix, iy) = val;
            }
        }
    }
}

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
bool matmul(Halide::Target target) {
    // used for compiling to llvm IR or asm
    (void)target;

    constexpr bool lhs_signed = std::is_signed_v<LhsInt8>;
    constexpr bool rhs_signed = std::is_signed_v<RhsInt8>;

    auto lhs = std::conditional_t<lhs_signed, make_int_t, make_uint_t>{};
    auto rhs = std::conditional_t<rhs_signed, make_int_t, make_uint_t>{};

    const int row = getenv("MM_ROW") ? atoi(getenv("MM_ROW")) : 1024;
    const int col = getenv("MM_COL") ? atoi(getenv("MM_COL")) : 1024;
    const int acc = getenv("MM_ACC") ? atoi(getenv("MM_ACC")) : 1024;

    Var x("x"), y("y");
    ImageParam A(lhs(8), 2, "lhs");
    // NB the RHS matrix in AMX instructions should be tiled in "VNNI format",
    // where instead of being (cols, rows) where rows are adjacent in memory it
    // should be (4, cols, rows / 4) for int8, or (2, cols, rows / 2) for bf16.
    // This means that the rows must always be divisible by 4 (or 2 for bf16).
    ImageParam B(rhs(8), 3, "rhs");
    // Constrain B's innermost dim to exactly 4 contiguous elements (the
    // VNNI K-pack), and the next dim's stride to 4. AMX's tile_load expects
    // each output column to be K bytes packed contiguously; without these
    // constraints the strides are symbolic and the AMX matcher conservatively
    // rejects.
    B.dim(0).set_stride(1).set_extent(4);
    B.dim(1).set_stride(4);

    RDom r(0, acc);

    Func mm("matmul");
    mm(y, x) = cast<int32_t>(0);
    mm(y, x) += cast<int32_t>(A(r.x, x)) * B(r.x % 4, y, r.x / 4);

    // Tile sizes match the full AMX hardware tile. For int8 the native tile is
    // 16 rows x 64 bytes, so a single tile_matmul can reduce over K = 64.
    int tile_y = 16;
    int tile_x = 16;
    int tile_r = 64;

    // Register-block a 2x2 grid of output tiles. This keeps 4 accumulator tiles
    // live (so the TMUL latency is hidden across independent chains) and reuses
    // each loaded LHS/RHS tile across two accumulators.
    int outer_y = col > tile_y ? 2 : 1;
    int outer_x = row > tile_x ? 2 : 1;

    // Schedule the reduction
    Var rxi("rxi"), ryi("ryi"), rxo("rxo"), ryo("ryo");
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
        .vectorize(rxi)
        // Register-block over a 2x2 grid of tiles.
        .tile(y, x, ryo, rxo, outer_y, outer_x)
        .reorder({rri, ryi, rxi, ryo, rxo, rro, y, x})
        .unroll(ryo)
        .unroll(rxo);

    // Schedule the initialization
    Var ixi("ixi"), iyi("iyi");
    mm.compute_at(mm.in(), y)
        .tile(y, x, iyi, ixi, tile_y, tile_x)
        .vectorize(iyi)
        .vectorize(ixi)
        .unroll(y)
        .unroll(x);

    // Schedule the consumer
    Var mmxi("mmxi"), mmyi("mmyi");
    mm.in()
        .tile(y, x, mmyi, mmxi, tile_y * outer_y, tile_x * outer_x)
        .vectorize(mmyi, tile_y)
        .vectorize(mmxi, tile_x)
        .unroll(mmyi)
        .unroll(mmxi);

    Buffer<LhsInt8> a_buf(acc, row);
    fill_buffer_a(a_buf, row, acc);
    A.set(a_buf);

    Buffer<RhsInt8> b_buf(4, col, acc / 4);
    fill_buffer_b(b_buf, col, acc);
    B.set(b_buf);

    Buffer<int32_t> out(col, row);

    Func result = mm.in();

    // Uncomment to check the asm
    if (getenv("DUMP_IR")) {
        result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.ll", {A, B}, target);
        result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.s", {A, B}, target);
        result.compile_to_conceptual_stmt(Internal::get_test_tmp_dir() + "tiled_matmul.stmt", {A, B});
    }

    // Warm up JIT compilation before benchmarking
    result.realize(out);
    const int bench_iters = getenv("BENCH_ITERS") ? atoi(getenv("BENCH_ITERS")) : 200;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < bench_iters; i++) {
        result.realize(out);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double time = std::chrono::duration<double>(t1 - t0).count() / bench_iters;
    // 2 ops per MAC (multiply + accumulate)
    double gops = 2.0 * row * col * acc / (time * 1e9);
    std::cout << "Exec time: " << std::scientific << std::setprecision(3) << time
              << "s  (" << std::fixed << std::setprecision(1) << gops << " GOPS)\n";
    std::cout << "Success!\n";
    return true;
}

auto matmul_ss = &matmul<int8_t, int8_t>;
auto matmul_us = &matmul<uint8_t, int8_t>;
auto matmul_su = &matmul<int8_t, uint8_t>;
auto matmul_uu = &matmul<uint8_t, uint8_t>;

bool equal_eps(float lhs, float rhs, float eps) {
    return std::abs(lhs - rhs) < eps;
}

bool matmul_bf16(Halide::Target target) {
    (void)target;

    const int row = getenv("MM_ROW") ? atoi(getenv("MM_ROW")) : 1024;
    const int col = getenv("MM_COL") ? atoi(getenv("MM_COL")) : 1024;
    const int acc = getenv("MM_ACC") ? atoi(getenv("MM_ACC")) : 1024;

    Var x("x"), y("y");
    ImageParam A(BFloat(16), 2, "lhs");
    ImageParam B(BFloat(16), 3, "rhs");
    // Same VNNI-pack constraint as the int8 case, but with K=2 for bf16.
    B.dim(0).set_stride(1).set_extent(2);
    B.dim(1).set_stride(2);

    RDom r(0, acc, "acc");

    Func mm("matmul");
    mm(x, y) = cast<float>(0);
    mm(x, y) += cast<float>(cast<float>(A(r.x, y))) * cast<float>(B(r.x % 2, x, r.x / 2));

    // Tile sizes match the full AMX hardware tile. For bf16 the native tile is
    // 16 rows x 64 bytes, so a single tile_matmul can reduce over K = 32.
    int tile_x = 16;
    int tile_y = 16;
    int tile_r = 32;

    // Register-block a 2x2 grid of output tiles (4 live accumulators), as in
    // the int8 path, to hide TMUL latency and reuse loaded tiles.
    int outer_x = col > tile_x ? 2 : 1;
    int outer_y = row > tile_y ? 2 : 1;

    Var rxi("rxi"), ryi("ryi"), rxo("rxo"), ryo("ryo");
    RVar rri("rri"), rro("rro");

    mm.compute_at(mm.in(), x)
        .store_in(MemoryType::AMXTile)
        .update()
        .tile(x, y, rxi, ryi, tile_x, tile_y, TailStrategy::GuardWithIf)
        .split(r.x, rro, rri, tile_r)
        .reorder({rri, rxi, ryi, rro, x, y})
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi)
        .tile(x, y, rxo, ryo, outer_x, outer_y)
        .reorder({rri, rxi, ryi, rxo, ryo, rro, x, y})
        .unroll(rxo)
        .unroll(ryo);

    Var ixi("ixi"), iyi("iyi");
    mm.compute_at(mm.in(), x)
        .tile(x, y, ixi, iyi, tile_x, tile_y)
        .vectorize(ixi)
        .vectorize(iyi)
        .unroll(x)
        .unroll(y);

    // schedule the consumer
    Var mmxi("mmxi"), mmyi("mmyi");
    mm.in()
        .tile(x, y, mmxi, mmyi, tile_x * outer_x, tile_y * outer_y)
        .vectorize(mmxi, tile_x)
        .vectorize(mmyi, tile_y)
        .unroll(mmxi)
        .unroll(mmyi);

    Func result = mm.in();

    Buffer<bfloat16_t> a_buf(acc, row);
    fill_buffer_a_bf16(a_buf, row, acc);
    A.set(a_buf);

    Buffer<bfloat16_t> b_buf(2, col, acc / 2);
    fill_buffer_b_bf16(b_buf, col, acc);
    B.set(b_buf);

    Buffer<float> out(col, row);

    // Uncomment to check the asm
    if (getenv("DUMP_IR")) {
        result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.ll", {A, B}, target);
        result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.s", {A, B}, target);
        result.compile_to_conceptual_stmt(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.stmt", {A, B});
    }

    // Warm up JIT compilation before benchmarking
    result.realize(out);
    const int bench_iters = 200;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < bench_iters; i++) {
        result.realize(out);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double time = std::chrono::duration<double>(t1 - t0).count() / bench_iters;
    // 2 ops per MAC (multiply + accumulate)
    double gflops = 2.0 * row * col * acc / (time * 1e9);
    std::cout << "Exec time: " << std::scientific << std::setprecision(3) << time
              << "s  (" << std::fixed << std::setprecision(1) << gflops << " GFLOPS)\n";
    std::cout << "Success!\n";
    return true;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::AVX512_SapphireRapids)) {
        std::cout << "[SKIP] The tiled matmul test is only designed to test AMX support.\n";
        return 0;
    }

    printf("Running AMX (signed/signed)\n");
    matmul_ss(target);
    printf("Running AMX (unsigned/signed)\n");
    matmul_us(target);
    printf("Running AMX (signed/unsigned)\n");
    matmul_su(target);
    printf("Running AMX (unsigned/unsigned)\n");
    matmul_uu(target);

    printf("Running AMX (bf16)\n");
    matmul_bf16(target);
    return 0;
}
