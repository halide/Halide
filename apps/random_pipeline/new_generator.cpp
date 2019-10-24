#include "Halide.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <cstdlib>
#include <unordered_map>
#include <limits>
#include <iostream>
#include <fstream>
#include <sstream>
#include "schema.h"


using namespace Halide;
using namespace Halide::Internal;
using std::vector;
using std::string;
using std::unordered_map;
using Halide::Derivative;


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

// Generate random expressions. Given a vector of expresions and a
// tree depth, recursively generates an expression by combining
// subexpressions.  At the base case where depth is 0, we just return
// a randomly chosen input.
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

Expr avg(Expr a, Expr b) {
    Type wider = a.type().with_bits(a.type().bits() * 2);
    return cast(a.type(), (cast(wider, a) + b + 1)/2);
}

Expr random_expr_inner(vector<Expr> inputs, int depth, int func_size);

Expr random_condition(vector<Expr> inputs, int depth, int func_size) {
    Expr a = random_expr_inner(inputs, depth, func_size);
    Expr b = random_expr_inner(inputs, depth, func_size);
    int op = rng() % comp_bin_op_count;
    return make_comp_bin_op[op](a, b);
}

// takes a vector of inputs (points in functions) and an expected Type
// if the chosen input is not of the given type, cast it to conform
Expr make_leaf(vector<Expr> inputs) {
    auto chosen_input = inputs[rand_int(0, inputs.size()-1)];
    return chosen_input;
}

Expr random_expr_inner(vector<Expr> inputs, int depth, int func_size) {
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
        auto e1 = random_expr_inner(inputs, depth, func_size);
        return cast(convertT, e1);
    }
    case 1: // select operation
    {
        auto c = random_condition(inputs, depth-2, func_size); // arbitrarily chose to make condition expression shorter
        auto e1 = random_expr_inner(inputs, depth-1, func_size);
        auto e2 = random_expr_inner(inputs, depth-2, func_size);
        // make sure e1 and e2 have the same type
        if (e1.type() != e2.type()) {
            e2 = cast(e1.type(), e2);
        }
        return select(c, e1, e2);
    }
    case 2: // unary boolean op
    {
        auto e1 = random_expr_inner(inputs, depth-1, func_size);
        if (e1.type().is_bool()) {
            return !e1;
        }
        break;
    }
    case 3: // sin
    {
        if (func_size > func_size_thresh)
            break;
        auto e1 = random_expr_inner(inputs, depth-1, func_size);
        return sin(cast<float>(e1));
    }
    case 4: // tanh
    {
        if (func_size > func_size_thresh) {
            // Don't use expensive ops if the function is very large
            break;
        }
        auto e1 = random_expr_inner(inputs, depth-1, func_size);
        return tanh(cast<float>(e1));
    }
    case 5: // exp
    {
        auto e1 = random_expr_inner(inputs, depth-1, func_size);
        return fast_exp(cast<float>(e1));
    }
    case 6: // sqrt
    {
        auto e1 = random_expr_inner(inputs, depth-1, func_size);
        return sqrt(cast<float>(e1));
    }
    case 7: // log
    {
        auto e1 = random_expr_inner(inputs, depth-1, func_size);
        return fast_log(cast<float>(e1));
    }
    case 8: // condition
    {
        return random_condition(inputs, depth-1, func_size);
    }
    default: // binary op
        make_bin_op_fn maker;
        auto e1 = random_expr_inner(inputs, depth-1, func_size);
        auto e2 = random_expr_inner(inputs, depth-2, func_size);
        if (e1.type().is_bool() && e2.type().is_bool()) {
            maker = make_bool_bin_op[op % bool_bin_op_count];
        } else {
            maker = make_bin_op[op % bin_op_count];
        }

        return maker(e1, e2);
    }

    // selected case did not return an expression, try again
    return random_expr_inner(inputs, depth, func_size);
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

Expr random_expr(vector<Expr> inputs, int depth, int func_size) {
    for (auto &e : inputs) {
        e = Internal::simplify(e);
    }

    for (int attempts = 0; attempts < 10; attempts++) {
        Expr result =
            Internal::simplify(Internal::common_subexpression_elimination(random_expr_inner(inputs, depth, func_size)));

        class Checker : public Internal::IRMutator {
        public:
            Expr mutate(const Expr &e) override {
                exprs_to_find.erase(e);
                return IRMutator::mutate(e);
            }
            using Internal::IRMutator::mutate;
            std::set<Expr, Internal::IRDeepCompare> exprs_to_find;
            Checker(const vector<Expr> &inputs) {
                for (const auto &e : inputs) {
                    exprs_to_find.insert(e);
                }
            }
        } checker(inputs);

        checker.mutate(result);

        // Double check all the inputs are used
        if (!checker.exprs_to_find.empty()) {
            std::cerr << "In random expression: " << result << "\n"
                      << "The following expressions were unused:\n";
            for (auto &e : checker.exprs_to_find) {
                std::cerr << e << "\n";
            }
        } else {
            return result;
        }
    }

    // We're having a hard time generating an expression that uses all the inputs. Just sum them.
    Type t = inputs[0].type();
    if (t.is_bool()) {
        t = UInt(8);
    }
    Expr result = cast(t, 0);
    for (const auto &e : inputs) {
        result += e;
    }
    return result;
}

static void hash_combine(uint64_t &h, uint64_t next) {
    // From boost
    h ^= (next + 0x9e3779b9 + (h<<6) + (h>>2));
}

// Generator to produce a random pipeline. The generated pipeline will
// be solely a function of the seed and the number of stages.
// Modified from random_pipeline_generator used by autoscheduler to have 
// learnable parameters (currently just the weights used by the conv stages)
template<bool training>
class RandomPipeline : public Halide::Generator<RandomPipeline<training>> {
public:    
    template<typename T> using Input = GeneratorInput<T>;
    template<typename T> using Output = GeneratorOutput<T>;
    using dim_shape = std::tuple<int,int>;
    using Generator<RandomPipeline<training>>::auto_schedule;
    using Generator<RandomPipeline<training>>::get_pipeline;
    // types for buffers
    using inputT = int16_t;
    Type inputHT = Halide::type_of<inputT>();
    using outputT = int16_t;
    using lossT = float;
    using paramT = float;
    Type paramHT = Halide::type_of<paramT>();

    int num_stage_types = 21;

    // The random seed to use to generate the pipeline.
    GeneratorParam<int> seed{"seed", 1};
    // The number of input buffers to this random pipeline
    GeneratorParam<int> num_input_buffers{"num_input_buffers", 4};
    // The size of the input buffers ASSUMING ALL ARE THE SAME SIZE FOR NOW
    GeneratorParam<int> input_w{"input_w", 14};
    GeneratorParam<int> input_h{"input_h", 14};
    GeneratorParam<int> input_c{"input_c", 3};
    GeneratorParam<int> output_w{"output_w", 10};
    GeneratorParam<int> output_h{"output_h", 10};
    GeneratorParam<int> output_c{"output_c", 3};
    // The number of output buffers to this random pipeline
    GeneratorParam<int> num_output_buffers{"num_output_buffers", 1};
    // The approximate max number of stages to generate in the random pipeline.
    GeneratorParam<int> max_stages{"max_stages", 20};
    // how much to shift input image by to avoid boundary issues 
    GeneratorParam<int> shift{"shift", 2}; 
    
