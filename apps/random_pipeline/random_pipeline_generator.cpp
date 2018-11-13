#include "Halide.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <cstdlib>

using namespace Halide;
using namespace Halide::Internal;
using std::vector;
using std::unordered_map;

// Convert a vector of Vars to Exprs. Useful for generating references
// to Funcs.
vector<Expr> make_arguments(vector<Var> vars) {
    vector<Expr> result;
    for (Var i : vars) {
        result.push_back(i);
    }
    return result;
}

std::mt19937 rng;

// Helpers to generate random values.
int rand_int(int min, int max) { return (rng() % (max - min + 1)) + min; }
bool rand_bool() { return rng() % 2 == 0; }
float rand_float() { return rand_int(0, 1 << 30) / (float)(1 << 30); }
float uniform_rand() { return static_cast <float>(rand()) / static_cast <float>(RAND_MAX); }

// Generate random expressions
// Given a vector of expresions and a tree depth, recursively
// generates an expression by combining subexpressions.
// At the base case where depth is 0, we just return a randomly
// chosen input.

Type expr_types[] = { UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32), Float(32) };
const int expr_type_count = sizeof(expr_types)/sizeof(expr_types[0]);

typedef Expr (*make_bin_op_fn)(Expr, Expr);

make_bin_op_fn make_bin_op[] = {
    (make_bin_op_fn)operator+,
    (make_bin_op_fn)operator-,
    (make_bin_op_fn)operator*,
    (make_bin_op_fn)min,
    (make_bin_op_fn)max,
    (make_bin_op_fn)operator/,
    (make_bin_op_fn)operator%,
};

make_bin_op_fn make_bool_bin_op[] = {
    (make_bin_op_fn)operator&&,
    (make_bin_op_fn)operator||,
};

make_bin_op_fn make_comp_bin_op[] = {
    (make_bin_op_fn)operator==,
    (make_bin_op_fn)operator!=,
    (make_bin_op_fn)operator<,
    (make_bin_op_fn)operator<=,
    (make_bin_op_fn)operator>,
    (make_bin_op_fn)operator>=
};

const int bin_op_count = sizeof(make_bin_op) / sizeof(make_bin_op[0]);
const int bool_bin_op_count = sizeof(make_bool_bin_op) / sizeof(make_bool_bin_op[0]);
const int comp_bin_op_count = sizeof(make_comp_bin_op) / sizeof(make_comp_bin_op[0]);

Type random_type() {
    Type T = expr_types[rng()%expr_type_count];
    return T;
}

Expr random_expr(vector<Expr> inputs, int depth, int func_size);

Expr random_condition(vector<Expr> inputs, int depth, int func_size) {
    Expr a = random_expr(inputs, depth, func_size);
    Expr b = random_expr(inputs, depth, func_size);
    int op = rng() % comp_bin_op_count;
    return make_comp_bin_op[op](a, b);
}

// takes a vector of inputs (points in functions) and an expected Type
// if the chosen input is not of the given type, cast it to conform
Expr make_leaf(vector<Expr> inputs) {
    auto chosen_input = inputs[rand_int(0, inputs.size()-1)];
    return chosen_input;
}

Expr random_expr(vector<Expr> inputs, int depth, int func_size) {
    const int op_count = bin_op_count + bool_bin_op_count + 9;
    const int func_size_thresh = 1e4; // if input is too large do not use trig functions

    if (depth <= 0) {
        return make_leaf(inputs);
    }

    // pick a random operation to combine exprs
    int op = rng() % op_count; // ops need to be defined
    switch(op) {
    case 0:  // casting
    {
        // Get a random type
        Type convertT = random_type();
        auto e1 = random_expr(inputs, depth, func_size);
        return cast(convertT, e1);
    }
    case 1: // select operation
    {
        auto c = random_condition(inputs, depth-2, func_size); // arbitrarily chose to make condition expression shorter
        auto e1 = random_expr(inputs, depth-1, func_size);
        auto e2 = random_expr(inputs, depth-2, func_size);
        // make sure e1 and e2 have the same type
        if (e1.type() != e2.type()) {
            e2 = cast(e1.type(), e2);
        }
        return select(c, e1, e2);
    }
    case 2: // unary boolean op
    {
        auto e1 = random_expr(inputs, depth-1, func_size);
        if (e1.type().is_bool()) {
            return !e1;
        }
        break;
    }
    case 3: // sin
    {
        if (func_size > func_size_thresh)
            break;
        auto e1 = random_expr(inputs, depth-1, func_size);
        return sin(e1);
    }
    case 4: // tanh
    {
        if (func_size > func_size_thresh)
            break;
        auto e1 = random_expr(inputs, depth-1, func_size);
        return tanh(e1);
    }
    case 5: // exp
    {
        auto e1 = random_expr(inputs, depth-1, func_size);
        return exp(e1);
    }
    case 6: // sqrt
    {
        auto e1 = random_expr(inputs, depth-1, func_size);
        return sqrt(e1);
    }
    case 7: // log
    {
        auto e1 = random_expr(inputs, depth-1, func_size);
        return log(e1);
    }
    case 8: // condition
    {
        return random_condition(inputs, depth-1, func_size);
    }
    default: // binary op
        make_bin_op_fn maker;
        auto e1 = random_expr(inputs, depth-1, func_size);
        auto e2 = random_expr(inputs, depth-2, func_size);
        if (e1.type().is_bool() && e2.type().is_bool()) {
            maker = make_bool_bin_op[op % bool_bin_op_count];
        } else {
            maker = make_bin_op[op % bin_op_count];
        }

        return maker(e1, e2);
    }

    // selected case did not return an expression, try again
    return random_expr(inputs, depth, func_size);
}

