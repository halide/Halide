// We directly include the headers from the Halide source tree to
// avoid a build dependency on Halide.h
#include "BoundaryConditions.h"
#include "Derivative.h"
#include "InlineReductions.h"
#include "Generator.h"

// Define the pipeline that we'll be producing as a nullptr, because
// we're going to be linking to most libHalide with that pipeline
// missing
extern "C" {
void *halide_autoscheduler_ranking_model = nullptr;
void *halide_autoscheduler_train_ranking_model = nullptr;
}

using namespace Halide;

// A model weight is either just an input, or an input and an output
// (the updated weights and the ADAM state) depending on whether we're
// doing inference or training.
template<bool training> struct ModelWeight;

template<>
struct ModelWeight<false> : public GeneratorInput<Buffer<float>> {
    ModelWeight(const std::string &name, int dim) : GeneratorInput<Buffer<float>>(name, dim) {}
    void backprop(const Derivative &d, Expr learning_rate, Expr timestep) {}
    void set_shape(int s0, int s1 = 0, int s2 = 0) {
        dim(0).set_stride(Expr()).dim(dimensions() - 1).set_stride(1);
        dim(0).set_bounds(0, s0);
        if (s1) dim(1).set_bounds(0, s1);
        if (s2) dim(2).set_bounds(0, s2);
    }
};

template<>
struct ModelWeight<true> : public GeneratorInput<Buffer<float>> {
    GeneratorOutput<Buffer<float>> grad;

    ModelWeight(const std::string &name, int dim) : GeneratorInput<Buffer<float>>(name, dim), grad("updated_" + name, dim + 1) {}
    void backprop(const Derivative &d, Expr learning_rate, Expr timestep) {
        std::vector<Expr> args(dimensions() + 1);
        for (auto &e : args) e = Var();
        grad(args) = undef<float>();

        // We'll report back the new weights and the loss gradients,
        // and update the ADAM state. Depending on the mode the caller
        // is in, it may use the new weights, or it may just send the
        // loss gradients up to an ADAM server.
        args.back() = 0;
        FuncRef new_weight = grad(args);
        args.back() = 1;
        FuncRef smoothed_deriv = grad(args);
        args.back() = 2;
        FuncRef smoothed_second_moment = grad(args);
        args.back() = 3;
        FuncRef loss_gradient = grad(args);

        args.pop_back();
        Expr current_weight = (*this)(args);

        loss_gradient = d(*this)(args);

        // Update the first and second moment estimates
        smoothed_deriv = 0.9f * smoothed_deriv + 0.1f * loss_gradient;
        smoothed_second_moment = 0.999f * smoothed_second_moment + 0.001f * pow(loss_gradient, 2);

        // Correction to account for the fact that the smoothed_deriv
        // and smoothed_second_moment start at zero when t == 0
        Expr smoothed_deriv_correction = 1 / (1 - pow(0.9f, timestep + 1));
        Expr smoothed_second_moment_correction = 1 / (1 - pow(0.999f, timestep + 1));

        // Update the weights
        Expr step = learning_rate * smoothed_deriv * smoothed_deriv_correction;
        step /= sqrt(smoothed_second_moment * smoothed_second_moment_correction) + 1e-8f;

        new_weight = current_weight - step;
    }
    void set_shape(int s0, int s1 = 0, int s2 = 0) {
        dim(0).set_stride(Expr()).dim(dimensions() - 1).set_stride(1);
        // grad.dim(0).set_stride(Expr()).dim(dimensions() - 1).set_stride(1);
        dim(0).set_bounds(0, s0);
        grad.dim(0).set_bounds(0, s0);
        grad.bound(grad.args()[0], 0, s0);
        if (s1) {
            dim(1).set_bounds(0, s1);
            grad.dim(1).set_bounds(0, s1);
            grad.bound(grad.args()[1], 0, s1);
        }
        if (s2) {
            dim(2).set_bounds(0, s2);
            grad.dim(2).set_bounds(0, s2);
            grad.bound(grad.args()[2], 0, s2);
        }
        grad.dim(dimensions()).set_bounds(0, 4);
    }
};

template<bool training>
class RankingModel : public Generator<RankingModel<training>> {
public:
    // Same issue as CodeGen_GPU_Host.h: because we inherit from a
    // dependent template type we don't pull in the parent class's
    // names automatically.
    template<typename T> using Input = GeneratorInput<T>;
    template<typename T> using Output = GeneratorOutput<T>;

