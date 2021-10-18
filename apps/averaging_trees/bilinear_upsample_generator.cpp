#include "Halide.h"

using namespace Halide;

enum class Method {
    Averaging,
    RoundUp,
    RoundToEven,
    Dither,
    Float
};

class BilinearUpsample : public Generator<BilinearUpsample> {
public:
    Input<Buffer<>> input{"input", 2};
    Input<Buffer<uint8_t>> noise{"noise", 2};
    Output<Buffer<>> output{"output", 2};

    GeneratorParam<Method> method{
        "method",
        Method::Averaging,
        {{"averaging", Method::Averaging},
         {"round_up", Method::RoundUp},
         {"round_to_even", Method::RoundToEven},
         {"dither", Method::Dither},
         {"float", Method::Float}}};

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

        Expr out00, out10, out01, out11;

        if (method == Method::Float) {
            out00 = (9 * in00 + 3 * (in01 + in10) + in11) / 16.0f;
            out10 = (9 * in10 + 3 * (in00 + in11) + in01) / 16.0f;
            out01 = (9 * in01 + 3 * (in00 + in11) + in10) / 16.0f;
            out11 = (9 * in11 + 3 * (in01 + in10) + in00) / 16.0f;
        } else if (method == Method::Averaging) {
            out00 = avg1339(in11, in01, in10, in00);
            out10 = avg1339(in01, in00, in11, in10);
            out01 = avg1339(in10, in00, in11, in01);
            out11 = avg1339(in00, in01, in10, in11);
        } else {

            // Widen
            if (!input.type().is_float()) {
                in00 = cast<uint16_t>(in00);
                in10 = cast<uint16_t>(in10);
                in01 = cast<uint16_t>(in01);
                in11 = cast<uint16_t>(in11);
            }

            out00 = 9 * in00 + 3 * (in01 + in10) + in11;
            out10 = 9 * in10 + 3 * (in00 + in11) + in01;
            out01 = 9 * in01 + 3 * (in00 + in11) + in10;
            out11 = 9 * in11 + 3 * (in01 + in10) + in00;

            // Round and narrow
            if (method == Method::RoundUp) {
                auto round = [](const Expr &e) {
                    return cast<uint8_t>((e + 8) / 16);
                };
                out00 = round(out00);
                out10 = round(out10);
                out01 = round(out01);
                out11 = round(out11);
            } else if (method == Method::RoundToEven) {
                auto round = [](const Expr &e) {
                    Expr x = e + 7;
                    Expr adj = (x >> 4) & 1;
                    return cast<uint8_t>((x + adj) / 16);
                };
                out00 = round(out00);
                out10 = round(out10);
                out01 = round(out01);
                out11 = round(out11);
            } else {
                // Dither

                auto round = [&](const Expr &e) {
                    Expr n = noise(x % 32, y % 32);

                    if (input.type().is_float()) {
                        return cast<uint8_t>(clamp(Halide::floor((e * 256.0f + n) / 16.0f), 0.f, 255.f));
                    } else {
                        return cast<uint8_t>((e + n) / 16);
                    }
                };
                out00 = round(out00);
                out10 = round(out10);
                out01 = round(out01);
                out11 = round(out11);
            }
        }
        output(x, y) = select(x % 2 == 0 && y % 2 == 0, out00,
                              x % 2 == 1 && y % 2 == 0, out10,
                              x % 2 == 0 && y % 2 == 1, out01,
                              out11);

        Var xi, yi;

        const int vec = natural_vector_size(output.type());

        // The unrolled tiling removes the select
        output
            .tile(x, y, xi, yi, 2 * vec, 2, TailStrategy::RoundUp)
            .unroll(xi, 2)
            .unroll(yi)
            .vectorize(xi);

        output.dim(0).set_min(0);
        output.dim(1).set_min(0);
    }
};

HALIDE_REGISTER_GENERATOR(BilinearUpsample, bilinear_upsample);
