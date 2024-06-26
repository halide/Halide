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

template<typename T>
void print_mat(const Buffer<T> &buf, int rows, int cols) {
    using cast_T = std::conditional_t<std::is_integral_v<T>, int, T>;
    for (int j = 0; j != rows; ++j) {
        for (int i = 0; i != cols; ++i) {
            std::cout << static_cast<cast_T>(buf(i, j)) << " ";
        }
        std::cout << std::endl;
    }
}

template<typename T>
void print_mat_rhs(const Buffer<T> &buf, int rows, int cols) {
    using cast_T = std::conditional_t<std::is_integral_v<T>, int, T>;
    for (int j = 0; j != (rows / (4 / sizeof(T))); ++j) {
        for (int k = 0; k != (4 / sizeof(T)); ++k) {
            for (int i = 0; i != cols; ++i) {
                std::cout << static_cast<cast_T>(buf(k, i, j)) << " ";
            }

            std::cout << std::endl;
        }
    }
}

template<typename LhsInt8, typename RhsInt8>
bool matmul(int row, int col, int acc, int tile_x, int tile_y, int tile_r) {
    Buffer<LhsInt8> A_buf(acc, row);
    Buffer<RhsInt8> B_buf(4, col, acc / 4);

    Var x("x"), y("y");
    RDom r(0, acc);

    Func mm("matmul");
    mm(x, y) = cast<int32_t>(0);
    mm(x, y) += cast<int32_t>(A_buf(r, y)) * cast<int32_t>(B_buf(r % 4, x, r / 4));

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

    // uncomment to check the matrices
    // std::cout << "Matrix A\n";
    // print_mat(A_buf, row, acc);
    // std::cout << "Matrix B\n";
    // print_mat_rhs(B_buf, acc, col);

    // std::cout << "result\n";
    // print_mat(out, row, col);

    for (int j = 0; j < row; ++j) {
        for (int i = 0; i < col; ++i) {
            int32_t val = 0;
            for (int k = 0; k < acc; ++k) {
                val += static_cast<int32_t>(A_buf(k, j)) * static_cast<int32_t>(B_buf(k % 4, i, k / 4));
            }
            if (val != out(i, j)) {
                std::cerr << "Invalid result at " << i << ", " << j << "\n"
                          << out(i, j) << " != " << val << "\n"
                          << "Matrix dims: " << row << "x" << col << "x" << acc << "\nTile dims: " << tile_x << "x" << tile_y << "x" << tile_r << "\n";
                return false;
            }
        }
    }

    std::cout << "Success!\n";
    return true;
}

bool matmul_bf16(int row, int col, int acc, int tile_x, int tile_y, int tile_r) {
    Var x("x"), y("y");
    Buffer<bfloat16_t> A(acc, row);
    Buffer<bfloat16_t> B(2, col, acc / 2);

    RDom r(0, acc, "acc");

    Func mm("matmul");
    mm(x, y) = cast<float>(0);
    mm(x, y) += cast<float>(cast<float>(A(r.x, y))) * cast<float>(B(r.x % 2, x, r.x / 2));

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

    // uncomment to check the matrices
    // std::cout << "Matrix A\n";
    // print_mat(A, row, acc);
    // std::cout << "Matrix B\n";
    // print_mat_rhs(B, acc, col);

    // std::cout << "result\n";
    // print_mat(out, row, col);

    for (int j = 0; j < row; ++j) {
        for (int i = 0; i < col; ++i) {
            float val = 0.f;
            for (int k = 0; k < acc; ++k) {
                val += static_cast<float>(A(k, j)) * static_cast<float>(B(k % 2, i, k / 2));
            }
            if (!equal_eps(val, out(i, j), 0.03f)) {
                std::cerr << "Invalid result at " << i << ", " << j << "\n"
                          << out(i, j) << " != " << val << "\n"
                          << "Matrix dims: " << row << "x" << col << "x" << acc << "\nTile dims: " << tile_x << "x" << tile_y << "x" << tile_r << "\n";
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

bool run_tests(bool (*fn)(int, int, int, int, int, int), int element_width) {
    return fn(2, 2, 16, 2, 2, 8 / element_width) && fn(4, 4, 8, 4, 4, 8 / element_width) && fn(32, 32, 32, 8, 8, 8 / element_width) && fn(32, 32, 32, 8, 8, 4 / element_width);
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::AVX512_SapphireRapids)) {
        printf("[SKIP] No AMX target enabled\n");
        return 0;
    }

    printf("Running AMX matmul (signed/signed)\n");
    if (!run_tests(matmul_ss, 1)) {
        return 1;
    }

    printf("Running AMX matmul (signed/unsigned)\n");
    if (!run_tests(matmul_su, 1)) {
        return 1;
    }

    printf("Running AMX matmul (unsigned/signed)\n");
    if (!run_tests(matmul_us, 1)) {
        return 1;
    }

    printf("Running AMX matmul (unsigned/unsigned)\n");
    if (!run_tests(matmul_uu, 1)) {
        return 1;
    }

    printf("Running AMX matmul (bf16)\n");
    if (!run_tests(matmul_bf16, 2)) {
        return 1;
    }

    return 0;
}