Expr rand_value(Type t) {
    if (t.is_bool()) {
        return cast(t, rand_int(0,1));
    } else if (t.is_int() || t.is_uint()) {
        return cast(t, rand_int(1, 127));
    } else if (t.is_float()) {
        return cast(t, rand_float());
    } else {
        // Shouldn't get here.
        assert(false);
        return undef(t);
    }
}

// Generator to produce a random pipeline. The generated pipeline will
// be solely a function of the seed and the number of stages.
class RandomPipeline : public Halide::Generator<RandomPipeline> {
public:
    int num_stage_types = 18;
    // The random seed to use to generate the pipeline.
    GeneratorParam<int> seed{"seed", 1};
    // The approximate max number of stages to generate in the random pipeline.
    GeneratorParam<int> max_stages{"max_stages", 20};

    Input<Buffer<float>>  input{"input", 3};
    Output<Buffer<float>> output{"output", 3};


    struct Stage {
        Func func;
        int w, h, c; // approx width and height and channels; TODO: ADD 4TH DIMENSION FOR BATCH SIZE

        static constexpr int max_size = 10000000;
        static constexpr int min_size = 100;

        int size() const {
            return w*h*c;
        }

        bool may_increase_size() const {
            return size() < max_size && w <= 8000 && h <= 8000 && c <= 512;
        }

        bool may_reduce_size() const {
            return size() > min_size;
        }

        int random_size_increase_factor() const {
            int sz = size();
            int max_factor = (max_size + sz - 1) / sz;
            if (max_factor <= 1) return 1;
            int log_max_factor = std::ceil(std::log(max_factor) / std::log(2));
            int factor = 1 << rand_int(std::max(1, log_max_factor - 3), log_max_factor);
            return factor;
        }

        int random_size_reduce_factor() const {
            int sz = size();
            int max_factor = (sz + min_size - 1) / min_size;
            if (max_factor <= 1) return 1;
            return std::min(8, 1 << rand_int(1, std::ceil(std::log(max_factor) / std::log(2))));
        }

        int random_out_channels() const {
            int min = (min_size + w * h - 1) / (w * h);
            int max = std::min(512, max_size / (w * h));
            if (min >= max) return min;
            return rand_int(min, max);
        }
    };

    // Generate a random convolution of one dimension of f, statically unrolled.
    Stage convolve(Stage f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]\n";

        vector<Var> args = f.func.args();

        /**
           Expr def = cast(f.func.value().type(), 0);
           for (int i = kernel_min; i <= kernel_max; i++) {
           vector<Expr> coords = make_arguments(f.func.args());
           coords[dim] += i;
           def = def + rand_value(f.func.value().type()) * f.func(coords);
           }
        **/

        // generate random expression using potentially all values in the stencil
        vector<Expr> inputs;
        for (int i = kernel_min; i <= kernel_max; i++) {
            vector<Expr> coords = make_arguments(f.func.args());
            coords[dim] += i;
            inputs.push_back(f.func(coords));
        }
        int min_depth = std::floor(std::log(kernel_max - kernel_min + 1));
        int max_depth = min_depth + 1;
        Expr def = random_expr(inputs, rand_int(min_depth, max_depth), f.size());
        std::cerr << def << "\n";

        Func conv("conv_" + args[dim].name());
        conv(args) = def;

