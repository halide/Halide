#include "Halide.h"
#include <iostream>
#include <random>

using namespace Halide;
using std::vector;

namespace {

// Convert a vector of Vars to Exprs. Useful for generating references
// to Funcs.
vector<Expr> make_arguments(vector<Var> vars) {
    vector<Expr> result;
    for (Var i : vars) {
        result.push_back(i);
    }
    return result;
}

// Generator to produce a random pipeline. The generated pipeline will
// be solely a function of the seed and the number of stages.
class RandomPipeline : public Halide::Generator<RandomPipeline> {
public:
    // The random seed to use to generate the pipeline.
    GeneratorParam<int> seed{"seed", 1};
    // The number of stages to generate in the random pipeline.
    GeneratorParam<int> stages{"stages", 20};

    Input<Buffer<float>>  input{"input", 3};
    Output<Buffer<float>> output{"output", 3};

    std::mt19937 rng;

    // Helpers to generate random values.
    int rand_int(int min, int max) { return (rng() % (max - min + 1)) + min; }
    bool rand_bool() { return rng() % 2 == 0; }
    float rand_float() { return rand_int(0, 1 << 30) / (float)(1 << 30); }

    Expr rand_value(Type t) {
        if (t.is_int()) {
            return cast(t, rand_int(-128, 127));
        } else if (t.is_float()) {
            return cast(t, rand_float());
        } else {
            // Shouldn't get here.
            assert(false);
            return undef(t);
        }
    }


    // Generate a random convolution of one dimension of f.
    Func convolve(Func f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << std::endl;

        vector<Var> args = f.args();

        Expr def = cast(f.value().type(), 0);
        for (int i = kernel_min; i <= kernel_max; i++) {
            vector<Expr> coords = make_arguments(f.args());
            coords[dim] += i;
            def = def + rand_value(f.value().type()) * f(coords);
        }

        Func conv("conv");
        conv(args) = def;

        return conv;
    }

    // Generate an upsampling or downsampling of dimension dim by factor.
    Func upsample(Func f, int dim, int factor) {
        std::cout << "Upsampling dimension " << dim << " by " << factor << "x" << std::endl;

        vector<Expr> resampled_coords = make_arguments(f.args());
        resampled_coords[dim] = resampled_coords[dim] / factor;

        Func resampled("resampled");
        resampled(f.args()) = f(resampled_coords);

        return resampled;
    }

    Func downsample(Func f, int dim, int factor) {
        std::cout << "Downsampling dimension " << dim << " by " << factor << "x" << std::endl;

        vector<Expr> resampled_coords = make_arguments(f.args());
        resampled_coords[dim] = resampled_coords[dim] * factor;

        Func resampled("resampled");
        resampled(f.args()) = f(resampled_coords);

        return resampled;
    }

    // Generate an all-to-all communication in dimension dim.
    Func all_to_all(Func f, int dim) {
        std::cout << "All to all on dimension " << dim << std::endl;

        // TODO: This just assumes that the extent of the dimension is
        // 3, which is really bad.
        RDom r(0, 3);
        vector<Expr> reduction_coords = make_arguments(f.args());
        reduction_coords[dim] = r;

        Func all("all");
        all(f.args()) = sum(f(reduction_coords) * (r + 1) * (f.args()[dim] + 1));

        return all;
    }

    // Generate a random stage using f as an input.
    Func random_stage(Func f) {
        int stage_type = rand_int(0, 6);
        if (stage_type < 4) {
            int dim = rand_int(0, 1);
            int kernel_min = rand_int(-5, 0);
            int kernel_max = rand_int(0, 5);
            return convolve(f, dim, kernel_min, kernel_max);
        } else if (stage_type == 4) {
            // For now, only upsample dimensions 0 or 1.
            int dim = rand_int(0, 1);
            int factor = rand_int(1, 4);
            return upsample(f, dim, factor);
        } else if (stage_type == 5) {
            // For now, only downsample dimensions 0 or 1.
            int dim = rand_int(0, 1);
            int factor = rand_int(1, 4);
            return downsample(f, dim, factor);
        } else if (stage_type == 6) {
            // TODO: For now, only generate all-to-all communication in dimension 2, which is likely to be small.
            int dim = 2;
            return all_to_all(f, dim);
        } else {
            assert(false);
            return Func();
        }
    }

    void generate() {
        rng.seed(seed);

        Func tail = input;
        for (int i = 0; i < stages; i++) {
            Func next = random_stage(tail);
            if (!auto_schedule) {
                next.compute_root();
            }
            tail = next;
        }
        output(tail.args()) = tail(tail.args());

        if (auto_schedule) {
            // This estimate of the input bounds is unlikely to be accurate.
            input.dim(0).set_bounds_estimate(0, 1024);
            input.dim(1).set_bounds_estimate(0, 1024);
            input.dim(2).set_bounds_estimate(0, 3);
            output.estimate(output.args()[0], 0, 1024);
            output.estimate(output.args()[1], 0, 1024);
            output.estimate(output.args()[2], 0, 3);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(RandomPipeline, random_pipeline)