    Input<int> batch_size{ "batch_size", 1 };
    Input<float> learning_rate{ "learning_rate", 1.0f };
    Input<int> timestep{ "timestep", 0 }; // Needed by ADAM
  
    // store generated pipeline information
    vector<DAGSchema> dag_schema;
    vector<FuncDefSchema> func_def_schema;

    // for avoiding duplicates 
    std::unordered_map<uint64_t, int>* hashes;

    // where to store databse information on generated pipelines
    string DAG_csv;
    string FuncDef_csv;

    void set_dag_file(string fname) {
        DAG_csv = fname;
    }

    void set_funcdef_file(string fname) {
        FuncDef_csv = fname;
    }

    void set_hashes(std::unordered_map<uint64_t, int>* used_hashes) {
        hashes = used_hashes;
    }

    void do_random_pipeline_schedule(Halide::Pipeline p) {
        // Compute an environment
        std::map<string, Function> env;
        for (Func &f : p.outputs()) {
            std::map<string, Function> more_funcs = find_transitive_calls(f.function());
            env.insert(more_funcs.begin(), more_funcs.end());
        }

        for (auto &f : env) {
            Func(f.second).compute_root();
        }
        return;
    }

    void set_input_weight_shape(Input<Halide::Buffer<float>>* weight, 
                                dim_shape s0, 
                                dim_shape s1, 
                                dim_shape s2, 
                                dim_shape s3) {
        weight->dim(0).set_bounds(std::get<0>(s0), std::get<1>(s0));
        weight->dim(1).set_bounds(std::get<0>(s1), std::get<1>(s1));
        weight->dim(2).set_bounds(std::get<0>(s2), std::get<1>(s2));
        weight->dim(3).set_bounds(std::get<0>(s3), std::get<1>(s3));
    }

    void set_output_weight_shape(Output<Halide::Buffer<paramT>>* weight,
                                 dim_shape s0,
                                 dim_shape s1,
                                 dim_shape s2,
                                 dim_shape s3) {
        weight->dim(0).set_bounds(std::get<0>(s0), std::get<1>(s0));
        weight->dim(0).set_bounds_estimate(std::get<0>(s0), std::get<1>(s0));
        weight->bound(weight->args()[0], std::get<0>(s0), std::get<1>(s0));
        weight->estimate(weight->args()[0], std::get<0>(s0), std::get<1>(s0));

        weight->dim(1).set_bounds(std::get<0>(s1), std::get<1>(s1));
        weight->dim(1).set_bounds_estimate(std::get<0>(s1), std::get<1>(s1));
        weight->bound(weight->args()[1], std::get<0>(s1), std::get<1>(s1));
        weight->estimate(weight->args()[1], std::get<0>(s1), std::get<1>(s1));

        weight->dim(2).set_bounds(std::get<0>(s2), std::get<1>(s2));
        weight->dim(2).set_bounds_estimate(std::get<0>(s2), std::get<1>(s2));
        weight->bound(weight->args()[2], std::get<0>(s2), std::get<1>(s2));
        weight->estimate(weight->args()[2], std::get<0>(s2), std::get<1>(s2));
        
        weight->dim(3).set_bounds(std::get<0>(s3), std::get<1>(s3));
        weight->dim(3).set_bounds_estimate(std::get<0>(s3), std::get<1>(s3));
        weight->bound(weight->args()[3], std::get<0>(s3), std::get<1>(s3));
        weight->estimate(weight->args()[3], std::get<0>(s3), std::get<1>(s3));

        weight->dim(weight->dimensions()-1).set_bounds(0, 4);
        weight->dim(weight->dimensions()-1).set_bounds_estimate(0, 4);
    }

    void backprop(Halide::ImageParam &weights,
                  Output<Halide::Buffer<paramT>>* grad, 
                  const Derivative &d, 
                  Expr learning_rate, 
                  Expr timestep) {
        std::vector<Expr> args(weights.dimensions()+1);
        for (auto &e : args) e = Var();
        (*grad)(args) = undef<paramT>();
        // We'll report back the new weights and the loss gradients,
        // and update the ADAM state. Depending on the mode the caller
        // is in, it may use the new weights, or it may just send the
        // loss gradients up to an ADAM server.
        args.back() = 0;
        FuncRef new_weight = (*grad)(args);
        args.back() = 1;
        FuncRef smoothed_deriv = (*grad)(args);
        args.back() = 2;
        FuncRef smoothed_second_moment = (*grad)(args);
        args.back() = 3;
        FuncRef loss_gradient = (*grad)(args);

        args.pop_back();
        Expr current_weight = weights(args);

        loss_gradient = d(weights)(args);
        std::cout << "loss gradient: " << loss_gradient << std::endl;
        std::cout << "loss gradient update definitons: " << std::endl;
        for (auto& def : loss_gradient.function().updates()) {
            for (auto& expr : def.values()) {
                std::cout << expr << std::endl;
            }
        }

        // Update the first and second moment estimates
        //smoothed_deriv = 0.9f * smoothed_deriv + 0.1f * loss_gradient;
        std::cout << "\nsmoothed deriv: " << smoothed_deriv << std::endl;
        std::cout << "smoothed deriv update definitons: " << std::endl;
        for (auto& def : smoothed_deriv.function().updates()) {
            for (auto& expr : def.values()) {
                std::cout << expr << std::endl;
            }
        }
        //smoothed_second_moment = 0.999f * smoothed_second_moment + 0.001f * pow(loss_gradient, 2);
      
        // Correction to account for the fact that the smoothed_deriv
        // and smoothed_second_moment start at zero when t == 0
        Expr smoothed_deriv_correction = 1 / (1 - pow(0.9f, timestep + 1));
        Expr smoothed_second_moment_correction = 1 / (1 - pow(0.999f, timestep + 1));

        std::cout << "\nsmoothed deriv expr: " << (Expr)smoothed_deriv << std::endl;
        std::cout << smoothed_deriv.function().name() << std::endl;
        // Update the weights
        //Expr step = learning_rate * smoothed_deriv * smoothed_deriv_correction;
        //step /= sqrt(smoothed_second_moment * smoothed_second_moment_correction) + 1e-5f;
        Expr step = learning_rate * d(weights)(args);

        std::cout << "step: " << step << std::endl;
        new_weight = current_weight - step;
    }

    void set_upcast_types(Type input_type, Type& mult_type, Type& sum_type) {
        if (input_type.is_bool()) {
            mult_type = UInt(8);
            sum_type = UInt(8);
        } else if (!input_type.is_float() && rand_int(0,1)) {
            int input_bits = input_type.bits();
            int mult_bits = std::min(32, 2*input_bits);
            int sum_bits = std::min(32, 2*mult_bits);
            mult_type = input_type.with_bits(mult_bits);
            sum_type = input_type.with_bits(sum_bits);
        } else {
            mult_type = input_type;
            sum_type = input_type;
        }
        return;
    }

    void set_downcast_type(Type input_type, Type& output_type) {
        if (input_type.is_int() && rand_int(0,1)) {
            int input_bits = input_type.bits();
            int factor = rand_int(1, 2) * 2;
            int output_bits = std::max(8, input_bits/factor);
            output_type = Int(output_bits);
        } else {
            output_type = input_type;
        }
        return;
    }

    struct Stage {
        Func func;

        // approx width and height and channels. Used to preserve
        // spatial scale when combining stages, and to track the total
        // sizes of things.
        int w, h, c;

