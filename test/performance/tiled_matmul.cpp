#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

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

template<typename LhsInt8, typename RhsInt8>
bool matmul(Halide::Target target) {
    // used for compiling to llvm IR or asm
    (void)target;

    constexpr bool lhs_signed = std::is_signed<LhsInt8>::value;
    constexpr bool rhs_signed = std::is_signed<RhsInt8>::value;

    auto lhs = typename std::conditional<lhs_signed, make_int_t, make_uint_t>::type{};
    auto rhs = typename std::conditional<rhs_signed, make_int_t, make_uint_t>::type{};

    const int row = 16;
    const int col = 16;
    const int acc = 16;

    Var x("x"), y("y");
    ImageParam A(lhs(8), 2, "lhs");
    ImageParam B(rhs(8), 2, "rhs");

    RDom r(0, acc);

    Func mm("matmul");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) += cast<int32_t>(A(r.x, y)) * B(x, r.x);

    // Ensure all (x, y) tile sizes are the same so that loops are fused.
    int tile_y = 8;
    int tile_x = 6;
    int tile_r = 4;

    // Schedule the reduction
    Var xi("xi"), yi("yi");
    RVar ri("ri"), ro("ro");
    mm.compute_at(mm.in(), y)
        .store_in(MemoryType::AMXTile)
        .update()
        // Split into (x,y) tile
        .tile(x, y, xi, yi, tile_x, tile_y, TailStrategy::GuardWithIf)
        // Split reduction dim by tile_r
        .split(r.x, ro, ri, tile_r)
        // Reorder so that the (x,y) tile is inside the inner ro loop
        .reorder({ri, xi, yi, ro, x, y})
        .atomic()
        .vectorize(ri)
        .vectorize(xi)
        .vectorize(yi);

    // Schedule the initialization
    mm.compute_at(mm.in(), x)
        .tile(x, y, xi, yi, tile_x, tile_y)
        .vectorize(xi)
        .vectorize(yi);

    // Schedule the consumer
    mm.in()
        .tile(x, y, xi, yi, tile_x, tile_y)
        .vectorize(xi)
        .vectorize(yi);

    // Stage the VNNI repack of B per strip of rows of output.
    // split the K axis into runs of four with the run innermost, so a load of
    // B(col, k) becomes the ki + col*4 + ko*(4*col) layout AMX's tile_load
    // wants.
    B.in()
        .compute_at(mm.in(), y)
        .split_storage(_1, y, yi, 4)
        .reorder_storage(yi, _0, y);

    Buffer<LhsInt8> a_buf(acc, row);
    fill_buffer_a(a_buf, row, acc);
    A.set(a_buf);

    Buffer<RhsInt8> b_buf(acc, col);
    fill_buffer_a(b_buf, col, acc);  // a natural 2D (k, col) matrix
    B.set(b_buf);

    Buffer<int32_t> out(col, row);

    Func result = mm.in();

    // Uncomment to check the asm
    // result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.ll", {A, B}, target);
    // result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.s", {A, B}, target);

    // Verify correctness against a reference.
    result.realize(out);
    for (int yy = 0; yy < row; yy++) {
        for (int xx = 0; xx < col; xx++) {
            int32_t ref = 0;
            for (int k = 0; k < acc; k++) {
                ref += (int32_t)a_buf(k, yy) * (int32_t)b_buf(xx, k);
            }
            if (out(xx, yy) != ref) {
                std::cout << "Incorrect result at (" << xx << ", " << yy << "): "
                          << out(xx, yy) << " vs " << ref << "\n";
                return false;
            }
        }
    }

    auto time = Tools::benchmark(20, 20, [&]() {
        result.realize(out);
    });
    std::cout << "Exec time: " << time << "\n";
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

    // lhs: 32x16, rhs: 16x32
    const int row = 32;
    const int col = 32;
    const int acc = 16;

    Var x("x"), y("y");
    ImageParam A(BFloat(16), 2, "lhs");
    ImageParam B(BFloat(16), 2, "rhs");

    RDom r(0, acc);

    Func mm("matmul");
    mm(x, y) = cast<float>(0);
    mm(x, y) += cast<float>(A(r.x, y)) * cast<float>(B(x, r.x));

    // Ensure all (x, y) tile sizes are the same so that loops are fused.
    int tile_y = 8;
    int tile_x = 8;
    int tile_r = 2;

    // Schedule the reduction
    Var xi("xi"), yi("yi");
    RVar ri("ri"), ro("ro");
    mm.compute_at(mm.in(), y)
        .store_in(MemoryType::AMXTile)
        .update()
        // Split into (x,y) tile
        .tile(x, y, xi, yi, tile_x, tile_y, TailStrategy::GuardWithIf)
        // Split reduction dim by tile_r
        .split(r.x, ro, ri, tile_r)
        // Reorder so that the (x,y) tile is inside the inner ro loop
        .reorder({ri, xi, yi, ro, x, y})
        .atomic()
        .vectorize(ri)
        .vectorize(xi)
        .vectorize(yi);

    // Schedule the initialization
    mm.compute_at(mm.in(), x)
        .tile(x, y, xi, yi, tile_x, tile_y)
        .vectorize(xi)
        .vectorize(yi);

    // Schedule the consumer
    mm.in()
        .tile(x, y, xi, yi, tile_x, tile_y)
        .vectorize(xi)
        .vectorize(yi);

    // Repack B to VNNI format once on first run, interleaving groups of two
    // rows.
    // Stage the VNNI repack of B (K-run of two for bf16), as in the int8 case.
    B.in()
        .compute_at(mm.in(), y)
        .split_storage(_1, y, yi, 2)
        .reorder_storage(yi, _0, y);

    Buffer<bfloat16_t> a_buf(acc, row);
    fill_buffer_a_bf16(a_buf, row, acc);
    A.set(a_buf);

    Buffer<bfloat16_t> b_buf(col, acc);
    fill_buffer_a_bf16(b_buf, acc, col);  // a natural 2D (col, k) matrix
    B.set(b_buf);

    Buffer<float> out(col, row);

    Func result = mm.in();

    // Uncomment to check the asm
    // result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.ll", {A, B}, target);
    // result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.s", {A, B}, target);

    // Verify correctness against a reference.
    result.realize(out);
    for (int yy = 0; yy < row; yy++) {
        for (int xx = 0; xx < col; xx++) {
            float ref = 0;
            for (int k = 0; k < acc; k++) {
                ref += (float)a_buf(k, yy) * (float)b_buf(xx, k);
            }
            if (!equal_eps(out(xx, yy), ref, std::abs(ref) * 1e-2f + 1.0f)) {
                std::cout << "Incorrect result at (" << xx << ", " << yy << "): "
                          << out(xx, yy) << " vs " << ref << "\n";
                return false;
            }
        }
    }

    auto time = Tools::benchmark(20, 20, [&]() {
        result.realize(out);
    });

    std::cout << "Exec time: " << time << "\n";
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
