#include "Halide.h"
#include <gtest/gtest.h>

#include <random>
#include <utility>

using namespace Halide;

namespace {
Expr u8(Expr a) {
    return cast<uint8_t>(std::move(a));
}

/* Do `n` unrolled iterations of game-of-life on a torus */
Func gameOfLife(const ImageParam &input, int n) {
    Var x, y;
    Func in;
    if (n == 1) {
        in(x, y) = input(x, y);
    } else {
        in = gameOfLife(input, n - 1);
        in.compute_root();
    }

    Expr w = input.width(), h = input.height();
    Expr W = (x + w - 1) % w,
         E = (x + 1) % w,
         N = (y + h - 1) % h,
         S = (y + 1) % h;
    Expr livingNeighbors =
        in(W, N) + in(x, N) +
        in(E, N) + in(W, y) +
        in(E, y) + in(W, S) +
        in(x, S) + in(E, S);
    Expr alive = in(x, y) != 0;
    Func output;
    output(x, y) = select(
        livingNeighbors == 3 || (alive && livingNeighbors == 2),
        u8(1),
        u8(0));

    return output;
}

class GameOfLifeTest : public ::testing::Test {
protected:
    ImageParam input{UInt(8), 2};
    std::mt19937 rng{0xC0FFEE};
    std::uniform_int_distribution<> dist01{0, 1};

    void fill_equal(Buffer<uint8_t> &a, Buffer<uint8_t> &b) {
        const int w = a.width();
        const int h = a.height();
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t val = static_cast<uint8_t>(dist01(rng));
                a(x, y) = val;
                b(x, y) = val;
            }
        }
    }
};
}  // namespace

TEST_F(GameOfLifeTest, OuterLoopInC) {
    Buffer<uint8_t> board1(32, 32), board2(32, 32);
    fill_equal(board1, board2);

    Func oneIteration = gameOfLife(input, 1);
    Func twoIterations = gameOfLife(input, 2);

    for (int i = 0; i < 10; i++) {
        input.set(board1);
        board1 = oneIteration.realize({32, 32});
        input.set(board1);
        board1 = oneIteration.realize({32, 32});
        input.set(board2);
        board2 = twoIterations.realize({32, 32});

        for (int yy = 0; yy < 32; yy++) {
            for (int xx = 0; xx < 32; xx++) {
                EXPECT_EQ(board1(xx, yy), board2(xx, yy))
                    << "at iteration " << i << ": x = " << xx << ", y = " << yy;
            }
        }
    }
}

TEST_F(GameOfLifeTest, OuterLoopReduction) {
    Buffer<uint8_t> ref(32, 32), got(32, 32);
    fill_equal(ref, got);

    // Compute a 20-iteration reference using two-iteration steps
    Func twoIterations = gameOfLife(input, 2);
    for (int i = 0; i < 10; i++) {
        input.set(ref);
        ref = twoIterations.realize({32, 32});
    }

    // Now compute 20 iterations using a Halide reduction with ping-pong buffers
    Var x, y, z;
    Func life;

    // Initialize step
    life(x, y, z) = input(x, y);

    // Update step
    Expr w = input.width(), h = input.height();
    RDom t(0, w, 0, h, 0, 21);
    Expr lastT = (t.z + 1) % 2;
    Expr W = (t.x + w - 1) % w,
         E = (t.x + 1) % w,
         N = (t.y + h - 1) % h,
         S = (t.y + 1) % h;
    Expr alive = life(t.x, t.y, lastT) != u8(0);
    Expr livingNeighbors =
        life(W, N, lastT) + life(t.x, N, lastT) +
        life(E, N, lastT) + life(W, t.y, lastT) +
        life(E, t.y, lastT) + life(W, S, lastT) +
        life(t.x, S, lastT) + life(E, S, lastT);
    life(t.x, t.y, t.z % 2) =
        select(livingNeighbors == 3 || (alive && livingNeighbors == 2),
               u8(1),
               u8(0));
    life.compute_root();

    Func output;
    output(x, y) = life(x, y, 1);

    input.set(got);
    output.realize(got);

    for (int yy = 0; yy < 32; yy++) {
        for (int xx = 0; xx < 32; xx++) {
            EXPECT_EQ(ref(xx, yy), got(xx, yy))
                << "x = " << xx << ", y = " << yy;
        }
    }
}
