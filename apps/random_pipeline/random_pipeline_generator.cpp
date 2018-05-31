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
    // The approximate max number of stages to generate in the random pipeline.
    GeneratorParam<int> max_stages{"max_stages", 20};

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

    struct Stage {
        Func func;
        int w, h; // approx width and height;
    };

    // Generate a random convolution of one dimension of f.
    Stage convolve(Stage f, int dim, int kernel_min, int kernel_max) {
        std::cout << "Convolving dimension " << dim
                  << " with kernel [" << kernel_min << ", " << kernel_max << "]"
                  << std::endl;

        vector<Var> args = f.func.args();

        Expr def = cast(f.func.value().type(), 0);
        for (int i = kernel_min; i <= kernel_max; i++) {
            vector<Expr> coords = make_arguments(f.func.args());
            coords[dim] += i;
            def = def + rand_value(f.func.value().type()) * f.func(coords);
        }

        Func conv("conv");
        conv(args) = def;

        return {conv, f.w, f.h};
    }

    // Generate an upsampling or downsampling of dimension dim by factor.
    Stage upsample(Stage f, int dim, int factor) {
        std::cout << "Upsampling dimension " << dim << " by " << factor << "x" << std::endl;

        vector<Expr> resampled_coords = make_arguments(f.func.args());
        resampled_coords[dim] = resampled_coords[dim] / factor;

        Func resampled("upsampled");
        resampled(f.func.args()) = f.func(resampled_coords);

        Stage s {resampled, f.w, f.h};
        if (dim == 0) {
            s.w *= factor;
        } else if (dim == 1) {
            s.h *= factor;
        } else {
            assert(false);
        }
        return s;
    }

    Stage downsample(Stage f, int dim, int factor) {
        std::cout << "Downsampling dimension " << dim << " by " << factor << "x" << std::endl;

        vector<Expr> resampled_coords = make_arguments(f.func.args());
        resampled_coords[dim] = resampled_coords[dim] * factor;

        Func resampled("downsampled");
        resampled(f.func.args()) = f.func(resampled_coords);

        Stage s {resampled, f.w, f.h};
        if (dim == 0) {
            s.w = (s.w + factor - 1)/factor;
        } else if (dim == 1) {
            s.h = (s.h + factor - 1)/factor;
        } else {
            assert(false);
        }
        return s;
    }

    Stage add(Stage f, Stage g) {
        Func added("added");
        added(f.func.args()) = f.func(f.func.args()) + g.func(f.func.args());
        return {added, f.w, f.h};
    }

    // Generate an all-to-all communication in dimension dim.
    Stage all_to_all(Stage f, int dim) {
        std::cout << "All to all on dimension " << dim << std::endl;

        // TODO: This just assumes that the extent of the dimension is
        // 3, which is really bad.
        vector<Expr> reduction_coords = make_arguments(f.func.args());
        Expr e = 0.f;
        for (int i = 0; i < 3; i++) {
            reduction_coords[dim] = i;
            e += f.func(reduction_coords) * (i + 1) * (f.func.args()[dim] + 1);
        }

        Func all("all");
        all(f.func.args()) = e;

        return {all, f.w, f.h};
    }

    // Generate a random stage using f as an input.
    Stage random_stage(const vector<Stage> &s) {
        int m = (int)s.size() - 1;
        int i2 = m > 0 ? rand_int(0, m - 1) : 0;
        int i1 = m > 0 ? rand_int(i2 + 1, m) : 0;
        Stage f = s[i1], g = s[i2];
        int stage_type = rand_int(0, 7);
        if (stage_type < 3) {
            int dim = rand_int(0, 1);
            int kernel_min = rand_int(-5, 0);
            int kernel_max = rand_int(0, 5);
            return convolve(f, dim, kernel_min, kernel_max);
        } else if (stage_type == 3) {
            // For now, only upsample dimensions 0 or 1.
            int dim = rand_int(0, 1);
            int factor = 2;
            if (f.w < 1024 && f.h < 1024) {
                return upsample(f, dim, factor);
            } else {
                return random_stage(s);
            }
        } else if (stage_type == 4) {
            // For now, only downsample dimensions 0 or 1.
            int dim = rand_int(0, 1);
            int factor = 2;
            if (f.w > 128 && f.h > 128) {
                return downsample(f, dim, factor);
            } else {
                return random_stage(s);
            }
        } else if (stage_type == 5) {
            // TODO: For now, only generate all-to-all communication in dimension 2, which is likely to be small.
            int dim = 2;
            return all_to_all(f, dim);
        } else if (i1 != i2) {
            // Add two equal resolution stages
            if (f.w == g.w && f.h == g.h) {
                return add(f, g);
            } else {
                return random_stage(s);
            }
        } else {
            return random_stage(s);
        }
    }

    void generate() {
        rng.seed((int)seed);

        vector<Stage> stages;
        // Assume input starts at ~1024x1024
        stages.emplace_back(Stage{input, 1024, 1024});

        for (int i = 0; i < max_stages - 2; i++) {
            std::cout << "Approx size: " << stages.back().w << ", " << stages.back().h << "\n";
            Stage next = random_stage(stages);
            if (!auto_schedule) {
                next.func.compute_root();
            }
            stages.push_back(next);
            if (!auto_schedule) {
                stages.back().func.compute_root().reorder(_0, _2, _1).vectorize(_0, 8).parallel(_1);
            }
        }

        Stage tail = stages.back();

        // Resample back to the correct resolution
        if (tail.w >= 2048) {
            tail = downsample(tail, 0, tail.w / 1024);
        } else if (tail.w < 512) {
            tail = upsample(tail, 0, 1024 / tail.w);
        }

        if (tail.h >= 2048) {
            tail = downsample(tail, 1, tail.h / 1024);
        } else if (tail.h < 512) {
            tail = upsample(tail, 1, 1024 / tail.h);
        }

        output(tail.func.args()) = tail.func(tail.func.args());

        if (!auto_schedule) {
            output.compute_root().reorder(_0, _2, _1).vectorize(_0, 8).parallel(_1);
        }

        if (auto_schedule) {
            output.estimate(output.args()[0], 0, 1024);
            output.estimate(output.args()[1], 0, 1024);
            output.estimate(output.args()[2], 0, 3);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(RandomPipeline, random_pipeline)
