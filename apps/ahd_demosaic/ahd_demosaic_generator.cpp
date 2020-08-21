#include "Halide.h"

namespace {

struct Tensor {
    Halide::Func f;
    std::vector<int> shape;
    std::string name;
    Halide::RDom r;
    Halide::Func padded;  // defined for conv layers that pad their inputs
};

struct WeightShape {
    int c;  // output channels
    int w;
    int h;
    int pad;
    int stride;
};

class AHD : public Halide::Generator<AHD> {
public:
    Input<Buffer<float>> input{"input", 4};
    /** parameter values for scaling layers **/
    Input<Buffer<float>> g_convex_weights{"g_convex_weights", 4};
    Input<Buffer<float>> g_filter_weights{"g_filter_weights", 4};
    Input<Buffer<float>> chroma_v_weights{"chroma_v_weights", 4};
    Input<Buffer<float>> chroma_h_weights{"chroma_h_weights", 4};
    Input<Buffer<float>> chroma_q_weights{"chroma_q_weights", 4};
    Output<Buffer<float>> output{"output", 4};

    const WeightShape g_filter_ws = {16, 5, 5, 2, 1};
    const WeightShape g_convex_ws = {16, 5, 5, 2, 1};
    const WeightShape chroma_v_ws = {2, 5, 5, 2, 1};
    const WeightShape chroma_h_ws = {2, 5, 5, 2, 1};
    const WeightShape chroma_q_ws = {2, 5, 5, 2, 1};

    Var c{"c"}, x{"x"}, y{"y"}, n{"n"}, xi{"xi"}, yi{"yi"};

