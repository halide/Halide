#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>

using namespace Halide;

int getenv_int(const char *name, int default_value) {
    if (const char *value = getenv(name)) {
        return atoi(value);
    }
    return default_value;
}

bool getenv_bool(const char *name) {
    if (const char *value = getenv(name)) {
        return atoi(value) != 0;
    }
    return false;
}

bool is_power_of_two(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

bool run_case(const char *name, bool default_value = true) {
    const char *only = getenv("ONLY");
    if (only) {
        return strcmp(only, name) == 0;
    }

    return getenv_bool(name) || default_value;
}

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

void fill_buffer_b_packed_bf16(Buffer<bfloat16_t> &buf, int col, int acc) {
    for (int iyo = 0; iyo < col / 16; ++iyo) {
        for (int iko = 0; iko < acc / 2; ++iko) {
            for (int iyi = 0; iyi < 16; ++iyi) {
                for (int iki = 0; iki < 2; ++iki) {
                    bfloat16_t val = bfloat16_t(((float)rand() / (float)(RAND_MAX)) * 100.f);
                    buf(iki, iyi, iko, iyo) = val;
                }
            }
        }
    }
}

template<typename IntT>
void fill_buffer_b_packed(Buffer<IntT> &buf, int col, int acc) {
    for (int iyo = 0; iyo < col / 16; iyo++) {
        for (int iko = 0; iko < acc / 4; iko++) {
            for (int iyi = 0; iyi < 16; iyi++) {
                for (int iki = 0; iki < 4; iki++) {
                    buf(iki, iyi, iko, iyo) = rand() % 256 + std::numeric_limits<IntT>::min();
                }
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

    const int row = getenv_int("MM_ROW", 1024);
    const int col = getenv_int("MM_COL", 1024);
    const int acc = getenv_int("MM_ACC", 1024);
    const bool pack_b = getenv_int("PACK_B", 0) != 0;
    int tile_y = 16;
    int tile_x = 16;
    int tile_r = 64;

    if (row <= 0 || col <= 0 || acc <= 0) {
        std::cerr << "MM_ROW, MM_COL, and MM_ACC must be positive.\n";
        return false;
    }
    if ((acc % tile_r) != 0) {
        std::cerr << "MM_ACC must be a multiple of " << tile_r << ".\n";
        return false;
    }
    if (pack_b && (col % 16) != 0) {
        std::cerr << "PACK_B requires MM_COL to be a multiple of 16.\n";
        return false;
    }

    Var x("x"), y("y");
    ImageParam A(lhs(8), 2, "lhs");
    A.dim(0).set_min(0).set_extent(acc).set_stride(1);
    A.dim(1).set_min(0).set_extent(row).set_stride(acc);
    // NB the RHS matrix in AMX instructions should be tiled in "VNNI format",
    // where instead of being (cols, rows) where rows are adjacent in memory it
    // should be (4, cols, rows / 4) for int8, or (2, cols, rows / 2) for bf16.
    // This means that the rows must always be divisible by 4 (or 2 for bf16).
    ImageParam B(rhs(8), 3, "rhs");
    ImageParam Bp(rhs(8), 4, "rhs_packed");
    // Constrain B's innermost dim to exactly 4 contiguous elements (the
    // VNNI K-pack), and the next dim's stride to 4. AMX's tile_load expects
    // each output column to be K bytes packed contiguously; without these
    // constraints the strides are symbolic and the AMX matcher conservatively
    // rejects.
    B.dim(0).set_stride(1).set_extent(4);
    B.dim(1).set_min(0).set_extent(col).set_stride(4);
    B.dim(2).set_min(0).set_extent(acc / 4).set_stride(col * 4);

    Bp.dim(0).set_min(0).set_extent(4).set_stride(1);
    Bp.dim(1).set_min(0).set_extent(16).set_stride(4);
    Bp.dim(2).set_min(0).set_extent(acc / 4).set_stride(64);
    Bp.dim(3).set_min(0).set_extent(col / 16).set_stride(acc * 16);

    RDom r(0, acc);

    Func mm("matmul");
    mm(y, x) = cast<int32_t>(0);
    Expr rhs_value = pack_b ? Bp(r.x % 4, y % 16, r.x / 4, y / 16) :
                              B(r.x % 4, y, r.x / 4);
    mm(y, x) += cast<int32_t>(A(r.x, x)) * rhs_value;

    // Tile sizes match the full AMX hardware tile. For int8 the native tile is
    // 16 rows x 64 bytes, so a single tile_matmul can reduce over K = 64.
    // Register-block a 2x2 grid of output tiles. This keeps 4 accumulator tiles
    // live (so the TMUL latency is hidden across independent chains) and reuses
    // each loaded LHS/RHS tile across two accumulators.
    int outer_y = col > tile_y ? getenv_int("RB_Y", 2) : 1;
    int outer_x = row > tile_x ? getenv_int("RB_X", 2) : 1;
    if (outer_y <= 0 || outer_x <= 0) {
        std::cerr << "RB_X and RB_Y must be positive.\n";
        return false;
    }
    if (pack_b && !is_power_of_two(outer_y)) {
        std::cerr << "PACK_B currently requires RB_Y to be a power of two.\n";
        return false;
    }

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

    Buffer<RhsInt8> b_buf, b_packed_buf;
    if (pack_b) {
        b_packed_buf = Buffer<RhsInt8>(4, 16, acc / 4, col / 16);
        fill_buffer_b_packed(b_packed_buf, col, acc);
        Bp.set(b_packed_buf);
    } else {
        b_buf = Buffer<RhsInt8>(4, col, acc / 4);
        fill_buffer_b(b_buf, col, acc);
        B.set(b_buf);
    }

    Buffer<int32_t> out(col, row);

    Func result = mm.in();
    result.output_buffer().dim(0).set_min(0).set_extent(col).set_stride(1);
    result.output_buffer().dim(1).set_min(0).set_extent(row).set_stride(col);

    if (getenv_bool("DUMP_IR")) {
        if (pack_b) {
            result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.ll", {A, Bp}, target);
            result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.s", {A, Bp}, target);
            result.compile_to_conceptual_stmt(Internal::get_test_tmp_dir() + "tiled_matmul.stmt", {A, Bp});
        } else {
            result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.ll", {A, B}, target);
            result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.s", {A, B}, target);
            result.compile_to_conceptual_stmt(Internal::get_test_tmp_dir() + "tiled_matmul.stmt", {A, B});
        }
    }

    // Warm up JIT compilation before benchmarking
    result.realize(out);
    const int bench_iters = getenv_int("BENCH_ITERS", 200);
    double time = Tools::benchmark(1, bench_iters, [&]() {
        result.realize(out);
    });
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

    const int row = getenv_int("MM_ROW", 1024);
    const int col = getenv_int("MM_COL", 1024);
    const int acc = getenv_int("MM_ACC", 1024);
    const bool pack_b = getenv_int("PACK_B", 0) != 0;
    int tile_x = 16;
    int tile_y = 16;
    int tile_r = 32;

    if (row <= 0 || col <= 0 || acc <= 0) {
        std::cerr << "MM_ROW, MM_COL, and MM_ACC must be positive.\n";
        return false;
    }
    if ((acc % tile_r) != 0) {
        std::cerr << "MM_ACC must be a multiple of " << tile_r << ".\n";
        return false;
    }
    if (pack_b && (col % 16) != 0) {
        std::cerr << "PACK_B requires MM_COL to be a multiple of 16.\n";
        return false;
    }

    Var x("x"), y("y");
    ImageParam A(BFloat(16), 2, "lhs");
    A.dim(0).set_min(0).set_extent(acc).set_stride(1);
    A.dim(1).set_min(0).set_extent(row).set_stride(acc);
    ImageParam B(BFloat(16), 3, "rhs");
    ImageParam Bp(BFloat(16), 4, "rhs_packed");
    // Same VNNI-pack constraint as the int8 case, but with K=2 for bf16.
    B.dim(0).set_stride(1).set_extent(2);
    B.dim(1).set_min(0).set_extent(col).set_stride(2);
    B.dim(2).set_min(0).set_extent(acc / 2).set_stride(col * 2);

    Bp.dim(0).set_min(0).set_extent(2).set_stride(1);
    Bp.dim(1).set_min(0).set_extent(16).set_stride(2);
    Bp.dim(2).set_min(0).set_extent(acc / 2).set_stride(32);
    Bp.dim(3).set_min(0).set_extent(col / 16).set_stride(acc * 16);

    RDom r(0, acc, "acc");

    Func mm("matmul");
    mm(x, y) = cast<float>(0);
    Expr rhs_value = pack_b ? Bp(r.x % 2, x % 16, r.x / 2, x / 16) :
                              B(r.x % 2, x, r.x / 2);
    mm(x, y) += cast<float>(cast<float>(A(r.x, y))) * cast<float>(rhs_value);

    // Tile sizes match the full AMX hardware tile. For bf16 the native tile is
    // 16 rows x 64 bytes, so a single tile_matmul can reduce over K = 32.
    // Register-block a 2x2 grid of output tiles (4 live accumulators), as in
    // the int8 path, to hide TMUL latency and reuse loaded tiles.
    int outer_x = col > tile_x ? getenv_int("RB_X", 2) : 1;
    int outer_y = row > tile_y ? getenv_int("RB_Y", 2) : 1;
    if (outer_y <= 0 || outer_x <= 0) {
        std::cerr << "RB_X and RB_Y must be positive.\n";
        return false;
    }
    if (pack_b && !is_power_of_two(outer_x)) {
        std::cerr << "PACK_B currently requires RB_X to be a power of two.\n";
        return false;
    }

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
    result.output_buffer().dim(0).set_min(0).set_extent(col).set_stride(1);
    result.output_buffer().dim(1).set_min(0).set_extent(row).set_stride(col);

    Buffer<bfloat16_t> a_buf(acc, row);
    fill_buffer_a_bf16(a_buf, row, acc);
    A.set(a_buf);

    Buffer<bfloat16_t> b_buf, b_packed_buf;
    if (pack_b) {
        b_packed_buf = Buffer<bfloat16_t>(2, 16, acc / 2, col / 16);
        fill_buffer_b_packed_bf16(b_packed_buf, col, acc);
        Bp.set(b_packed_buf);
    } else {
        b_buf = Buffer<bfloat16_t>(2, col, acc / 2);
        fill_buffer_b_bf16(b_buf, col, acc);
        B.set(b_buf);
    }

    Buffer<float> out(col, row);

    if (getenv_bool("DUMP_IR")) {
        if (pack_b) {
            result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.ll", {A, Bp}, target);
            result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.s", {A, Bp}, target);
            result.compile_to_conceptual_stmt(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.stmt", {A, Bp});
        } else {
            result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.ll", {A, B}, target);
            result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.s", {A, B}, target);
            result.compile_to_conceptual_stmt(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.stmt", {A, B});
        }
    }

    // Warm up JIT compilation before benchmarking
    result.realize(out);
    const int bench_iters = getenv_int("BENCH_ITERS", 200);
    double time = Tools::benchmark(1, bench_iters, [&]() {
        result.realize(out);
    });
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

    bool success = true;

    if (run_case("ss")) {
        printf("Running AMX (signed/signed)\n");
        success &= matmul_ss(target);
    }
    if (run_case("us")) {
        printf("Running AMX (unsigned/signed)\n");
        success &= matmul_us(target);
    }
    if (run_case("su")) {
        printf("Running AMX (signed/unsigned)\n");
        success &= matmul_su(target);
    }
    if (run_case("uu")) {
        printf("Running AMX (unsigned/unsigned)\n");
        success &= matmul_uu(target);
    }
    if (run_case("bf16")) {
        printf("Running AMX (bf16)\n");
        success &= matmul_bf16(target);
    }
    return success ? 0 : 1;
}
