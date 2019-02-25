#include "Halide.h"

namespace {

using namespace Halide;

class ConvolutionLayer : public Halide::Generator<ConvolutionLayer> {
public:
    Input<Buffer<float>>  input{"input", 4};
    Input<Buffer<float>>  filter{"filter", 4};
    Input<Buffer<float>>  bias{"bias", 1};

    Output<Buffer<float>> relu{"ReLU", 4};

    void generate() {
        // 3x3 winograd convolution.
        // Taken from code written by Benoit Steiner in the onnx converter branch.

        // We'll compile for a single fixed size, because we're
        // benchmarking against MKL, which JITs for a single fixed
        // size.
        const int N = 5, CI = 120, CO = 24, W = 100, H = 80;

        // We only support the case of a 3x3 convolution at the moment. The notation
        // is derived from the one used in the Winograd paper.
        static float BFilter[4][4] = {
            {1, 0, -1, 0}, {0, 1, 1, 0}, {0, -1, 1, 0}, {0, 1, 0, -1}};
        const Halide::Buffer<float> B(&BFilter[0][0], 4, 4);

        static float GFilter[3][4] = {
            {1, 0.5, 0.5, 0}, {0, 0.5, -0.5, 0}, {0, 0.5, 0.5, 1}};
        const Halide::Buffer<float> G{&GFilter[0][0], 4, 3};

        static float AFilter[2][4] = {{1, 1, 1, 0}, {0, 1, -1, -1}};
        const Halide::Buffer<float> A{&AFilter[0][0], 4, 2};

        Var x, y; // Spatial dimensions
        Var b; // Batch index
        Var k; // Output channel
        Var c; // Input channel
        Var alpha, beta; // Dimensions of winograd transform tile

        // Transform the weights
        Func u;
        u(k, c, alpha, beta) = 0.0f;
        for (int r1 = 0; r1 < 3; r1++) {
            for (int r2 = 0; r2 < 3; r2++) {
                for (int alpha = 0; alpha < 4; alpha++) {
                    for (int beta = 0; beta < 4; beta++) {
                        float coeff = G(alpha, r1) * G(beta, r2);
                        if (coeff != 0) {
                            u(k, c, alpha, beta) += coeff * filter(k, r1, r2, c);
                        }
                    }
                }
            }
        }
        Func U;
        U(k, c, alpha, beta) = u(k, c, alpha, beta);

        // Transform a patch of the input
        Func v;
        v(c, x, y, b, alpha, beta) = 0.0f;
        for (int r3 = 0; r3 < 4; r3++) {
            for (int r4 = 0; r4 < 4; r4++) {
                for (int alpha = 0; alpha < 4; alpha++) {
                    for (int beta = 0; beta < 4; beta++) {
                        float coeff = B(r3, alpha) * B(r4, beta);
                        if (coeff != 0) {
                            v(c, x, y, b, alpha, beta) += coeff * input(c, 2*x + r3, 2*y + r4, b);
                        }
                    }
                }
            }
        }
        Func V;
        V(c, x, y, b, alpha, beta) = v(c, x, y, alpha, beta);

        // Do the conv in the transformed domain. It is now a group of
        // 16 sgemms to produce a 2x2 output tile, or four per
        // output. Doing the convs in the primal domain would have
        // been 9 sgemms per output.
        RDom c_r(0, CI);
        M(k, x, y, b, alpha, beta) += U(k, c_r, alpha, beta) * V(c_r, x, y, b, alpha, beta);

        // Transform the output back to the primal domain
        Expr w = 0.0f;
        for (int alpha_r = 0; alpha_r < 4; alpha_r++) {
            for (int beta_r = 0; beta_r < 4; beta_r++) {
                for (int dx = 0; dx < 2; dx++) {
                    for (int dy = 0; dy < 2; dy++) {
                        float coeff = A(alpha_r, dx) * A(beta_r, dy);
                        if (coeff != 0) {
                            w += coeff * M(k, x / 2, y / 2, b, alpha_r, beta_r);
                        }
                    }
                }
            }
        }
        Func winograd_conv;
        winograd_conv(k, x, y, b) = w;

        relu(k, x, y, b) = max(0, winograd_conv(k, x, y, b) + bias(k));

        // MKL JITs code for the specific size and strides, so we'll
        // do the same and ask Halide to compile for this specific
        // size:

        input.dim(0).set_bounds(0, CI).set_stride(1)
            .dim(1).set_bounds(0, W + 2).set_stride(CI)
            .dim(2).set_bounds(0, H + 2).set_stride(CI * (W + 2))
            .dim(3).set_bounds(0, N).set_stride(CI * (W + 2) * (H + 2));

        filter.dim(0).set_bounds(0, CO).set_stride(1)
            .dim(1).set_bounds(0, 3).set_stride(CO)
            .dim(2).set_bounds(0, 3).set_stride(CO * 3)
            .dim(3).set_bounds(0, CI).set_stride(CO * 3 * 3);

        relu
            .bound(c, 0, CO)
            .bound(x, 0, W)
            .bound(y, 0, H)
            .bound(b, 0, N);

        relu.dim(0).set_bounds(0, CO).set_stride(1)
            .dim(1).set_bounds(0, W).set_stride(CO)
            .dim(2).set_bounds(0, H).set_stride(CO * W)
            .dim(3).set_bounds(0, N).set_stride(CO * H * W);

        input.dim(0).set_bounds(0, CI).set_stride(1)
            .dim(1).set_bounds(0, W + 2).set_stride(CI)
            .dim(2).set_bounds(0, H + 2).set_stride(CI * (W + 2))
            .dim(3).set_bounds(0, N).set_stride(CI * (W + 2) * (H + 2));

        filter.dim(0).set_bounds(0, CO).set_stride(1)
            .dim(1).set_bounds(0, 3).set_stride(CO)
            .dim(2).set_bounds(0, 3).set_stride(CO * 3)
            .dim(3).set_bounds(0, CI).set_stride(CO * 3 * 3);

        bias.dim(0).set_bounds(0, CO).set_stride(1);

        if (auto_schedule) {
            // Provide estimates on the input image
            input.dim(0).set_bounds_estimate(0, CI);
            input.dim(1).set_bounds_estimate(0, W + 2);
            input.dim(2).set_bounds_estimate(0, H + 2);
            input.dim(3).set_bounds_estimate(0, N);

            filter.dim(0).set_bounds_estimate(0, CO);
            filter.dim(1).set_bounds_estimate(0, 3);
            filter.dim(2).set_bounds_estimate(0, 3);
            filter.dim(3).set_bounds_estimate(0, CI);

            bias.dim(0).set_bounds_estimate(0, CO);

            // Provide estimates on the pipeline relu
            relu.estimate(x, 0, W)
                .estimate(y, 0, H)
                .estimate(c, 0, CO)
                .estimate(b, 0, N);

        } else {
            // Naive schedule for now
            U.compute_root();
            V.compute_root();
            M.compute_root();
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ConvolutionLayer, conv_layer)
