#include "Halide.h"

#define SZ 2048

using namespace Halide;

class BilinearUpsampleRoundUp : public Generator<BilinearUpsampleRoundUp> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    void generate() {
        Var x, y;

        Expr in00 = input(x / 2, y / 2);
        Expr in10 = input(x / 2 + 1, y / 2);
        Expr in01 = input(x / 2, y / 2 + 1);
        Expr in11 = input(x / 2 + 1, y / 2 + 1);

        // Widen
        in00 = cast<uint16_t>(in00);
        in10 = cast<uint16_t>(in10);
        in01 = cast<uint16_t>(in01);
        in11 = cast<uint16_t>(in11);

        // Somewhat naive version
        /*
        Expr out00 = 9 * in00 + 3 * (in01 + in10) + in11;
        Expr out10 = 9 * in10 + 3 * (in00 + in11) + in01;
        Expr out01 = 9 * in01 + 3 * (in00 + in11) + in10;
        Expr out11 = 9 * in11 + 3 * (in01 + in10) + in00;
        */

        // Version which attempts to share more work. It's slightly faster.

        // Do a widening add of each opposing pair
        Expr diag0011 = in00 + in11;
        Expr diag1001 = in10 + in01;

        // The widened sum of all four
        Expr avg = diag0011 + diag1001;

        // Each output is a shift-and-add of several of the above.
        Expr out00 = avg + diag1001 * 2 + in00 * 8;
        Expr out10 = avg + diag0011 * 2 + in10 * 8;
        Expr out01 = avg + diag0011 * 2 + in01 * 8;
        Expr out11 = avg + diag1001 * 2 + in11 * 8;

        // Round and narrow
        out00 = cast<uint8_t>((out00 + 8) / 16);
        out10 = cast<uint8_t>((out10 + 8) / 16);
        out01 = cast<uint8_t>((out01 + 8) / 16);
        out11 = cast<uint8_t>((out11 + 8) / 16);

        output(x, y) = select(x % 2 == 0 && y % 2 == 0, out00,
                              x % 2 == 1 && y % 2 == 0, out10,
                              x % 2 == 0 && y % 2 == 1, out01,
                              out11);

        Var xi, yi;

        // The unrolled tiling removes the select
        output.tile(x, y, xi, yi, 2, 2).vectorize(x, 64).unroll(xi).unroll(yi);

        output.dim(0).set_bounds(0, SZ);
        output.dim(1).set_bounds(0, SZ);
    }
};

class BilinearUpsampleAveraging : public Generator<BilinearUpsampleAveraging> {
public:
    Input<Buffer<uint8_t>> input{"input", 2};
    Output<Buffer<uint8_t>> output{"output", 2};

    Expr avg_u(Expr a, Expr b) {
        return Internal::rounding_halving_add(a, b);
    }

    Expr avg_d(Expr a, Expr b) {
        return Internal::halving_add(a, b);
    }

    Expr avg1339(Expr v3, Expr v0, Expr v1, Expr v2) {
        Expr v4 = avg_d(v0, v1);  //  Kernel: 1 1 0 0  : -0.25 -0.5 0
        Expr v5 = avg_u(v0, v1);  //  Kernel: 1 1 0 0  : 0.25 0 0.5
        Expr v6 = avg_u(v2, v3);  //  Kernel: 0 0 1 1  : 0.25 0 0.5
        Expr v7 = avg_u(v4, v6);  //  Kernel: 1 1 1 1  : 0.25 -0.25 0.75
        Expr v8 = avg_u(v5, v7);  //  Kernel: 3 3 1 1  : 0.5 0 1
        Expr v9 = avg_d(v2, v8);  //  Kernel: 3 3 9 1  : 0 -0.5 0.5
        // Note the function args were permuted to turn the 3 3 9 1 into a 1 3 3 9
        return v9;
    }

    void generate() {
        Var x, y;

        Expr in00 = input(x / 2, y / 2);
        Expr in10 = input(x / 2 + 1, y / 2);
        Expr in01 = input(x / 2, y / 2 + 1);
        Expr in11 = input(x / 2 + 1, y / 2 + 1);

        Expr out00 = avg1339(in11, in01, in10, in00);
        Expr out10 = avg1339(in01, in00, in11, in10);
        Expr out01 = avg1339(in10, in00, in11, in01);
        Expr out11 = avg1339(in00, in01, in10, in11);

        output(x, y) = select(x % 2 == 0 && y % 2 == 0, out00,
                              x % 2 == 1 && y % 2 == 0, out10,
                              x % 2 == 0 && y % 2 == 1, out01,
                              out11);

        Var xi, yi;

        output.tile(x, y, xi, yi, 2, 2).vectorize(x, 64).unroll(xi).unroll(yi);

        output.dim(0).set_bounds(0, SZ);
        output.dim(1).set_bounds(0, SZ);
    }
};

HALIDE_REGISTER_GENERATOR(BilinearUpsampleAveraging, bilinear_upsample_averaging);
HALIDE_REGISTER_GENERATOR(BilinearUpsampleRoundUp, bilinear_upsample_round_up);