    void generate() {

        Tensor input_t;
        std::vector<int> input_shape = {1, 128, 128};
        input_t.f = input;
        input_t.shape = input_shape;

        // green model
        Tensor raw_weights, final_weights, interpolations, prod, green_pred, green;

        raw_weights = conv2D(input_t, g_convex_ws, g_convex_weights, "g_convex");
        final_weights = softmax_layer(raw_weights, 16, "softmax");
        interpolations = conv2D(input_t, g_filter_ws, g_filter_weights, "g_filter");
        prod = prod_layer(final_weights, interpolations, "g_weighted_interpolations");
        green_pred = sumR_layer(prod, "sumR");

        // extract green at red and blue locations and use given green from bayer
        green.shape = input_t.shape;
        green.f(c, x, y, n) = select((x % 2) == (y % 2), input_t.f(c, x, y, n), green_pred.f(c, x, y, n));

        // chroma model
        Tensor chroma_minus_g, chroma_v_diff, chroma_h_diff, chroma_q_diff, chroma_v, chroma_h, chroma_q;
        chroma_minus_g.shape = input_t.shape;
        chroma_minus_g.f(c, x, y, n) = input_t.f(c, x, y, n) - green.f(0, x, y, n);

        chroma_v_diff = conv2D(chroma_minus_g, chroma_v_ws, chroma_v_weights, "chroma_v_diff");
        chroma_h_diff = conv2D(chroma_minus_g, chroma_h_ws, chroma_h_weights, "chroma_h_diff");
        chroma_q_diff = conv2D(chroma_minus_g, chroma_q_ws, chroma_q_weights, "chroma_q_diff");

        std::vector<int> chroma_shape = {2, 128, 128};
        chroma_v.shape = chroma_shape;
        chroma_h.shape = chroma_shape;
        chroma_q.shape = chroma_shape;

        chroma_v.f(c, x, y, n) = chroma_v_diff.f(c, x, y, n) + green.f(0, x, y, n);
        chroma_h.f(c, x, y, n) = chroma_h_diff.f(c, x, y, n) + green.f(0, x, y, n);
        chroma_q.f(c, x, y, n) = chroma_q_diff.f(c, x, y, n) + green.f(0, x, y, n);

        Expr r = select((x % 2) == 0 && (y % 2) == 0, chroma_h.f(0, x, y, n),
                        (x % 2) == 0 && (y % 2) == 1, chroma_q.f(0, x, y, n),
                        (x % 2) == 1 && (y % 2) == 0, input_t.f(0, x, y, n),
                        chroma_v.f(0, x, y, n));
        Expr g = green.f(0, x, y, n);
        Expr b = select((x % 2) == 0 && (y % 2) == 0, chroma_v.f(0, x, y, n),
                        (x % 2) == 0 && (y % 2) == 1, input_t.f(0, x, y, n),
                        (x % 2) == 1 && (y % 2) == 0, chroma_q.f(0, x, y, n),
                        chroma_h.f(0, x, y, n));

        output(c, x, y, n) = mux(c, {r, g, b});

        // estimates
        input.dim(0).set_estimate(0, 1);
        input.dim(1).set_estimate(0, 128);
        input.dim(2).set_estimate(0, 128);
        input.dim(3).set_estimate(0, 32);

        chroma_h_weights.dim(0).set_estimate(0, 2).dim(1).set_estimate(0, 5).dim(2).set_estimate(0, 5).dim(3).set_estimate(0, 1);
        chroma_v_weights.dim(0).set_estimate(0, 2).dim(1).set_estimate(0, 5).dim(2).set_estimate(0, 5).dim(3).set_estimate(0, 1);
        chroma_q_weights.dim(0).set_estimate(0, 2).dim(1).set_estimate(0, 5).dim(2).set_estimate(0, 5).dim(3).set_estimate(0, 1);
        g_convex_weights.dim(0).set_estimate(0, 16).dim(1).set_estimate(0, 5).dim(2).set_estimate(0, 5).dim(3).set_estimate(0, 1);
        g_filter_weights.dim(0).set_estimate(0, 16).dim(1).set_estimate(0, 5).dim(2).set_estimate(0, 5).dim(3).set_estimate(0, 1);

        output.bound(output.args()[0], 0, 3);
        output.bound(output.args()[1], 0, 128);
        output.bound(output.args()[2], 0, 128);
        output.bound(output.args()[3], 0, 32);  // batch size is 32

        if (!auto_schedule) {
            Var xii, yii;
            output.compute_root()
                .tile(x, y, xi, yi, 32, 8)
                .tile(xi, yi, xii, yii, 2, 2)
                .gpu_blocks(x, y, n)
                .gpu_threads(xi, yi)
                .reorder(xii, yii, c, xi, yi, x, y, n)
                .unroll(c)
                .unroll(xii)
                .unroll(yii);

            for (Tensor *t : {&chroma_v_diff, &chroma_h_diff, &chroma_q_diff}) {
                t->f.in()
                    .compute_at(output, x)
                    .tile(x, y, xi, yi, 2, 2)
                    .reorder(xi, yi, c, n, x, y)
                    .unroll(xi)
                    .unroll(yi)
                    .gpu_threads(x, y);
                t->f.compute_at(t->f.in(), x)
                    .reorder(x, y, c, n)
                    .unroll(x)
                    .unroll(y)
                    .update()
                    .reorder(x, y, c, t->r[0], t->r[1], t->r[2], n)
                    .unroll(x)
                    .unroll(y);
                t->padded.compute_at(output, x)
                    .tile(x, y, xi, yi, 3, 3)
                    .reorder(xi, yi, c, n, x, y)
                    .unroll(xi)
                    .unroll(yi)
                    .gpu_threads(x, y);
            }

            green.f.compute_root()
                .tile(x, y, xi, yi, 32, 8, TailStrategy::RoundUp)
                .tile(xi, yi, xii, yii, 2, 2)
                .gpu_blocks(x, y, n)
                .gpu_threads(xi, yi)
                .reorder(xii, yii, c, xi, yi, x, y, n)
                .unroll(c)
                .unroll(xii)
                .unroll(yii);
            green_pred.f.compute_at(green.f, xi)
                .unroll(x)
                .unroll(y)
                .update()
                .unroll(x)
                .unroll(y);
            final_weights.f.compute_at(green.f, xi)
                .unroll(x)
                .unroll(y);

            for (Tensor *t : {&raw_weights, &interpolations}) {
                t->f.in()
                    .compute_root()
                    .tile(x, y, xi, yi, 8, 8)
                    .tile(xi, yi, xii, yii, 2, 2)
                    .reorder(xii, yii, c, xi, yi, x, y, n)
                    .unroll(xii)
                    .unroll(yii)
                    .gpu_blocks(x, y, n)
                    .gpu_threads(c, xi, yi);

                t->f.compute_at(t->f.in(), c)
                    .unroll(c)
                    .unroll(x)
                    .unroll(y)
                    .update()
                    .reorder(c, x, y, t->r[0], t->r[1], t->r[2])
                    .unroll(c)
                    .unroll(x)
                    .unroll(y);

                t->padded.compute_at(t->f.in(), x)
                    .split(Halide::_2, y, yi, 3)
                    .gpu_threads(Halide::_1, y)
                    .unroll(yi)
                    .reorder(yi, Halide::_1, y);
            }
        }
    }