    // Inputs
    Input<int> batch_size{ "batch_size", 1 };
    Input<Buffer<float>> embeddingA{ "embeddingA", 2};
    Input<Buffer<float>> embeddingB{ "embeddingB", 2};

    // Network weights. These are parameters instead of baked-in
    // buffers so that they can be swapped out using an environment
    // variable at runtime. In training mode they are also outputs.
    using Weight = ModelWeight<training>;
    Weight fc1_weight{ "fc1", 2 };
    Weight fc1_bias{ "fc1_bias", 1};
    Weight fc2_weight{ "fc2", 2};
    Weight fc2_bias{ "fc2_bias", 1};
    Weight fc3_weight{ "fc3", 2};
    Weight fc3_bias{ "fc3_bias", 1};

    // Some extra inputs for training mode. Really should be conditional on 'training'.
    Input<float> learning_rate{ "learning_rate", 1.0f };
    Input<int> timestep{ "timestep", 0 }; // Needed by ADAM
    Input<Buffer<float>> true_runtime{ "true_runtime", 1 };

    // Either outputs a prediction per batch element or a loss
    // aggregated across the batch, depending on training or inference
    Output<Buffer<float>> output{ "output", training ? 0 : 1 };

    void generate() {
        Var c("c"), w("w"), n("n"), i("i"), j("j"), s("s");
        const int embedding_dim = 144;
        const int fc1_channels = 72;
        const int fc2_channels = 48;
        const int fc3_channels = 2;

        Func fc1_stage1("fc1_stage1");
        RDom r_fc1(0, embedding_dim);
        fc1_stage1(n, c) = fc1_bias(c);
        fc1_stage1(n, c) += fc1_weight(c, r_fc1) * embeddingA(n, r_fc1);

        Func fc1_stage2("fc1_stage2");
        fc1_stage2(n, c) = fc1_stage1(n, c);
        fc1_stage2(n, c) += fc1_weight(c, embedding_dim + r_fc1) * embeddingB(n, r_fc1); 

        Func fc2("fc2");
        RDom r_fc2(0, fc1_channels);
        fc2(n, c) = fc2_bias(c);
        fc2(n, c) += fc2_weight(c, r_fc2) * fc1_stage2(n, r_fc2);

        Func fc3("fc3");
        RDom r_fc3(0, fc2_channels);
        fc3(n, c) = fc3_bias(c);
        fc3(n, c) += fc3_weight(c, r_fc3) * fc2(n, r_fc3);

        Func prediction;
        prediction(n) = select(fc3(n,0) > fc3(n,1), 0, 1); // whichever logit is larger is the winner

        if (!training) {
            output(n) = prediction(n);

            // schedule
            output.bound(n, 0, batch_size);

            const int vec = 8;

            // Pipeline features processing
            normalized_pipeline_features.compute_root()
                .vectorize(i, vec).update().vectorize(i, vec);
            head1_relu.compute_root().vectorize(c, vec);
            conv1_stage1.compute_root().vectorize(c, vec);

            // Schedule features processing
            for (Func f : {normalized_schedule_features, head2_relu, relu1, relu2, relu3, pool3, relu4, pool4, relu5, relu6}) {
                f.compute_at(output, n).specialize(batch_size >= vec).vectorize(n, vec);
            }

            output.compute_root()
                .specialize(batch_size >= vec)
                .vectorize(n, vec)
                .specialize(batch_size >= 2*vec)
                .parallel(n, 2);
        }

        // All the model weight shapes are statically known. Helps to
        // simplify generated code.
        fc1_weight.set_shape(fc1_channels, embedding_dim*2);
        fc1_bias.set_shape(fc1_channels);
        fc2_weight.set_shape(fc2_channels, fc1_channels);
        fc2_bias.set_shape(fc2_channels);
        fc3_weight.set_shape(fc3_channels, fc2_channels);
    }
};

using RankingModelInference = RankingModel<false>;
using RankingModelTraining = RankingModel<true>;

HALIDE_REGISTER_GENERATOR(RankingModelInference, halide_autoscheduler_ranking_model);
HALIDE_REGISTER_GENERATOR(RankingModelTraining, halide_autoscheduler_train_ranking_model);
