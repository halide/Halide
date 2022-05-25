#include "Halide.h"
#include <stdio.h>

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

bool equal_eps(float lhs, float rhs, float eps) {
    return std::abs(lhs - rhs) < eps;
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

template<typename LhsInt8, typename RhsInt8>
bool matmul() {
    constexpr int row = 16;
    constexpr int col = 16;
    constexpr int acc = 16;

    Buffer<LhsInt8> A_buf(acc, row);
    Buffer<RhsInt8> B_buf(4, col, acc / 4);

    Var x("x"), y("y");
    RDom r(0, acc);

    Func mm("matmul");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) += cast<int32_t>(A_buf(r, y)) * cast<int32_t>(B_buf(r % 4, x, r / 4));

    constexpr int tile_x = 8;
    constexpr int tile_y = 8;
    constexpr int tile_r = 4;

    Var rxi("rxi"), ryi("ryi");
    RVar rri("rri"), rro("rro");

    mm.compute_at(mm.in(), x)
        .store_in(MemoryType::AMXTile)
        .update()
        .tile(x, y, rxi, ryi, tile_x, tile_y, TailStrategy::GuardWithIf)
        .split(r, rro, rri, tile_r)
        .reorder(rri, rxi, ryi, rro, x, y)
        .atomic()
        .vectorize(rri)
        .vectorize(rxi)
        .vectorize(ryi);

    Var ixi("ixi"), iyi("iyi");
    mm.compute_at(mm.in(), x)
        .tile(x, y, ixi, iyi, tile_x, tile_y)
        .vectorize(ixi)
        .vectorize(iyi);

    // schedule the consumer
    Var mmxi("mmxi"), mmyi("mmyi");
    mm.in()
        .tile(x, y, mmxi, mmyi, tile_x, tile_y)
        .vectorize(mmxi)
        .vectorize(mmyi);

    Func result = mm.in();

    fill_buffer_a(A_buf, row, acc);
    fill_buffer_b(B_buf, col, acc);

    Buffer<int32_t> out(col, row);

    result.realize(out);

    for (int j = 0; j < row; ++j) {
        for (int i = 0; i < col; ++i) {
            int32_t val = 0;
            for (int k = 0; k < acc; ++k) {
                val += static_cast<int32_t>(A_buf(k, j)) * static_cast<int32_t>(B_buf(k % 4, i, k / 4));
            }
            if (val != out(i, j)) {
                std::cerr << "Invalid result at " << i << ", " << j << "\n"
                          << out(i, j) << " != " << val << "\n";
                return false;
            }
        }
    }

    return true;
}

bool matmul_bf16() {
    // lhs: 32x16, rhs: 16x32
    const int row = 32;
    const int col = 32;
    const int acc = 16;

    Var x("x"), y("y");
    Buffer<bfloat16_t> A(acc, row);
    Buffer<bfloat16_t> B(2, col, acc / 2);

    RDom r(0, acc, "acc");

    Func mm("matmul");
    mm(x, y) = cast<float>(0);
    mm(x, y) += cast<float>(cast<float>(A(r.x, y))) * cast<float>(B(r.x % 2, x, r.x / 2));

    int tile_x = 8;
    int tile_y = 8;
    int tile_r = 2;

    Var rxi("rxi"), ryi("ryi");
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
        .vectorize(ryi);

    Var ixi("ixi"), iyi("iyi");
    mm.compute_at(mm.in(), x)
        .tile(x, y, ixi, iyi, tile_x, tile_y)
        .vectorize(ixi)
        .vectorize(iyi);

    // schedule the consumer
    Var mmxi("mmxi"), mmyi("mmyi");
    mm.in()
        .tile(x, y, mmxi, mmyi, tile_x, tile_y)
        .vectorize(mmxi)
        .vectorize(mmyi);

    Func result = mm.in();

    fill_buffer_a_bf16(A, row, acc);
    fill_buffer_b_bf16(B, col, acc);

    Buffer<float> out(col, row);

    // Uncomment to check the asm
    // result.compile_to_llvm_assembly(Internal::get_test_tmp_dir() + "tiled_matmul_bf16.ll", {A, B}, target);
    // result.compile_to_assembly(Internal::get_test_tmp_dir() + "tiled_matmul.s", {A, B}, target);

    result.realize(out);

    for (int j = 0; j < row; ++j) {
        for (int i = 0; i < col; ++i) {
            float val = 0.f;
            for (int k = 0; k < acc; ++k) {
                val += static_cast<float>(A(k, j)) * static_cast<float>(B(k % 2, i, k / 2));
            }
            if (!equal_eps(val, out(i, j), 0.01f)) {
                std::cerr << "Invalid result at " << i << ", " << j << "\n"
                          << out(i, j) << " != " << val << "\n";
                return false;
            }
        }
    }

    return true;
}

auto matmul_ss = &matmul<int8_t, int8_t>;
auto matmul_us = &matmul<uint8_t, int8_t>;
auto matmul_su = &matmul<int8_t, uint8_t>;
auto matmul_uu = &matmul<uint8_t, uint8_t>;

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::AVX512_SapphireRapids)) {
        printf("[SKIP] No AMX target enabled\n");
        return 0;
    }

    printf("Running AMX matmul (signed/signed)\n");
    if (!matmul_ss()) {
        return -1;
    } else {
        printf("Success!\n");
    }

    printf("Running AMX matmul (signed/unsigned)\n");
    if (!matmul_su()) {
        return -1;
    } else {
        printf("Success!\n");
    }

    printf("Running AMX matmul (unsigned/signed)\n");
    if (!matmul_us()) {
        return -1;
    } else {
        printf("Success!\n");
    }

    printf("Running AMX matmul (unsigned/unsigned)\n");
    if (!matmul_uu()) {
        return -1;
    } else {
        printf("Success!\n");
    }

    printf("Running AMX matmul (bf16)\n");
    if (!matmul_bf16()) {
        return -1;
    } else {
        printf("Success!\n");
    }
    return 0;
}