    Func pad(Func f, Expr width, Expr height) {
        Halide::Region bounds(f.dimensions());
        bounds[1].min = 0;
        bounds[1].extent = width;
        bounds[2].min = 0;
        bounds[2].extent = height;
        return Halide::BoundaryConditions::constant_exterior(f, 0.0f, bounds);
    }

    std::vector<int> compute_shape(const Tensor &in, const WeightShape &params) {
        int w = (1.0 / params.stride) * (params.pad * 2 + in.shape[1] - params.w + 1 + params.stride - 1);
        int h = (1.0 / params.stride) * (params.pad * 2 + in.shape[2] - params.h + 1 + params.stride - 1);
        int c = params.c;

        return {c, w, h};
    }

    Tensor conv2D(const Tensor &input, const WeightShape &weight_shape,
                  const Func &weights, const std::string &name) {
        int p = weight_shape.pad;
        Func padded;
        // pad input
        if (p) {
            padded = pad(input.f, input.shape[1], input.shape[2]);
        } else {
            padded = input.f;
        }

        Func w("w");
        Var ci, co;
        w(co, x, y, ci) = weights(co, x, y, ci);

        Func in("in");
        in(c, x, y, n) = padded(c, x, y, n);

        RDom r(0, input.shape[0], 0, weight_shape.w, 0, weight_shape.h);
        Func conv(name + "_conv2D");
        conv(c, x, y, n) += (w(c, r.y, r.z, r.x) *
                             in(r.x, weight_shape.stride * x + r.y - p, weight_shape.stride * y + r.z - p, n));

        if (!auto_schedule) {
            //conv.compute_at(this->output, xi);
        }

        Tensor output;
        output.f = conv;
        output.name = name;
        output.shape = compute_shape(input, weight_shape);
        output.r = r;
        output.padded = padded;
        return output;
    }

    Tensor softmax_layer(const Tensor &input, const int classes, const std::string &name) {
        assert(input.shape[0] == classes);
        RDom r(0, classes);
        Func exp_vals("exp_vals");
        exp_vals(c, x, y, n) = fast_exp(input.f(c, x, y, n));
        Func sum("softmax_sum");
        sum(x, y, n) += exp_vals(r.x, x, y, n);
        Func outvals("softmax_vals");
        outvals(c, x, y, n) = exp_vals(c, x, y, n) / sum(x, y, n);

        if (!auto_schedule) {
            exp_vals.compute_at(outvals, x).unroll(c);
            sum.compute_at(outvals, x).update().unroll(r.x);
            outvals.unroll(c);
        }

        Tensor output;
        output.f = outvals;
        output.name = name;
        output.shape = input.shape;
        output.r = r;
        return output;
    }

    Tensor prod_layer(const Tensor &t1, const Tensor &t2, const std::string &name) {
        assert(t1.shape == t2.shape);
        Func product("product");
        product(c, x, y, n) = t1.f(c, x, y, n) * t2.f(c, x, y, n);
        Tensor output;
        output.f = product;
        output.shape = t1.shape;
        output.name = name;
        return output;
    }

    Tensor sumR_layer(const Tensor &t1, const std::string &name) {
        Func sum_reduction("product");

        RDom r(0, t1.shape[0]);
        sum_reduction(c, x, y, n) += t1.f(r, x, y, n);

        if (!auto_schedule) {
            sum_reduction.update().reorder(x, y, r);
        }

        Tensor output;
        output.f = sum_reduction;
        output.shape = t1.shape;
        output.shape[0] = 1;
        output.name = name;
        output.r = r;
        return output;
    }
};
}  //namespace

HALIDE_REGISTER_GENERATOR(AHD, ahd_demosaic)