        static constexpr int max_size = 10000;
        static constexpr int min_size = 100;
        static constexpr int max_stride = 3; // for convs and pools

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

    vector<Stage> stages; // the list of stages we will generate

    // USED BY INTERP 2TAP STAGES
    typedef std::tuple<Stage, vector<Expr>, vector<Expr>, Func> InterpStageAndCoords;

    // Generate a random convolution of one dimension of f, statically unrolled.
    Stage convolve(Stage f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]\n";

        vector<Var> args = f.func.args();

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
        std::cout << conv.name() << " has input: " << f.func.name() << "\n";

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
        std::cout << conv.name() << " has input: " << f.func.name() << "\n";

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
        std::cout << conv.name() << " has input: " << f.func.name() << "\n";

        return {conv, f.w, f.h, f.c};
    }

    // Generate a padding layer (a zero boundary condition)
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
        std::cout << "Padding has input: " << f.func.name() << "\n";

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
        if (pool_type == 0) return pool2D_unrolled(f, kernel_min, kernel_max);
        if (pool_type == 1) return pool2D_w(f, kernel_min, kernel_max);
        else return pool2D_r(f, kernel_min, kernel_max);
    }

    Stage activation(Stage f) {
        return relu_layer(f);
    }

    Stage relu_layer(Stage f) {
        std::cout << "Relu\n";
        Func activation("relu");
        // if input type is int, downcast with 50% chance
        Type input_type = f.func.value().type();
        Type output_type;
        set_downcast_type(input_type, output_type);

        vector<Expr> coords = make_arguments(f.func.args());
        activation(f.func.args()) = max(cast(output_type, 0), cast(output_type,f.func(coords)));
        std::cout << activation.name() << " has input: " << f.func.name() << "\n";
        return {activation, f.w, f.h, f.c};
    }

    Stage tanh_layer(Stage f) {
        std::cout << "Tanh\n";
        Func activation("tanh");
        // if input type is int, downcast with 50% chance
        Type input_type = f.func.value().type();
        Type output_type;
        set_downcast_type(input_type, output_type);

        vector<Expr> coords = make_arguments(f.func.args());
        Expr exp_pos = fast_exp(2 * cast<float>(f.func(coords)));
        activation(f.func.args()) = (exp_pos - 1) / (exp_pos + 1);
        return {activation, f.w, f.h, f.c};
    }

    Stage pool2D_unrolled(Stage f, int kernel_min, int kernel_max) {
        vector<Var> args = f.func.args();
        Func pooled2D("pooled2D" + args[0].name() + args[1].name());
        int stride = f.random_size_reduce_factor();

        int extent = kernel_max - kernel_min + 1;
        int scale = extent * extent;

        if (stride > extent) {
            stride = 1;
        }

        std::cout << "Pooling unrolled with stride: " << stride
                  << " and kernel [ " << kernel_min
                  << ", " << kernel_max << "]\n";

        Expr def = cast(f.func.value().type(), 0);

        // Avoid huge unrolled loops
        if (extent >= 4) return pool2D_r(f, kernel_min, kernel_max);

        // assuming input is 3d: w, h, c
        for (int i = kernel_min; i <= kernel_max; i++) {
            for (int j = kernel_min; j <= kernel_max; j++) {
                vector<Expr> pooled_coords = make_arguments(f.func.args());
                pooled_coords[0] = pooled_coords[0] * stride + i;
                pooled_coords[1] = pooled_coords[1] * stride + j;
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
        std::cout << pooled2D.name() << " has input: " << f.func.name() << "\n";
        return {pooled2D, (f.w + stride - 1) / stride, (f.h + stride - 1) / stride, f.c};
    }

    Stage pool2D_r(Stage f, int kernel_min, int kernel_max) {
        vector<Var> args = f.func.args();
        Func pooled2D_r("pool2D_r_" + args[0].name() + args[1].name());
        int stride = f.random_size_reduce_factor();
        int extent = kernel_max - kernel_min + 1;
        int scale = extent * extent;

        if (stride > extent) {
            stride = 1;
        }

        std::cout << "Pooling using += with stride: " << stride << " and kernel [ " << kernel_min
          << ", " << kernel_max << "]\n";

        RDom r(kernel_min, extent,
               kernel_min, extent);

        vector<Expr> coords = make_arguments(f.func.args());
        Type ty = f.func.value().type();
        coords[0] = coords[0] * stride + r.x;
        coords[1] = coords[1] * stride + r.y;
        if (ty.is_bool()) {
            pooled2D_r(args) = const_true();
            pooled2D_r(args) = pooled2D_r(args) && f.func(coords);
        } else {
            pooled2D_r(args) += f.func(coords) / scale;
        }
        std::cout << pooled2D_r.name() << " has input: " << f.func.name() << "\n";
        return {pooled2D_r, (f.w + stride - 1) / stride, (f.h + stride - 1) / stride, f.c};
    }

    Stage pool2D_w(Stage f, int kernel_min, int kernel_max) {
        vector<Var> args = f.func.args();
        Func pooled2D_w("pooled2D_w_" + args[0].name() + args[1].name());
        int stride = f.random_size_reduce_factor();
        int extent = kernel_max - kernel_min + 1;
        int scale = extent * extent;

        if (stride > extent) {
            stride = 1;
        }

        std::cout << "Pooling using sum() with stride: " << stride << " and kernel [ " << kernel_min
          << ", " << kernel_max << "]\n";

        RDom r(kernel_min, extent,
               kernel_min, extent);

        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] = (coords[0] * stride + r.x);
        coords[1] = (coords[1] * stride + r.y);
        pooled2D_w(args) = sum(cast<float>(f.func(coords))) / scale;
        std::cout << pooled2D_w.name() << " has input: " << f.func.name() << "\n";

        return {pooled2D_w, (f.w + stride - 1) / stride, (f.h + stride - 1) / stride, f.c};
    }

    // Convolution in the deep learning sense of the word.
    Stage convolve2D_unrolled(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]\n";

        vector<Var> args = f.func.args();
        // Avoid huge unrolled loops
        if (f.c >= 4) return convolve2D_r(f, kernel_min, kernel_max);

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
        std::cout << conv.name() << " has input: " << f.func.name() << "\n";
        return {conv, f.w, f.h, out_channels};
    }

    // Generate a random convolution of one dimension of f using a reduction.
    // Changing this to use input and output parameters for weights
    Stage convolve2D_r(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using +=\n";

        vector<Var> args = f.func.args();
        Func conv("conv2D_r_" + args[0].name() + args[1].name());

        int stride = f.random_size_reduce_factor();
        int extent = kernel_max - kernel_min + 1;
        int output_channels = f.random_out_channels();

        if (stride > extent) {
            stride = 1;
        }

        RDom r(kernel_min, extent,
               kernel_min, extent,
               0, f.c);
        // if input type is int, upcast with 50% chance
        // (KMA) input type is always float for now becuase the weights will always have type float
        Type mult_type, sum_type;
        Type input_type = Float(32); 
        set_upcast_types(input_type, mult_type, sum_type);

        Input<Buffer<paramT>>* conv_in_weights = Halide::Internal::GeneratorBase::add_input<Buffer<paramT>>(
                f.func.name() + "_conv_in_weight", 4);
        
        Halide::ImageParam conv_in_weights_p(paramHT, 4, f.func.name() + "_conv_in_weight");
        input_param_dummies[f.func.name()] = conv_in_weights_p;
        param_shapes[f.func.name()] = std::make_tuple(std::make_tuple(0, f.c), 
                                                std::make_tuple(0, extent),
                                                std::make_tuple(0, extent),
                                                std::make_tuple(0, output_channels));
        input_params[f.func.name()] = conv_in_weights;

        if (training) { 
            Output<Buffer<paramT>>* conv_out_weights = Halide::Internal::GeneratorBase::add_output<Buffer<paramT>>(
                    f.func.name() + "_conv_out_weight", 5);
            output_params[f.func.name()] = conv_out_weights;
        }

        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] = coords[0] * stride + r.x; // only stride in w and h
        coords[1] = coords[1] * stride + r.y;
        coords[2] = r.z;
        conv(args) += cast(sum_type, cast(mult_type, input_param_dummies[f.func.name()](r.z, r.x-kernel_min, r.y-kernel_min, args[2]) * f.func(coords)));

        std::cout << conv.name() << " has input: " << f.func.name() << "\n";
        Stage out {conv, f.w, f.h, output_channels};
        out.w = (out.w + stride - 1)/stride;
        out.h = (out.h + stride - 1)/stride;
        return out;
    }

    // Generate a random convolution of one dimension of f using a reduction with a wrapper
    Stage convolve2D_w(Stage f, int kernel_min, int kernel_max) {
        std::cout << "Convolving 2D dimension 1: " << 0
                  << " dimension 2: " << 1
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << " using sum() helper\n";

        vector<Var> args = f.func.args();
        Func conv("conv2D_w_" + args[0].name() + args[1].name());
        // if input type is int, upcast with 50% chance
        Type mult_type, sum_type;
        Type input_type = Float(32); 
        set_upcast_types(input_type, mult_type, sum_type);

        int stride = f.random_size_reduce_factor();
        int extent = kernel_max - kernel_min + 1;
        int output_channels = f.random_out_channels();

        if (stride > extent) {
            stride = 1;
        }

        RDom r(kernel_min, extent,
               kernel_min, extent,
               0, f.c);
        vector<Expr> coords = make_arguments(f.func.args());
        coords[0] = coords[0] * stride + r.x;
        coords[1] = coords[1] * stride + r.y;
        coords[2] = r.z;

        Input<Buffer<paramT>>* conv_in_weights = Halide::Internal::GeneratorBase::add_input<Buffer<paramT>>(
                f.func.name() + "_conv_in_weight", 4);
        input_params[f.func.name()] = conv_in_weights;
        Halide::ImageParam conv_in_weights_p(paramHT, 4, f.func.name() + "_conv_in_weight");
        input_param_dummies[f.func.name()] = conv_in_weights_p;
        param_shapes[f.func.name()] = std::make_tuple(std::make_tuple(0, f.c), 
                                                std::make_tuple(0, extent),
                                                std::make_tuple(0, extent),
                                                std::make_tuple(0, output_channels));
        
        if (training) {
            Output<Buffer<paramT>>* conv_out_weights = Halide::Internal::GeneratorBase::add_output<Buffer<paramT>>(
                    f.func.name() + "_conv_out_weight", 5);
            output_params[f.func.name()] = conv_out_weights;
        }

        // sum() captures free vars in the order found, and the new
        // autoscheduler isn't clever enough to do storage reordering
        // yet, so make sure to put the term that depends on the
        // output channel last.
        conv(args) = sum(cast(sum_type, cast(mult_type, input_param_dummies[f.func.name()](r.z, r.x-kernel_min, r.y-kernel_min, args[2]) * f.func(coords))));
        std::cout << conv.name() << " has input: " << f.func.name() << "\n";

        // choose a channel output size - 0.5 prob of doubling channel dim
        Stage out {conv, f.w, f.h, output_channels};
        out.w = (out.w + stride -1)/stride;
        out.h = (out.h + stride -1)/stride;
        return out;
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
            Expr x = resampled_coords[dim];
            resampled_coords[dim] = x / factor;
            Expr s1 = f.func(resampled_coords);
            resampled_coords[dim] += 1;
            Expr s2 = f.func(resampled_coords);
            x = x % factor;

            Type mult_type, sum_type;
            Type input_type = f.func.value().type();
            set_upcast_types(input_type, mult_type, sum_type);
            s1 = cast(sum_type, s1);
            s2 = cast(sum_type, s2);

            resampled(f.func.args()) = cast(input_type, ((factor - x) * s1 + x * s2) / (2*factor));
        }

        std::cout << resampled.name() << " has input: " << f.func.name() << "\n";
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
        std::cout << resampled.name() << " has input: " << f.func.name() << "\n";
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

    // A random pointwise combination of two stages.
    Stage binary_op(Stage f, Stage g) {
        std::cout << "Binary op\n";
        // They are first resized to match scales.
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
        std::cout << binary.name() << " has inputs: " << f.func.name() << ", " << g.func.name() << "\n";
        return {binary, f.w, f.h, std::min(f.c, g.c)};
    }

    Stage unary_op(Stage f) {
        std::cout << "Unary op" << std::endl;
        Func unary("unary_op");
        vector<Expr> coords = make_arguments(f.func.args());
        int op_type = rand_int(0,2); // exp, log, sqrt

        if (op_type == 0) {
            unary(f.func.args()) = fast_exp(cast<float>(f.func(coords)));
            std::cout << "Unary op: exp\n";
        } else if (op_type == 1) {
            unary(f.func.args()) = fast_log(cast<float>(f.func(coords)));
            std::cout << "Unary op: log\n";
        } else if (op_type == 2) {
            unary(f.func.args()) = sqrt(cast<float>(f.func(coords)));
            std::cout << "Unary op: sqrt\n";
        }
        std::cout << unary.name() << " has input: " << f.func.name() << std::endl;
        return {unary, f.w, f.h, f.c};
    }

    // Generate an all-to-all communication in dimension dim,
    // statically unrolled. Currently only every applied over the
    // channels dimension.
    Stage all_to_all(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << std::endl;

        if (f.c > 16) return all_to_all_r(f, dim);

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        Expr e = 0.f;
        for (int i = 0; i < f.c; i++) {
            reduction_coords[dim] = i;
            e += f.func(reduction_coords) * ((i + 1) * f.c + (f.func.args()[dim] + 1));
        }

        Func all("all");
        all(f.func.args()) = e;
        std::cout << all.name() << " has input: " << f.func.name() << std::endl;
        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate an all-to-all communication in dimension dim using an RDom
    Stage all_to_all_r(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << " using += " << std::endl;

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        RDom r(0, f.c);
        reduction_coords[dim] = r;
        Func all("all_r");
        all(f.func.args()) += f.func(reduction_coords) * ((r + 1) * f.c + (f.func.args()[dim] + 1));
        std::cout << all.name() << " has input: " << f.func.name() << std::endl;

        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate an all-to-all communication in dimension dim using an RDom with wrapper func
    Stage all_to_all_w(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << " using += " << std::endl;

        vector<Expr> reduction_coords = make_arguments(f.func.args());
        RDom r(0, f.c);
        reduction_coords[dim] = r;
        Func all("all_w");
        all(f.func.args()) = sum(f.func(reduction_coords) * ((r + 1) * f.c + (f.func.args()[dim] + 1)));
        std::cout << all.name() << " has input: " << f.func.name() << std::endl;

        return {all, f.w, f.h, f.random_out_channels()};
    }

    // Generate a forwards-then-backwards scan along a dimension
    Stage scan(Stage f, int dim) {
        std::cout << "Scan on dimension " << dim << std::endl;
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
        std::cout << scan.name() << " has input: " << f.func.name() << std::endl;
        return {scan, f.w, f.h, f.c};
    }

    /** normalize a grid of values for slicing **/
    Stage normalize2DGrid(Stage f) {
        // indexing math won't work if width or height = 1
        assert(f.w > 1 && f.h > 1 && f.c == 1);
        RDom r(0, f.w, 0, f.h, 0, 1); // assume last dim extent = 1
        Func normed;
        Func max_scan_x, min_scan_x, max_scan_y, min_scan_y;
        max_scan_x(f.func.args()) = std::numeric_limits<float>::min(); 
        min_scan_x(f.func.args()) = std::numeric_limits<float>::max();
        max_scan_y(f.func.args()[1]) = std::numeric_limits<float>::min(); 
        min_scan_y(f.func.args()[1]) = std::numeric_limits<float>::max();

        max_scan_x(r.x, r.y, r.z) = select(f.func(r.x, r.y, r.z) >  max_scan_x(r.x-1, r.y, r.z), 
                                        f.func(r.x, r.y, r.z),
                                        max_scan_x(r.x-1, r.y, r.z));
        min_scan_x(r.x, r.y, r.z) = select(f.func(r.x, r.y, r.z) <  min_scan_x(r.x-1, r.y, r.z), 
                                        f.func(r.x, r.y, r.z),
                                        min_scan_x(r.x-1, r.y, r.z));

        max_scan_y(r.y) = select(max_scan_x(f.w-1, r.y, r.z) > max_scan_y(r.y-1),
                                max_scan_x(f.w-1, r.y, r.z),
                                max_scan_y(r.y-1));   
  
        min_scan_y(r.y) = select(min_scan_x(f.w-1, r.y, r.z) < min_scan_y(r.y-1),
                                min_scan_x(f.w-1, r.y, r.z),
                                min_scan_y(r.y-1));     

        Expr f_max = max_scan_y(f.h-1);
        Expr f_min = min_scan_y(f.h-1);

        normed(f.func.args()) = (f.func(f.func.args()) - f_min) / (f_max - f_min + 0.0001f);
        return {normed, f.w, f.h, f.c};
    }

    // Do a data-dependent looking into one stage using another as the
    // index.
    Stage slice(Stage f, Stage g) {
        std::cout << "Slice" << std::endl;
        if (f.c > g.c) {
            std::swap(f, g);
        }

        // Index g's channels using f
        f = resample_to(f, g.w, g.h, 1);
        // normalize f's values for indexing
        Stage normed = normalize2DGrid(f);

        Func sliced("sliced");

        vector<Expr> int_coords_below = make_arguments(normed.func.args());
        int_coords_below.back() = clamp(cast<int>(floor(cast<float>(g.c) * (normed.func(normed.func.args())))), 0, g.c - 2);

        vector<Expr> int_coords_above = make_arguments(normed.func.args()); 
        int_coords_above.back() = int_coords_below.back() + 1;

        vector<Expr> float_coords = make_arguments(normed.func.args());
        float_coords.back() = clamp(cast<float>(g.c) * (normed.func(normed.func.args())), 0.0f, cast<float>(g.c - 1));

        Expr wc = float_coords.back() - int_coords_below.back();
          
        sliced(normed.func.args()) = g.func(int_coords_below) * wc + g.func(int_coords_above) * (1.0f - wc);
        std::cout << sliced.name() << " has inputs: " << f.func.name() << ", " << g.func.name() << std::endl;
        return {sliced, normed.w, normed.h, normed.c};
    }

    // Construct a tiled histogram of regions of a stage.
    Stage tiled_histogram(Stage f) {
        std::cout << "Tiled histogram" << std::endl;

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
        std::cout << hist.name() << " has input: " << f.func.name() << std::endl;

        return {hist, f.w / box_size, f.h / box_size, histogram_buckets};
    }

    // Resample a stage to a different size.
    Stage resample_to(Stage f, int w, int h, int c) {
        std::cout << "Resampling from " << f.w << ", " << f.h << ", " << f.c << " to " << w << ", " << h << ", " << c << std::endl;
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
        std::cout << "Resulting size: " << out.w << ", " << out.h << ", " << out.c << std::endl;
        std::cout << out.func.name() << " has input: " << f.func.name() << std::endl;
        return out;
    }

    Stage cast_stage(Type t, Stage f) {
        Func casted("casted");
        std::cout << "Casting " << f.func.name() << std::endl;
        casted(f.func.args()) = cast(t, f.func(f.func.args()));
        std::cout << casted.name() << " has input: " << f.func.name() << std::endl;
        return {casted, f.w, f.h, f.c};
    }
    
    /** generates interpolation coords and makes sure that the coordinates are not the same **/
    bool random_coords(vector<Expr> &coords1, vector<Expr> &coords2, uint64_t &h1, uint64_t &h2) {
        int choice = rand_int(0, 2);
        int offset11, offset12, offset21, offset22;
        offset11 = offset12 = offset21 = offset22 = 1;

        switch (choice) {
            case 0:
                break; 
            case 1:
                offset11 = 2;
                coords1[0] += 1;
                break;
            case 2:
                offset11 = 0;
                coords1[0] -= 1;
        }
        choice = rand_int(0, 2);
        switch (choice) {
            case 0:
                break; 
            case 1:
                offset12 = 2;
                coords1[1] += 1;
                break;
            case 2:
                offset12 = 0;
                coords1[1] -= 1;
        }
        choice = rand_int(0, 2);
        switch (choice) {
            case 0:
                break; 
            case 1:
                offset21 = 2;
                coords2[0] += 1;
                break;
            case 2:
                offset21 = 0;
                coords2[0] -= 1;
        }
        choice = rand_int(0, 2);
        switch (choice) {
            case 0:
                break; 
            case 1:
                offset22 = 2;
                coords2[1] += 1;
                break;
            case 2:
                offset22 = 0;
                coords2[1] -= 1;
        }
        
        hash_combine(h1, (offset11)*10 + (offset12));
        hash_combine(h2, (offset21)*10 + (offset22));

        bool success = !( equal(coords1[0], coords2[0]) && equal(coords1[1], coords2[1]) );
        return success;
    }

    InterpStageAndCoords interp2Tap_stage(vector<Stage> &s, uint64_t &h, int input_id=-1) {
        uint64_t stage_type = 1;
        Func interp("interp2Tap");
        if (input_id < 0) {
            // pick a random input
            input_id = rand_int(0, s.size()-1);
        }
        Stage input_s = s[input_id];
        std::cout << interp.name() << " is Interp 2 tap on " << input_s.func.name() << std::endl;
        // generate random coordinates to use 
        vector<Expr> coords1 = make_arguments(input_s.func.args());
        vector<Expr> coords2 = make_arguments(input_s.func.args());
        uint64_t h_coords1, h_coords2;
        h_coords1 = h_coords2 = 0;
        while (!random_coords(coords1, coords2, h_coords1, h_coords2)) {
            coords1 = make_arguments(input_s.func.args());
            coords2 = make_arguments(input_s.func.args());
            h_coords1 = h_coords2 = 0;
        }

        std::cout << "coords1: " << coords1[0] << "," << coords1[1] << std::endl;
        std::cout << "coords2: " << coords2[0] << "," << coords2[1] << std::endl;
        Expr value = avg(input_s.func(coords1), input_s.func(coords2));
        interp(input_s.func.args()) = value;

        std::cout << interp(input_s.func.args()) << " = ";
        std::cout << value << std::endl;

        Stage interp_s = {interp, input_s.w, input_s.h, input_s.c};
    
        hash_combine(h, stage_type); 
        hash_combine(h, input_id);
        hash_combine(h, h_coords1 + h_coords2);
        
        // create schema 
        dag_schema.emplace_back((uint64_t)seed, std::string(interp.name()), 
                                (uint64_t)stage_type, (uint64_t)s.size(), 
                                (uint64_t)input_id, std::string(input_s.func.name()));
        
        std::ostringstream left;
        left << interp(input_s.func.args());
        const auto left_string = left.str();

        std::ostringstream right;
        right << value;
        const auto right_string = right.str();

        func_def_schema.emplace_back((uint64_t)seed, std::string(interp.name()), 
                                     (uint64_t)s.size(), 
                                     left_string + " = " + right_string); 

        return std::make_tuple(interp_s, coords1, coords2, input_s.func); 
    }

    bool same_vars(vector<Var> v1, vector<Var> v2) {
        assert(v1.size() == v2.size());
        for (int i = 0; i < (int)v1.size(); i++) {
            if (v1[i].name() != v2[i].name()) return false;
        }
        return true;
    }

    Stage select_interp2Tap_stage(vector<Stage> &s, uint64_t &h, int input_id=-1) {
        uint64_t stage_type = 2;
        Func selectInterp("selectInterp2Tap");

        Stage s1, s2;
        vector<Expr> s1coords1, s1coords2, s2coords1, s2coords2;
        Func s1input, s2input;

        uint64_t h_interp1, h_interp2;
        h_interp1 = h_interp2 = 0;
        std::cout << selectInterp.name() << " is Select Interp" << std::endl; 

        std::tie(s1, s1coords1, s1coords2, s1input) = interp2Tap_stage(s, h_interp1, input_id);
        s.push_back(s1);
        dag_schema.emplace_back( (uint64_t)seed, std::string(selectInterp.name()), 
                                 (uint64_t)stage_type, (uint64_t)s.size(), 
                                 dag_schema.back().stage_index, std::string(dag_schema.back().func_name) );

        std::tie(s2, s2coords1, s2coords2, s2input) = interp2Tap_stage(s, h_interp2);
        s.push_back(s2);
        dag_schema.emplace_back( (uint64_t)seed, std::string(selectInterp.name()), 
                                 (uint64_t)stage_type, (uint64_t)s.size(), 
                                 dag_schema.back().stage_index, std::string(dag_schema.back().func_name) );

        std::cout << selectInterp.name() << " selects from: " << s1.func.name() << " and " << s2.func.name() << std::endl; 

        // make sure that the two funcs have the same function arguments and size
        vector<Expr> s1args = make_arguments(s1input.args());
        assert(same_vars(s1input.args(), s2input.args()));

        assert(s1.w == s2.w && s1.h == s2.h && s1.c == s2.c);

        Expr diff1 = absd(s1input(s1coords1), s1input(s1coords2));      
        Expr diff2 = absd(s2input(s2coords1), s2input(s2coords2));

        Expr value = select(diff1 < diff2, s1.func(s1args), s2.func(s1args));
        selectInterp(s1args) = value;

        std::cout << selectInterp(s1args) << " = ";
        std::cout << value << std::endl;
        
        hash_combine(h, stage_type); 
        hash_combine(h, h_interp1 + h_interp2);

        std::ostringstream left;
        left << selectInterp(s1args); 
        const auto left_string = left.str();

        std::ostringstream right;
        right << value;
        const auto right_string = right.str();

        func_def_schema.emplace_back( (uint64_t)seed, std::string(selectInterp.name()), (uint64_t)s.size(), 
                                      left_string + " = " + right_string );

        return {selectInterp, s1.w, s1.h, s1.c};
    }

    InterpStageAndCoords correct_interp2Tap_stage(vector<Stage> &s, uint64_t &h, int use_id=-1) {
        uint64_t stage_type = 3;
        Func correctInterp("correctInterp2Tap");
        Stage input_s, ref_s, interp_s;
        int input_id, ref_id, interp_id;

        // pick a random input buffers
        input_id = rand_int(0, s.size() - 1);
        ref_id = rand_int(0, s.size() - 1);
        interp_id = rand_int(0, s.size() - 1);

        // if stage id is given, use that as one of the input functions 
        if (use_id >= 0) {
            // pick a buffer to fill given input
            int buff_id = rand_int(0, 2);
            switch (buff_id) {
                case 0:
                    input_id = use_id;
                    break;
                case 1: 
                    ref_id = use_id;
                    break;
                case 2:
                    interp_id = use_id;
                    break;
            }
        }

        input_s = s[input_id];
        ref_s = s[ref_id];
        interp_s = s[interp_id];

        Func input_f  = input_s.func;
        Func ref_f    = ref_s.func;
        Func interp_f = interp_s.func;
        
        std::cout << correctInterp.name() << " is Corrected Interp 2 Tap on: " << input_f.name() << " with correction funcs: " << ref_f.name() << " and " << interp_f.name() << std::endl;

        // generate random coordinates to use 
        vector<Expr> coords1 = make_arguments(input_f.args());
        vector<Expr> coords2 = make_arguments(input_f.args());

        uint64_t h_coords1, h_coords2;
        h_coords1 = h_coords2 = 0;
        while (!random_coords(coords1, coords2, h_coords1, h_coords2)) {
            coords1 = make_arguments(input_s.func.args());
            coords2 = make_arguments(input_s.func.args());
            h_coords1 = h_coords2 = 0;
        }
        std::cout << "coords1: " << coords1[0] << "," << coords1[1] << std::endl;
        std::cout << "coords2: " << coords2[0] << "," << coords2[1] << std::endl;

        vector<Expr> coords = make_arguments(input_f.args()); 
        Expr correction = ref_f(coords) - avg(interp_f(coords1), interp_f(coords2));
        Expr value = correction + avg(input_f(coords1), input_f(coords2));

        // using unit-respecting correction application
        /**
        Expr average = avg(interp_f(coords1), interp_f(coords2));
        Expr correction = select(average <= 0, 1, ref_f(coords) / average);
        Expr value = correction * avg(input_f(coords1), input_f(coords2));
        **/

        correctInterp(coords) = value;

        Stage correct_interp_s = {correctInterp, input_s.w, input_s.h, input_s.c};  
        
        std::cout << correctInterp(coords) << " = ";
        std::cout << value << std::endl;

        hash_combine(h, 3);
        hash_combine(h, input_id);
        hash_combine(h, ref_id);
        hash_combine(h, interp_id);
        hash_combine(h, h_coords1 + h_coords2);

        dag_schema.emplace_back( (uint64_t)seed, std::string(correctInterp.name()), (uint64_t)stage_type, 
                                 (uint64_t)s.size(), (uint64_t)input_id,  std::string(input_f.name()) );
        dag_schema.emplace_back( (uint64_t)seed, std::string(correctInterp.name()), (uint64_t)stage_type, 
                                 (uint64_t)s.size(), (uint64_t)ref_id, std::string(ref_f.name()) );
        dag_schema.emplace_back( (uint64_t)seed, std::string(correctInterp.name()), (uint64_t)stage_type, 
                                 (uint64_t)s.size(), (uint64_t)interp_id, std::string(interp_f.name()) );

        std::ostringstream left;
        left << correctInterp(coords);
        const auto left_string = left.str();

        std::ostringstream right;
        right << value;
        const auto right_string = right.str();

        func_def_schema.emplace_back( (uint64_t)seed, std::string(correctInterp.name()), (uint64_t)s.size(),
                                      left_string + " = " + right_string );

        return std::make_tuple(correct_interp_s, coords1, coords2, input_s.func);
    }

    Stage select_correct_interp2Tap_stage(vector<Stage> &s, uint64_t &h, int input_id=-1) {
        uint64_t stage_type = 4;
        Func selectInterp("selectCorrectInterp2Tap");
        std::cout << selectInterp.name() << " is Select Corrected Interp" << std::endl;

        Stage s1, s2;
        vector<Expr> s1coords1, s1coords2, s2coords1, s2coords2;
        Func s1input, s2input;

        uint64_t h_interp1, h_interp2;
        h_interp1 = h_interp2 = 0;

        std::tie(s1, s1coords1, s1coords2, s1input) = correct_interp2Tap_stage(s, h_interp1, input_id);
        s.push_back(s1);
        dag_schema.emplace_back( (uint64_t)seed, std::string(selectInterp.name()), (uint64_t)stage_type, 
                                 (uint64_t)s.size(), dag_schema.back().stage_index, 
                                 std::string(dag_schema.back().func_name) );

        std::tie(s2, s2coords1, s2coords2, s2input) = correct_interp2Tap_stage(s, h_interp2);
        s.push_back(s2);
        dag_schema.emplace_back( (uint64_t)seed, std::string(selectInterp.name()), (uint64_t)stage_type, 
                                 (uint64_t)s.size(), dag_schema.back().stage_index, 
                                 std::string(dag_schema.back().func_name) );

        std::cout << selectInterp.name() << " selects from: " << s1.func.name() << " and " << s2.func.name() << std::endl; 

        vector<Expr> s1args = make_arguments(s1input.args());
        assert(same_vars(s1input.args(), s2input.args()));
        assert(s1.w == s2.w && s1.h == s2.h && s1.c == s2.c);

        Expr diff1 = absd(s1input(s1coords1), s1input(s1coords2));
        Expr diff2 = absd(s2input(s2coords1), s2input(s2coords2));

        Expr value = select(diff1 < diff2, s1.func(s1args), s2.func(s1args));
        selectInterp(s1args) = value;
        
        std::cout << selectInterp(s1args) << " = ";
        std::cout << value << std::endl;
    
        hash_combine(h, 4);
        hash_combine(h, h_interp1 + h_interp2);

        std::ostringstream left;
        left << selectInterp(s1args);
        const auto left_string = left.str();

        std::ostringstream right;
        right << value;
        const auto right_string = right.str();

        func_def_schema.emplace_back( (uint64_t)seed, std::string(selectInterp.name()), (uint64_t)s.size(), 
                                      left_string + " = " + right_string );

        return {selectInterp, s1.w, s1.h, s1.c};
    }

    // Add a random new stage onto the end of the pipeline that can choose any of the 
    // input buffers or previous stages as an input. Note that the type of random stage
    // will determine how many inputs it needs 
    Stage random_stage(vector<Stage> &s, uint64_t &h, int input_id=-1) {
        int m = (int)s.size() - 1;
        int i2 = m > 0 ? rand_int(0, m - 1) : 0;
        int i1 = m > 0 ? rand_int(i2 + 1, m) : 0;
        
        int stage_type = rand_int(16, 19); // KMA: only select from demosaic template stages
        Stage f = s[i1], g = s[i2];
      
        std::cout <<  "STAGE TYPE: " << stage_type << std::endl;
        std::cout.flush();
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
            int kernel_min = rand_int(-5, 0);
            int kernel_max = rand_int(0, 5);
            return convolve2D(f, kernel_min, kernel_max);
        } else if (stage_type == 4 && f.may_reduce_size() && f.w >= 32 && f.h >= 32) {
            int kernel_min = rand_int(-5, 0);
            int kernel_max = rand_int(0, 5);
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
        } else if (stage_type == 13 && f.size() < 10000) {
            return unary_op(f);
        } else if (stage_type == 14 && f.w > 32 && f.h > 32) {
            return tiled_histogram(f);
        } else if (stage_type == 15) {
            return slice(f, g);
        } else if (stage_type == 16) {
            Stage interp_s;
            vector<Expr> _coords1, _coords2;
            Func _input;
            std::tie(interp_s, _coords1, _coords2, _input) = interp2Tap_stage(s, h, input_id);
            return interp_s;
        } else if (stage_type == 17) {
            if (s.size() < 2) {
                return random_stage(s, h);
            }
            return select_interp2Tap_stage(s, h, input_id);
        } else if (stage_type == 18) {
            if (s.size() < 3) { 
                return random_stage(s, h);
            }
            Stage interp_s;
            vector<Expr> _coords1, _coords2;
            Func _input;
            std::tie(interp_s, _coords1, _coords2, _input) = correct_interp2Tap_stage(s, h, input_id);
            return interp_s;
        } else if (stage_type == 19) {
            if (s.size() < 3) {
                return random_stage(s, h);
            }
            return select_correct_interp2Tap_stage(s, h, input_id);
        } else if (i1 != i2) {
            return binary_op(f, g);
        } else {
            // Try again
            return random_stage(s, h);
        }
    }

    // build pipeline and define all required inputs and outputs for the generated program
    void configure() {
        // create input and output buffers
        for (int i = 0; i < num_input_buffers; i++) {
            Input<Buffer<inputT>>* input_buff = 
                Halide::Internal::GeneratorBase::add_input<Buffer<outputT>>("input_" + std::to_string(i), 3);
            input_buffs.push_back(input_buff);
        }
        for (int i = 0; i < num_output_buffers; i++) {
            Output<Buffer<outputT>>* output_buff = 
                Halide::Internal::GeneratorBase::add_output<Buffer<outputT>>("output_" + std::to_string(i), 3);
            output_buffs.push_back(output_buff);
            Input<Buffer<outputT>>* correct_output_buff = 
                Halide::Internal::GeneratorBase::add_input<Buffer<outputT>>("correct_output_" + std::to_string(i), 3);
            correct_outputs.push_back(correct_output_buff);
        }

        rng.seed((int)seed);

        Var x("x"), y("y"), c("c");

        // create dummy image params for each input buffer so that we can access them in configure()
        // Zero pad all inputs and add them as stages to be used by the generated random stages
        // Assuming all inputs are same size for now
        for (int i = 0; i < num_input_buffers; i++) { 
            input_buff_dummies.emplace_back(Halide::ImageParam(inputHT, 3, "input_" + std::to_string(i)));
            std::vector<std::pair<Expr, Expr>> bounds(3); 
            bounds.at(0).first = 0;
            bounds.at(0).second = input_w;
            bounds.at(1).first = 0;
            bounds.at(1).second = input_h;
            bounds.at(2).first = 0;
            bounds.at(2).second = input_c;
            Func padded_input = Halide::BoundaryConditions::constant_exterior(input_buff_dummies[i], cast(inputHT, 0), bounds);
            std::string func_name;
            switch (i) {
                case 0:
                    func_name = "shifted_GR";
                    break;
                case 1:
                    func_name = "shifted_R";
                    break;
                case 2:
                    func_name = "shifted_B";
                    break;
                case 3:
                    func_name = "shifted_GB";
                    break;
            }

            Func shifted_input(func_name);
            // shift the input so that we don't have to worry about boundary conditions
            Expr value = padded_input(x + (int)shift, y + (int)shift, c);
            shifted_input(x, y, c) = value;

            std::cout << shifted_input(x, y, c) << " = " << value << std::endl;

            stages.emplace_back(Stage{shifted_input, output_w, output_h, output_c});  
        } 

        std::cout << "max stages: " << (int)max_stages << "\n" << std::endl;
        // NOTE: We cannot stop generating stages until we've created at least enough stages to fill the outputs 
        // for now just randomly assigning generated funcs to outputs but in the future we will need to make 
        // sure that the funcs satisfy the size/type/other constraints on the output buffers. 
        // CONSIDER growing pipeline from output and input buffers.
        assert((int)max_stages >= (int)num_output_buffers);

        // keep generating pipelines until we don't get a duplicate
        while (true) {
            uint64_t h = 0;
            for (int i = 0; i < max_stages; i++) {
                Stage next;
                if (i > 0) {
                    next = random_stage(stages, h, stages.size()-1); // use most recently created func as input
                } else {
                    next = random_stage(stages, h);
                }
                stages.push_back(next);
                std::cout << "Approx size: " << stages.back().w << ", " << stages.back().h << ", " << stages.back().c << "\n\n";
            }

            std::cout << "finished adding stages" << std::endl;
            if (!(*hashes)[h]++) {
                break;
            } // else keep generating pipelines
            std::cout << "hash: " << h << " duplicate" << std::endl;
            stages.erase(stages.begin()+num_input_buffers, stages.end());
        }
    }

    // Select which funcs to map to the output buffers 
    // Compute the loss and call backprop if we are in training mode
    void generate() {
        Var x("x"), y("y"), c("c");

        std::vector<Func> last_funcs; // need these for backrop if training
        last_funcs.push_back(stages[stages.size()-1].func);

        (*output_buffs[0])(x, y, c) = stages[stages.size()-1].func(x, y, c);

        /**
        // select output stages
        std::random_device rd;
        std::mt19937 g(rd());
        // can't select output stage from input buffers
        std::shuffle(stages.begin() + num_input_buffers, stages.end(), g);
        // resample and cast selected output funcs and fill output buffers
        for (int i = 0; i < num_output_buffers; i++) {
            Stage out = stages[num_input_buffers + i];
            out = resample_to(out, output_w, output_h, output_c);
            (*output_buffs[i])(x, y, c) = out.func(x, y, c);
            last_funcs.push_back(out.func);
        }**/
      
        Derivative d_loss_d;
        Func err;

        // need to compute total loss over all outputs
        RDom r(0, output_w, 
               0, output_h,
               0, output_c);
        Expr loss = Expr(0.0f);
        for (int i = 0; i < num_output_buffers; i++) {
            Expr diff = cast<double>((*correct_outputs[i])(x, y, c) - last_funcs[i](x, y, c));
            err(x, y, c) = (diff*diff);
            loss += sum(err(r.x, r.y, r.z)/((int)output_w * (int)output_h));
        }

        loss_output() = cast<lossT>(loss);

        // dump the schema information
        std::ofstream dag_file;
        dag_file.open(DAG_csv, std::ofstream::out | std::ofstream::app);

        for ( auto& elem: dag_schema) {
            dag_file << elem.dump() << "\n";
        } 
        dag_file.close(); 

        std::ofstream func_def_file;
        func_def_file.open(FuncDef_csv, std::ofstream::out | std::ofstream::app);

        for ( auto& elem: func_def_schema) {
            func_def_file << elem.dump() << "\n";
        } 
        func_def_file.close(); 

        // Compute derivatives of the loss, and backprop them to the parameters.
        if (training) {
            d_loss_d = propagate_adjoints(loss_output);
            
            // iterate over the generated params and backprop
            for (auto &output_w : output_params) {
                auto& input_w = input_param_dummies[output_w.first];
                backprop(input_w, output_w.second, d_loss_d, learning_rate, timestep);
            }
        }
        // set param_shapes for input and output weights
        if (training) {
            for (auto &output_w : output_params) {
                auto &shape = param_shapes[output_w.first];
                auto input_w = input_params[output_w.first];
                set_input_weight_shape(input_w, std::get<0>(shape), std::get<1>(shape), std::get<2>(shape), std::get<3>(shape));
                set_output_weight_shape(output_w.second, std::get<0>(shape), std::get<1>(shape), std::get<2>(shape), std::get<3>(shape));
            }      
        } else {
            for (auto &input_w : input_params) {
                auto &shape = param_shapes[input_w.first];
                set_input_weight_shape(input_w.second, std::get<0>(shape), std::get<1>(shape), std::get<2>(shape), std::get<3>(shape));
            }      
        }
        learning_rate.set_estimate(0.001f);
        timestep.set_estimate(37);
        batch_size.set_estimate(1);

        // SCHEDULING
        if (!auto_schedule and !training) {
            do_random_pipeline_schedule(get_pipeline());
        } 
        if (!auto_schedule and training) {
            do_random_pipeline_schedule(get_pipeline());
        }

        // bound all inputs and outputs
        for (int i = 0; i < num_input_buffers; i++) {
            (*input_buffs[i]).dim(0).set_bounds_estimate(0, input_w)
                .dim(1).set_bounds_estimate(0, input_h)
                .dim(2).set_bounds_estimate(0, input_c);
        }
        for (int i = 0; i < num_output_buffers; i++) {
            (*correct_outputs[i]).dim(0).set_bounds_estimate(0, output_w)
                .dim(1).set_bounds_estimate(0, output_h)
                .dim(2).set_bounds_estimate(0, output_c);

            (*output_buffs[i]).dim(0).set_bounds_estimate(0, output_w)
                .dim(1).set_bounds_estimate(0, output_h)
                .dim(2).set_bounds_estimate(0, output_c);
        }
    }

    void set_inputs(const std::vector<Buffer<inputT>> &inputs) {
        for (size_t i = 0; i < inputs.size(); i++) input_buff_dummies[i].set(inputs[i]);
    }

private:
    std::vector<Halide::ImageParam> input_buff_dummies;
    std::vector<Input<Buffer<inputT>> *>   input_buffs;
    std::vector<Input<Buffer<outputT>> *>  correct_outputs;
    std::vector<Output<Buffer<outputT>> *> output_buffs;

    std::unordered_map<string, Halide::ImageParam> input_param_dummies;
    std::unordered_map<string, Input<Halide::Buffer<paramT>> *> input_params;
    std::unordered_map<string, Output<Halide::Buffer<paramT>> *> output_params;
    // param_shapes of parameter buffers
    std::unordered_map<string, std::tuple<dim_shape, dim_shape, dim_shape, dim_shape>> param_shapes;

    Output<Buffer<lossT>> loss_output { "loss_output", 0 };
};

using RandomPipelineInference = RandomPipeline<false>;
using RandomPipelineTraining = RandomPipeline<true>;


HALIDE_REGISTER_GENERATOR(RandomPipelineInference, random_pipeline_inference)
HALIDE_REGISTER_GENERATOR(RandomPipelineTraining, random_pipeline_training)
