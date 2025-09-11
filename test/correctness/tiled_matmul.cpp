#include "Halide.h"
#include <gtest/gtest.h>
#include <stdio.h>

using namespace Halide;

namespace {
class TiledMatmulTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    void SetUp() override {
        if (!target.has_feature(Target::AVX512_SapphireRapids)) {
            GTEST_SKIP() << "No AMX target enabled";
        }
    }
};

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

template<typename LhsInt8, typename RhsInt8>
void matmul(int row, int col, int acc, int tile_x, int tile_y, int tile_r) {
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

    for (int j = 0; j < row; ++j) {
        for (int i = 0; i < col; ++i) {
            int32_t val = 0;
            for (int k = 0; k < acc; ++k) {
                val += static_cast<int32_t>(A_buf(k, j)) * static_cast<int32_t>(B_buf(k % 4, i, k / 4));
            }
            EXPECT_EQ(out(i, j), val)
                << "i = " << i << ", j = " << j << "\n"
                << "Matrix dims: " << row << "x" << col << "x" << acc << "\n"
                << "Tile dims: " << tile_x << "x" << tile_y << "x" << tile_r;
        }
    }
}

void matmul_bf16(int row, int col, int acc, int tile_x, int tile_y, int tile_r) {
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
    result.realize(out);

    for (int j = 0; j < row; ++j) {
        for (int i = 0; i < col; ++i) {
            float val = 0.f;
            for (int k = 0; k < acc; ++k) {
                val += static_cast<float>(A(k, j)) * static_cast<float>(B(k % 2, i, k / 2));
            }
            EXPECT_NEAR(out(i, j), val, 0.03f)
                << "i = " << i << ", j = " << j << "\n"
                << "Matrix dims: " << row << "x" << col << "x" << acc << "\n"
                << "Tile dims: " << tile_x << "x" << tile_y << "x" << tile_r;
        }
    }
}

void run_tests(void (*fn)(int, int, int, int, int, int), int element_width) {
    fn(2, 2, 16, 2, 2, 8 / element_width);
    fn(4, 4, 8, 4, 4, 8 / element_width);
    fn(32, 32, 32, 8, 8, 8 / element_width);
    fn(32, 32, 32, 8, 8, 4 / element_width);
}
}  // namespace

TEST_F(TiledMatmulTest, SignedSigned) {
    run_tests(matmul<int8_t, int8_t>, 1);
}

TEST_F(TiledMatmulTest, SignedUnsigned) {
    run_tests(matmul<int8_t, uint8_t>, 1);
}

TEST_F(TiledMatmulTest, UnsignedSigned) {
    run_tests(matmul<uint8_t, int8_t>, 1);
}

TEST_F(TiledMatmulTest, UnsignedUnsigned) {
    run_tests(matmul<uint8_t, uint8_t>, 1);
}

TEST_F(TiledMatmulTest, Bfloat16) {
    run_tests(matmul_bf16, 2);
}