        return {conv, f.w, f.h, f.c};
    }

    // Generate a random convolution of one dimension of f using a reduction.
    Stage convolve_r(Stage f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using +=\n";

        vector<Var> args = f.func.args();

        Func conv("conv_r_" + args[dim].name());
        RDom r(kernel_min, kernel_max - kernel_min + 1);
        vector<Expr> coords = make_arguments(f.func.args());
        coords[dim] += r;
        conv(args) += rand_value(f.func.value().type()) * f.func(coords);

        return {conv, f.w, f.h, f.c};
    }

    // Generate a random convolution of one dimension of f using a reduction with a wrapper
    Stage convolve_w(Stage f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using sum() helper\n";

        vector<Var> args = f.func.args();

        Func conv("conv_w_" + args[dim].name());
        RDom r(kernel_min, kernel_max - kernel_min + 1);
        vector<Expr> coords = make_arguments(f.func.args());
        coords[dim] += r;
        conv(args) = sum(rand_value(f.func.value().type()) * f.func(coords));

        return {conv, f.w, f.h, f.c};
    }

    /*****
     * convolutional net type layers
     *****/
    Stage padding(Stage f) {
        std::cout << "Padding\n";
        std::vector<std::pair<Expr, Expr>> bounds(3); // assuming all stages have 3 dims
        bounds.at(0).first = 0;
        bounds.at(0).second = f.w;
        bounds.at(1).first = 0;
        bounds.at(1).second = f.h;
        bounds.at(2).first = 0;
        bounds.at(2).second = f.c;
        Expr zero = cast(f.func.value().type(), 0);
        return {BoundaryConditions::constant_exterior(f.func, zero, bounds), f.w, f.h, f.c};
    }

    Stage convolve2D(Stage f, int kernel_min, int kernel_max) {
        int conv_type = rand_int(0,2);
        if (conv_type == 0) return convolve2D_unrolled(f, kernel_min, kernel_max);
        if (conv_type == 1) return convolve2D_w(f, kernel_min, kernel_max);
        else return convolve2D_r(f, kernel_min, kernel_max);
    }

    Stage pool2D(Stage f, int kernel_min, int kernel_max) {
        int pool_type = rand_int(0,2);
        if (pool_type == 0) return pool2D_unrolled(f);
        if (pool_type == 1) return pool2D_w(f);
        else return pool2D_r(f);
    }

    Stage activation(Stage f) {
        int activation_type = rand_int(0,1);
        if (activation_type == 0) return relu_layer(f);
        else return tanh_layer(f);
    }

    Stage relu_layer(Stage f) {
        std::cout << "Relu\n";
        Func activation("relu");
        vector<Expr> coords = make_arguments(f.func.args());
        activation(f.func.args()) = max(0.0f, f.func(coords));
        return {activation, f.w, f.h, f.c};
    }

    Stage tanh_layer(Stage f) {
        std::cout << "Tanh\n";
        Func activation("tanh");
        vector<Expr> coords = make_arguments(f.func.args());
        activation(f.func.args()) = tanh(f.func(coords));
        return {activation, f.w, f.h, f.c};
    }

    /*** pooling stages ***/
    Stage pool2D_unrolled(Stage f) { // for now always do 3x3 pool with stride 2
        std::cout << "Pooling 3x3 stride 2\n";
        vector<Var> args = f.func.args();
        Func pooled2D("pooled2D" + args[0].name() + args[1].name());

        int factor = 2;
        int min = -(factor+1)/2;
        int extent = min + factor + 1;
        int scale = extent * extent;

        Expr def = cast(f.func.value().type(), 0);

        // Avoid huge unrolled loops
        if (extent >= 4) return pool2D_r(f);

        // assuming input is 3d: w, h, c
        for (int i = min; i < min + extent; i++) {
            for (int j = min; j < min + extent; j++) {
                vector<Expr> pooled_coords = make_arguments(f.func.args());
                pooled_coords[0] = pooled_coords[0] * factor + i;
                pooled_coords[1] = pooled_coords[1] * factor + j;
                if (def.type().is_bool()) {
                    def = def && f.func(pooled_coords);
                } else {
                    def = def + f.func(pooled_coords);
                }
            }
        }

        if (!def.type().is_bool()) {
            def /= scale;
        }

        pooled2D(args) = def;

        return {pooled2D, (f.w + factor - 1) / factor, (f.h + factor - 1) / factor, f.c};
    }

    // Generate a 3x3 pool with stride 2 of f using a reduction.
    Stage pool2D_r(Stage f) {
        std::cout << "Pooling 3x3 stride 2 using reduction\n";
        vector<Var> args = f.func.args();
        Func pooled2D_r("pool2D_r_" + args[0].name() + args[1].name());

        int factor = 2;
        int min = -(factor+1)/2;
        int extent = min + factor + 1;
        int scale = extent * extent;

        RDom r(min, extent, min, extent);

        vector<Expr> coords = make_arguments(f.func.args());
        Type ty = f.func.value().type();
        coords[0] = coords[0] * factor + r.x;
        coords[1] = coords[1] * factor + r.y;
        if (ty.is_bool()) {
            pooled2D_r(args) = pooled2D_r(args) && f.func(coords);
        } else {
            pooled2D_r(args) += f.func(coords) / scale;
        }

        return {pooled2D_r, (f.w + factor - 1) / factor, (f.h + factor - 1) / factor, f.c};
    }

    // Generate a 3x3 pool with stride 2 of f using a reduction with a wrapper
    Stage pool2D_w(Stage f) {
        std::cout << "Pooling 3x3 stride 2 using sum() helper\n";
        vector<Var> args = f.func.args();
        Func pooled2D_w("pooled2D_w_" + args[0].name() + args[1].name());

        int factor = 2;
        int min = -(factor+1)/2;
        int extent = min + factor + 1;
        int scale = extent * extent;

        RDom r(min, extent, min, extent);

        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] = (coords[0] * factor + r.x);
        coords[1] = (coords[1] * factor + r.y);
        pooled2D_w(args) = sum(cast<float>(f.func(coords))) / scale;

        return {pooled2D_w, (f.w + factor - 1) / factor, (f.h + factor - 1) / factor, f.c};
    }

    /******* set of 2 dimensional (non separable) convs *********/
    // Generate a random convolution of one dimension of f, statically unrolled.
    Stage convolve2D_unrolled(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]\n";

        vector<Var> args = f.func.args();
        // Avoid huge unrolled loops
        if (f.c >= 4) return convolve2D_r(f, kernel_min, kernel_max);

        /**
           Expr def = cast(f.func.value().type(), 0);
           // assuming input is 3d: w, h, c
           for (int c = 0; c < f.c; c++)  {
           for (int i = kernel_min; i <= kernel_max; i++) {
           for (int j = kernel_min; j <= kernel_max; j++) {
           vector<Expr> coords = make_arguments(f.func.args());
           coords[0] += i;
           coords[1] += j;
           coords[2] = c;
           def = def + rand_value(f.func.value().type()) * f.func(coords);
           }
           }
           }
        **/
        vector<Expr> inputs;
        for (int c = 0; c < f.c; c++)  {
            for (int i = kernel_min; i <= kernel_max; i++) {
                for (int j = kernel_min; j <= kernel_max; j++) {
                    vector<Expr> coords = make_arguments(f.func.args());
                    coords[0] += i;
                    coords[1] += j;
                    coords[2] = c;
                    inputs.push_back(f.func(coords));
                }
            }
        }

        int out_channels = f.random_out_channels();
        int kernel_width = kernel_max - kernel_min + 1;
        int min_depth = std::floor(std::log(kernel_width * kernel_width * f.c));
        int max_depth = min_depth + 1;
        int func_size = f.w * f.h * out_channels;

        Expr def = random_expr(inputs, rand_int(min_depth, max_depth), func_size);
        std::cerr << def << "\n";

        Func conv("conv2D_" + args[0].name() + args[1].name());
        conv(args) = def;

        return {conv, f.w, f.h, out_channels};
    }

    // Generate a random convolution of one dimension of f using a reduction.
    Stage convolve2D_r(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using +=\n";

        vector<Var> args = f.func.args();

        Func conv("conv2D_r_" + args[0].name() + args[1].name());
        RDom r(kernel_min, kernel_max - kernel_min + 1,
               kernel_min, kernel_max - kernel_min + 1,
               0, f.c);
        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] += r.x;
        coords[1] += r.y;
        coords[2] = r.z;
        conv(args) += rand_value(f.func.value().type()) * (args[2] + 1) * f.func(coords);

        return {conv, f.w, f.h, f.random_out_channels()};
    }

    // Generate a random convolution of one dimension of f using a reduction with a wrapper
    Stage convolve2D_w(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using sum() helper\n";

        vector<Var> args = f.func.args();

        Func conv("conv2D_w_" + args[0].name() + args[1].name());
        RDom r(kernel_min, kernel_max - kernel_min + 1,
               kernel_min, kernel_max - kernel_min + 1,
               0, f.c);
        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] += r.x;
        coords[1] += r.y;
        coords[2] = r.z;
        // sum() captures free vars in the order found, and the new
        // autoscheduler isn't clever enough to do storage reordering
        // yet, so make sure to put the term that depends on the
        // output channel last.
        conv(args) = sum(rand_value(f.func.value().type()) * f.func(coords) * (args[2] + 1));

        // choose a channel output size - 0.5 prob of doubling channel dim
        return {conv, f.w, f.h, f.random_out_channels()};
    }

    // Generate an upsampling or downsampling of dimension dim by factor.
    Stage upsample(Stage f, int dim, int factor = 0) {
        std::cout << "Upsampling dimension " << dim << " by " << factor << "x\n";

        if (factor == 0) factor = f.random_size_increase_factor();

        Func resampled;

        if (rand_bool()) {
            // Nearest neighbour
            resampled = Func("upsampled_nn_" + f.func.args()[dim].name());
            vector<Expr> resampled_coords = make_arguments(f.func.args());
            resampled_coords[dim] = resampled_coords[dim] / factor;
            resampled(f.func.args()) = f.func(resampled_coords);
        } else {
            // Linear interpolation
            resampled = Func("upsampled_linear_" + f.func.args()[dim].name());
            vector<Expr> resampled_coords = make_arguments(f.func.args());
            Expr x = cast<float>(resampled_coords[dim]) / factor;
            resampled_coords[dim] = cast<int>(floor(x));
            Expr s1 = f.func(resampled_coords);
            resampled_coords[dim] += 1;
            Expr s2 = f.func(resampled_coords);
            Expr fx = x - floor(x);
            resampled(f.func.args()) = lerp(s1, s2, fx);
        }

        Stage s {resampled, f.w, f.h, f.c};
        if (dim == 0) {
            s.w *= factor;
        } else if (dim == 1) {
            s.h *= factor;
        } else {
            assert(false);
        }
        return s;
    }

    Stage downsample(Stage f, int dim, int factor = 0) {
        std::cout << "Downsampling dimension " << dim << " by " << factor << "x\n";

        if (factor == 0) factor = f.random_size_reduce_factor();

        Func resampled;
        if (rand_bool()) {
            // Nearest neighbour
            resampled = Func("downsampled_nn_" + f.func.args()[dim].name());
            vector<Expr> resampled_coords = make_arguments(f.func.args());
            resampled_coords[dim] = resampled_coords[dim] * factor;
            resampled(f.func.args()) = f.func(resampled_coords);
        } else {
            // Averaging down
            resampled = Func("downsampled_box_" + f.func.args()[dim].name());
            vector<Expr> resampled_coords = make_arguments(f.func.args());
            resampled_coords[dim] = resampled_coords[dim] * factor;
            Expr e = cast(f.func.value().type(), 0);
            for (int i = 0; i < factor; i++) {
                resampled_coords[dim] += 1;
                e += f.func(resampled_coords);
            }
            resampled(f.func.args()) = e;
        }

        Stage s {resampled, f.w, f.h, f.c};
        if (dim == 0) {
            s.w = (s.w + factor - 1)/factor;
        } else if (dim == 1) {
            s.h = (s.h + factor - 1)/factor;
        } else {
            assert(false);
        }
        return s;
    }

    Stage binary_op(Stage f, Stage g) {
        std::cout << "Binary op\n";
        if (f.w != g.w || f.h != g.h || f.c != g.c) {
            if (f.size() < g.size()) {
                f = resample_to(f, g.w, g.h, g.c);
            } else {
                g = resample_to(g, f.w, f.h, f.c);
            }
        }

        Func binary("binary_op");

        vector<Expr> inputs = {f.func(f.func.args()), g.func(f.func.args())};
        int min_depth = 1;
        int max_depth = 3;
        int func_size = f.w * f.h * std::min(f.c, g.c);
        Expr def = random_expr(inputs, rand_int(min_depth, max_depth), func_size);
        std::cerr << def << "\n";
        binary(f.func.args()) = def;
        return {binary, f.w, f.h, std::min(f.c, g.c)};
    }

    Stage unary_op(Stage f) {
        std::cout << "Unary op\n";
        Func unary("unary_op");
        vector<Expr> coords = make_arguments(f.func.args());
        int op_type = rand_int(0,3); // exp, log, sqrt, sin

        if (op_type == 0) {
            unary(f.func.args()) = exp(f.func(coords));
            std::cout << "Unary op: exp\n";
        } else if (op_type == 1) {
            unary(f.func.args()) = log(f.func(coords));
            std::cout << "Unary op: log\n";
        } else if (op_type == 2) {
            unary(f.func.args()) = sqrt(f.func(coords));
            std::cout << "Unary op: sqrt\n";
        } else {
            unary(f.func.args()) = sin(f.func(coords));
            std::cout << "Unary op: sin\n";
        }
        return {unary, f.w, f.h, f.c};
    }

    // Generate an all-to-all communication in dimension dim, statically unrolled.
    Stage all_to_all(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << '\n';

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        Expr e = 0.f;
        for (int i = 0; i < f.c; i++) {
            reduction_coords[dim] = i;
            e += f.func(reduction_coords) * (i + 1) * (f.func.args()[dim] + 1);
        }

        Func all("all");
        all(f.func.args()) = e;

        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate an all-to-all communication in dimension dim using an RDom
    Stage all_to_all_r(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << " using += \n";

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        RDom r(0, f.c);
        reduction_coords[dim] = r;
        Func all("all_r");
        all(f.func.args()) += f.func(reduction_coords) * (r + 1) * (f.func.args()[dim] + 1);

        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate an all-to-all communication in dimension dim using an RDom with wrapper func
    Stage all_to_all_w(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << " using += \n";

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        RDom r(0, f.c);
        reduction_coords[dim] = r;
        Func all("all_w");
        all(f.func.args()) = sum(f.func(reduction_coords) * (r + 1) * (f.func.args()[dim] + 1));

        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate a forwards-then-backwards scan along a dimension
    Stage scan(Stage f, int dim) {
        std::cout << "Scan on dimension " << dim << '\n';
        int extent = dim == 0 ? f.w : dim == 1 ? f.h : 3;
        RDom r(1, extent - 1);
        Func scan("scan_" + f.func.args()[dim].name());
        vector<Expr> coords = make_arguments(f.func.args());
        scan(coords) = f.func(coords);
        coords[dim] = r;
        vector<Expr> prev_coords = coords;
        prev_coords[dim] = r-1;
        scan(coords) += scan(prev_coords);
        // Now in reverse
        coords[dim] = extent - r - 1;
        prev_coords[dim] = extent - r;
        scan(coords) += scan(prev_coords);
        return {scan, f.w, f.h, f.c};
    }

    // Transpose
    Stage transpose(Stage f) {
        std::cout << "Transpose\n";
        Func transpose("transpose");
        vector<Expr> coords = make_arguments(f.func.args());
        vector<Expr> swizzled_coords = coords;
        std::swap(swizzled_coords[0], swizzled_coords[1]);

        transpose(coords) = f.func(swizzled_coords);

        return {transpose, f.h, f.w, f.c};
    }

    Stage slice(Stage f, Stage g) {
        std::cout << "Slice\n";
        if (f.c > g.c) {
            std::swap(f, g);
        }

        // Index g's channels using f

        f = resample_to(f, g.w, g.h, 1);

        Func sliced("sliced");
        vector<Expr> coords = make_arguments(f.func.args());
        coords.back() = clamp(cast<int>(f.func(f.func.args())), 0, g.c - 1);
        sliced(f.func.args()) = g.func(coords);

        return {sliced, f.w, f.h, f.c};
    }

    Stage tiled_histogram(Stage f) {
        // Make a histogram of NxN patches of f, preserving total size
        std::cout << "Tiled histogram\n";

        int old_c = f.c;
        f = resample_to(f, f.w, f.h, 1);

        int box_size = 1 << rand_int(1, 3);
        int histogram_buckets = box_size * box_size * old_c;

        RDom r(0, box_size, 0, box_size);
        vector<Expr> from_coords = make_arguments(f.func.args());
        vector<Expr> to_coords = from_coords;

        Func hist("hist");
        hist(f.func.args()) = 0.0f;
        from_coords[0] = to_coords[0] * box_size + r.x;
        from_coords[1] = to_coords[1] * box_size + r.y;
        from_coords[2] = 0;
        to_coords[2] = clamp(cast<int>(f.func(from_coords) * histogram_buckets), 0, histogram_buckets - 1);
        hist(to_coords) += 1;

        return {hist, f.w / box_size, f.h / box_size, histogram_buckets};
    }

    Stage resample_to(Stage f, int w, int h, int c) {
        std::cout << "Resampling from " << f.w << ", " << f.h << ", " << f.c << " to " << w << ", " << h << ", " << c << "\n";
        Stage out = f;
        // First decrease any sizes that need decreasing
        if (out.w > w) {
            int factor = (out.w + w/2) / w;
            if (factor != 1) {
                out = downsample(out, 0, factor);
            }
        }
        if (out.h > h) {
            int factor = (out.h + h/2) / h;
            if (factor != 1) {
                out = downsample(out, 1, (out.h + h/2) / h);
            }
        }
        // Adapt channel count with an all-to-all
        if (out.c != c) {
            out = all_to_all_r(out, 2);
            out.c = c;
        }
        // Increase any sizes that need increasing
        if (out.w < w) {
            int factor = (w + out.w/2) / out.w;
            if (factor != 1) {
                out = upsample(out, 0, factor);
            }
        }
        if (out.h < h) {
            int factor = (h + out.h/2) / out.h;
            if (factor != 1) {
                out = upsample(out, 1, factor);
            }
        }
        std::cout << "Resulting size: " << out.w << ", " << out.h << ", " << out.c << "\n";
        return out;
    }

    Stage cast_stage(Type t, Stage f) {
        Func casted("casted");
        casted(f.func.args()) = cast(t, f.func(f.func.args()));
        return {casted, f.w, f.h, f.c};
    }

    struct TransitionCDF {
        int num_states;
        int size;
        vector<float> cdf;

        void initialize(int n) {
            num_states = n;
            size = n*n;
            for (int i = 0; i < size; i++) {
                cdf.push_back(0.0f);
            }
        }

        float get(int i, int j) {
            int index = i*num_states+j;
            assert(index < size);
            return cdf[index];
        }

        void set(int i, int j, float val) {
            int index = i*num_states+j;
            assert(index < size);
            cdf[index] = val;
        }

        // use inverse transform sampling to sample next state given current state
        int sample_cdf(int state) {
            float sample_val = uniform_rand();
            for (int i = 0; i < num_states; i++) {
                if (get(state, i) >= sample_val) {
                    return i;
                }
            }
        }

        void print() {
            std::cout << std::setprecision(2) << std::fixed;
            for (int i = 0; i < num_states; i++) {
                for (int j = 0; j < num_states; j++) {
                    std::cout << " | " << get(i,j);
                }
                std::cout << " |\n";
            }
        }
    };

    // helper function to generate probability transition matrix between stages
    struct TransitionMatrix {
        int num_states; // number of states
        int size;

        // vector representaiton of 2D transition matrix.
        // value at (i,j) is probability of moving from state i to state j
        vector<float> probabilities;

        float get(int i, int j) {
            int index = i*num_states+j;
            assert(index < size);
            return probabilities[i*num_states+j];
        }

        void set(int i, int j, float val) {
            int index = i*num_states+j;
            assert(index < size);
            probabilities[index] = val;
        }

        void set_cdf(TransitionCDF& cdf) {
            assert(cdf.size == size);
            assert(cdf.num_states == num_states);

            for (int i = 0; i < num_states; i++) {
                float sum = 0.0f;
                for (int j = 0; j < num_states; j++) {
                    sum += get(i,j);
                    cdf.set(i,j,sum);
                }
            }
        }

        void initialize(int n) {
            num_states = n;
            size = n*n;
            // transition to every state equally likely
            float transition_prob = 1.0f/(num_states);
            for (int i = 0; i < size; i++) {
                probabilities.push_back(transition_prob);
            }
        }

        void print() {
            std::cout << std::setprecision(2) << std::fixed;
            for (int i = 0; i < num_states; i++) {
                for (int j = 0; j < num_states; j++) {
                    std::cout << " | " << get(i,j);
                }
                std::cout << " |\n";
            }
        }
    };


    // Generate a random stage using f as an input.
    Stage random_stage(const vector<Stage> &s, TransitionCDF& CDF, int& curr_stage_id) {
        int m = (int)s.size() - 1;
        int i2 = m > 0 ? rand_int(0, m - 1) : 0;
        int i1 = m > 0 ? rand_int(i2 + 1, m) : 0;
        Stage f = s[i1], g = s[i2];

        // generate stage based on transition probabilities
        int stage_type = CDF.sample_cdf(curr_stage_id);
        // set current stage id to the chosen stage for next iteration
        curr_stage_id = stage_type;


        if (stage_type == 0) {
            int dim = rand_int(0, 1);
            int kernel_min = rand_int(-3, 0);
            int kernel_max = rand_int(0, 3);
            return convolve(f, dim, kernel_min, kernel_max);
        } else if (stage_type == 1) {
            int dim = rand_int(0, 1);
            int kernel_min = rand_int(-10, 0);
            int kernel_max = rand_int(0, 10);
            return convolve_r(f, dim, kernel_min, kernel_max);
        } else if (stage_type == 2) {
            int dim = rand_int(0, 1);
            int kernel_min = rand_int(-10, 0);
            int kernel_max = rand_int(0, 10);
            return convolve_w(f, dim, kernel_min, kernel_max);
        } else if (stage_type == 3) {
            int kernel_min = rand_int(-3, 0);
            int kernel_max = rand_int(0, 3);
            return convolve2D(f, kernel_min, kernel_max);
        } else if (stage_type == 4 && f.may_reduce_size() && f.w >= 32 && f.h >= 32) {
            int kernel_min = rand_int(-3, 0);
            int kernel_max = rand_int(0, 3);
            return pool2D(f, kernel_min, kernel_max);
        } else if (stage_type == 5) {
            return activation(f);
        } else if (stage_type == 6) {
            return padding(f);
        } else if (stage_type == 7 && f.may_increase_size()) {
            // For now, only upsample dimensions 0 or 1.
            return upsample(f, rand_int(0, 1));
        } else if (stage_type == 8 && f.may_reduce_size()) {
            // For now, only downsample dimensions 0 or 1.
            return downsample(f, rand_int(0, 1));
        } else if (stage_type == 9) {
            int dim = 2;
            return all_to_all(f, dim);
        } else if (stage_type == 10) {
            int dim = 2;
            return all_to_all_r(f, dim);
        } else if (stage_type == 11) {
            int dim = 2;
            return all_to_all_w(f, dim);
        } else if (stage_type == 12) {
            int dim = rand_int(0, 2);
            return scan(f, dim);
        } else if (stage_type == 13 && false) {
            // TODO: transpose disabled for now because f(x, y) + f(y, x) totally breaks the bounds inference done by the autoscheduler.
            return transpose(f);
        } else if (stage_type == 14 && f.size() < 10000) {
            return unary_op(f);
        } else if (stage_type == 15 && f.w > 32 && f.h > 32) {
            return tiled_histogram(f);
        } else if (stage_type == 16) {
            return slice(f, g);
        } else if (i1 != i2) {
            return binary_op(f, g);
        } else {
            return random_stage(s, CDF, curr_stage_id);
        }
    }

    // Insert transition probabilities for deep network type stages
    void setup_transitions(TransitionMatrix& P) {
        int conv2D_id = 3;
        int pool2D_id = 4;
        int activation_id = 5;
        int padding_id = 6;
        float escape_prob; // prob of leaving a convnet state

        // P(activation | conv) = 0.8
        escape_prob = 0.2f/(num_stage_types-1);
        for (int i = 0; i < num_stage_types; i++) {
            if (i == activation_id) {
                P.set(conv2D_id, i, 0.8f);
            } else {
                P.set(conv2D_id, i, escape_prob);
            }
        }
        // P(padding | activation) = P(pool | activation) = 0.4
        escape_prob = 0.2f/(num_stage_types-2);
        for (int i = 0; i < num_stage_types; i++) {
            if (i == padding_id || i == pool2D_id) {
                P.set(activation_id, i, 0.4f);
            } else {
                P.set(activation_id, i, escape_prob);
            }
        }
        // P(conv | padding) = P(pool | padding) = 0.4
        escape_prob = 0.2f/(num_stage_types-2);
        for (int i = 0; i < num_stage_types; i++) {
            if (i == conv2D_id || i == pool2D_id) {
                P.set(padding_id, i, 0.4f);
            } else {
                P.set(padding_id, i, escape_prob);
            }
        }
        // P(conv | pool) = 0.8
        escape_prob = 0.2f/(num_stage_types-1);
        for (int i = 0; i < num_stage_types; i++) {
            if (i == conv2D_id) {
                P.set(pool2D_id, i, 0.8f);
            } else {
                P.set(pool2D_id, i, escape_prob);
            }
        }
    }

    void generate() {
        // create transition matrix between stages
        TransitionMatrix P;
        P.initialize(num_stage_types);
        setup_transitions(P);
        TransitionCDF CDF;
        CDF.initialize(num_stage_types);
        P.set_cdf(CDF);

        Var x("x"), y("y"), c("c");

        Func first;
        first(x, y, c) = input(x, y, c);

        rng.seed((int)seed);

        vector<Stage> stages;
        // Assume input starts at ~2000x2000
        stages.emplace_back(Stage{first, 2000, 2000, 3});
        // set starting stage type to a random stage
        int curr_stage_id = rand_int(0, num_stage_types-1);

        for (int i = 0; i < max_stages - 2; i++) {
            std::cout << "Approx size: " << stages.back().w << ", " << stages.back().h << ", " << stages.back().c << "\n";
            Stage next = random_stage(stages, CDF, curr_stage_id);
            stages.push_back(next);
            if (!auto_schedule) {
                stages.back().func.compute_root().reorder(x, c, y).vectorize(x, 8).parallel(y, 8);
            }
        }

        Stage tail = stages.back();

        // Resample back to the correct resolution
        tail = resample_to(tail, 2000, 2000, 3);
        Stage casted = cast_stage(output.type(), tail);
        output = casted.func;

        if (!auto_schedule) {
            output.compute_root().reorder(x, c, y).vectorize(x, 8).parallel(y);
        }

        if (auto_schedule) {
            input.dim(0).set_bounds_estimate(0, 2000)
                .dim(1).set_bounds_estimate(0, 2000)
                .dim(2).set_bounds_estimate(0, 3);
            output.estimate(output.args()[0], 0, 2000);
            output.estimate(output.args()[1], 0, 2000);
            output.estimate(output.args()[2], 0, 3);

            output.dim(0).set_bounds(0, 2000);
            output.dim(1).set_bounds(0, 2000);
            output.dim(2).set_bounds(0, 3);
        }
    }
};



HALIDE_REGISTER_GENERATOR(RandomPipeline, random_pipeline)
