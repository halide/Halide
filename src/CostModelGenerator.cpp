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
void *halide_autoscheduler_cost_model = nullptr;
void *halide_autoscheduler_train_cost_model = nullptr;
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
class CostModel : public Generator<CostModel<training>> {
public:
    // Same issue as CodeGen_GPU_Host.h: because we inherit from a
    // dependent template type we don't pull in the parent class's
    // names automatically.
    template<typename T> using Input = GeneratorInput<T>;
    template<typename T> using Output = GeneratorOutput<T>;

    // Inputs
    Input<int> num_stages{ "num_stages", 1 };
    Input<int> batch_size{ "batch_size", 1 };
    Input<Buffer<float>> pipeline_features{ "pipeline_features", 3 };
    Input<Buffer<float>> schedule_features{ "schedule_features", 3 };

    // Feature statistics for whitening
    Input<Buffer<float>> pipeline_mean{ "pipeline_mean", 2 };
    Input<Buffer<float>> pipeline_std{ "pipeline_std", 2 };
    Input<Buffer<float>> schedule_mean{ "schedule_mean", 1 };
    Input<Buffer<float>> schedule_std{ "schedule_std", 1 };

    // Network weights. These are parameters instead of baked-in
    // buffers so that they can be swapped out using an environment
    // variable at runtime. In training mode they are also outputs.
    using Weight = ModelWeight<training>;
    Weight head1_filter{ "head1_filter", 3 };
    Weight head1_bias{ "head1_bias", 1 };
    Weight head2_filter{ "head2_filter", 2 };
    Weight head2_bias{ "head2_bias", 1 };
    Weight filter1{ "filter1", 3 };
    Weight bias1{ "bias1", 1 };
    Weight filter2{ "filter2", 3 };
    Weight bias2{ "bias2", 1 };
    Weight filter3{ "filter3", 3 };
    Weight bias3{ "bias3", 1 };
    Weight filter4{ "filter4", 3 };
    Weight bias4{ "bias4", 1 };
    Weight filter5{ "filter5", 3 };
    Weight bias5{ "bias5", 1 };
    Weight filter6{ "filter6", 1 };
    Weight bias6{ "bias6", 0 };

    // Some extra inputs for training mode. Really should be conditional on 'training'.
    Input<float> learning_rate{ "learning_rate", 1.0f };
    Input<int> timestep{ "timestep", 0 }; // Needed by ADAM
    Input<Buffer<float>> true_runtime{ "true_runtime", 1 };

    // Either outputs a prediction per batch element or a loss
    // aggregated across the batch, depending on training or inference
    Output<Buffer<float>> output{ "output", training ? 0 : 1 };

    // Zero pad alone the last dimension of a Func
    Func pad_stages(Func f, Expr stages) {
        std::vector<std::pair<Expr, Expr>> bounds(f.dimensions());
        bounds.back().first = 0;
        bounds.back().second = stages;
        return BoundaryConditions::constant_exterior(f, 0.0f, bounds);
    }

    void generate() {
        Var c("c"), w("w"), n("n"), i("i"), j("j"), s("s");

        // The memory layout of the weights and stats is matrix-style
        for (auto *b : {&pipeline_mean, &pipeline_std} ) {
            b->dim(0).set_stride(Expr()).dim(b->dimensions() - 1).set_stride(1);
        }

        Expr padded_stages = max(num_stages, 22);
        Expr first_valid = max(0, (padded_stages - num_stages) / 2);

        Func normalized_pipeline_features("normalized_pipeline_features");
        normalized_pipeline_features(i, j, s) = 0.0f;
        RDom r_s(first_valid, num_stages);
        normalized_pipeline_features(i, j, r_s) =
            (pipeline_features(i, j, r_s - first_valid) - pipeline_mean(i, j)) / pipeline_std(i, j);

        Func normalized_schedule_features("normalized_schedule_features");
        normalized_schedule_features(n, i, s) = 0.0f;
        normalized_schedule_features(n, i, r_s) =
            (fast_log(schedule_features(n, i, r_s - first_valid) + 1) - schedule_mean(i)) / schedule_std(i);

        const int head1_channels = 20, head1_w = 56, head1_h = 7;
        const int head2_channels = 20, head2_w = 26;
        const int conv1_channels = 40;
        const int conv2_channels = 40;
        const int conv3_channels = 80;
        const int conv4_channels = 120;
        const int conv5_channels = 160;
        const int conv_support = 3;

        Func head1_conv("head1_conv");
        RDom r_head1(0, head1_w, 0, head1_h);
        head1_conv(c, w) = head1_bias(c);
        head1_conv(c, w) += head1_filter(c, r_head1.x, r_head1.y) * normalized_pipeline_features(r_head1.x, r_head1.y, w);

        Func head1_relu("head1_relu");
        head1_relu(c, w) = max(0, head1_conv(c, w));

        Func head1_relu_padded = pad_stages(head1_relu, padded_stages);

        Func head2_conv("head2_conv");
        RDom r_head2(0, head2_w);
        head2_conv(n, c, w) = head2_bias(c);
        head2_conv(n, c, w) += head2_filter(c, r_head2) * normalized_schedule_features(n, r_head2, w);

        Func head2_relu("head2_relu");
        head2_relu(n, c, w) = max(0, head2_conv(n, c, w));

        Func head2_relu_padded = pad_stages(head2_relu, padded_stages);

        /***** network trunk *****/
        // first 20 input channels are from head1_relu, next 20 input channels are from head2_relu
        // have to do two stagees for conv1 to convolve over each head's outputs
        Func conv1_stage1("conv1_stage1");
        RDom r1_stage1(0, head1_channels, 0, conv_support);
        conv1_stage1(c, w) = bias1(c);
        conv1_stage1(c, w) += filter1(c, r1_stage1.x, r1_stage1.y) * head1_relu_padded(r1_stage1.x, w + r1_stage1.y - 1);

        Func conv1_stage2("conv1_stage2");
        RDom r1_stage2(0, head2_channels, 0, conv_support);
        conv1_stage2(n, c, w) = conv1_stage1(c, w);  // Broadcast the processed pipeline features across the batch
        conv1_stage2(n, c, w) += (filter1(c, head1_filter.dim(0).extent() + r1_stage2.x, r1_stage2.y) *
                                  head2_relu_padded(n, r1_stage2.x, w + r1_stage2.y - 1));

        Func relu1("relu1");
        relu1(n, c, w) = max(0, conv1_stage2(n, c, w));

        Func relu1_padded = pad_stages(relu1, padded_stages);

        Func conv2("conv2");
        RDom r2(0, head1_channels + head2_channels, 0, conv_support);
        conv2(n, c, w) = bias2(c);
        conv2(n, c, w) += filter2(c, r2.x, r2.y) * relu1_padded(n, r2.x, w + r2.y - 1);

        Func relu2("relu2");
        relu2(n, c, w) = max(0, conv2(n, c, w));

        // set boundary conditions for relu2
        Func relu2_padded = pad_stages(relu2, padded_stages);

        Func conv3("conv3");
        RDom r3(0, conv2_channels, 0, conv_support);
        conv3(n, c, w) = bias3(c);
        conv3(n, c, w) += filter3(c, r3.x, r3.y) * relu2_padded(n, r3.x, w + r3.y - 1);

        Func relu3("relu3");
        relu3(n, c, w) = max(0, conv3(n, c, w));

        // set boundary conditions for relu3
        Func relu3_padded = pad_stages(relu3, padded_stages);

        Func pool3("pool3");
        pool3(n, c, w) = 0.5f * (relu3_padded(n, c, w * 2 - 1) + relu3_padded(n, c, w * 2));

        // set boundary conditions for pool3
        Func pool3_padded = pad_stages(pool3, padded_stages / 2 + 1);

        Func conv4("conv4");
        RDom r4(0, conv3_channels, 0, conv_support);
        conv4(n, c, w) = bias4(c);
        conv4(n, c, w) += filter4(c, r4.x, r4.y) * pool3_padded(n, r4.x, w + r4.y - 1);

        Func relu4("relu4");
        relu4(n, c, w) = max(0, conv4(n, c, w));

        // set boundary conditions for relu4
        Func relu4_padded = pad_stages(relu4, padded_stages / 2 + 1);

        Func pool4("pool4");
        pool4(n, c, w) = 0.5f * (relu4_padded(n, c, w * 2 - 1) + relu4_padded(n, c, w * 2));

        // set boundary conditions for pool4
        Func pool4_padded = pad_stages(pool4, (padded_stages + 6) / 4);

        Func conv5("conv5");
        RDom r5(0, conv4_channels, 0, conv_support);
        conv5(n, c, w) = bias5(c);
        conv5(n, c, w) += filter5(c, r5.x, r5.y) * pool4_padded(n, r5.x, w + r5.y - 1);

        Func relu5("relu5");
        relu5(n, c, w) = max(0, conv5(n, c, w));

        // set boundary conditions for relu5
        Func relu5_padded = pad_stages(relu5, (padded_stages + 6) / 4);

        Func conv6("conv6");
        RDom r6(0, conv5_channels);
        conv6(n, w) = bias6();
        conv6(n, w) += filter6(r6) * relu5_padded(n, r6, w);

        Func relu6("relu6");
        relu6(n, w) = max(0, conv6(n, w));

        // reduce over a region that expands to 3x1 convs from the first two stages to the last two stages with zero padding
        RDom r_reduce(0, (padded_stages + 6) / 4);
        Func prediction;
        prediction(n) = sum(relu6(n, r_reduce));

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

        } else {
            RDom r_batch(0, batch_size);

            /*
            Func average_prediction, average_runtime;

            average_prediction() += prediction(r_batch);
            average_prediction() /= batch_size;
            average_runtime() += true_runtime(r_batch);
            average_runtime() /= batch_size;
            */

            Func err;
            //Expr delta = (prediction(n) / average_prediction()) - (true_runtime(n) / average_runtime());
            Expr delta = 1.0f/(prediction(n) + 0.0001f) - 1.0f/(true_runtime(n) + 0.0001f);
            err(n) = delta * delta;
            Expr loss = sum(err(r_batch));

            output() = loss / batch_size;

            auto d_loss_d = propagate_adjoints(output);

            Weight *weights[] = {&head1_filter, &head1_bias,
                                 &head2_filter, &head2_bias,
                                 &filter1, &bias1,
                                 &filter2, &bias2,
                                 &filter3, &bias3,
                                 &filter4, &bias4,
                                 &filter5, &bias5,
                                 &filter6, &bias6};

            for (Weight *w : weights) {
                w->backprop(d_loss_d, learning_rate, timestep);
            }

            // A simple schedule. We'd like to autoschedule this, but
            // it's not available at this stage of compilation. We'll
            // just compute-root everything and let LLVM autovectorize.
            for (Weight *w : weights) {
                for (auto g : d_loss_d.funcs(Func(*w))) {
                    g.compute_root();
                }
            }

            for (Func f : {normalized_schedule_features, normalized_pipeline_features,
                        head1_conv, head1_relu, head1_relu_padded,
                        head2_conv, head2_relu, head2_relu_padded,
                        conv1_stage1, conv1_stage2, relu1, relu1_padded,
                        conv2, relu2, relu2_padded,
                        conv3, relu3, relu3_padded,
                        pool3, pool3_padded,
                        conv4, relu4, relu4_padded,
                        pool4, pool4_padded,
                        conv5, relu5, relu5_padded,
                        conv6, relu6,
                        prediction,
                        err, Func(output)}) {
                f.compute_root();
                for (auto g : d_loss_d.funcs(f)) {
                    g.compute_root();
                }
            }
        }

        // All the model weight shapes are statically known. Helps to
        // simplify generated code.
        head1_filter.set_shape(head1_channels, head1_w, head1_h);
        head1_bias.set_shape(head1_channels);
        head2_filter.set_shape(head2_channels, head2_w);
        head2_bias.set_shape(head2_channels);
        filter1.set_shape(conv1_channels, head1_channels + head2_channels, conv_support);
        bias1.set_shape(conv1_channels);
        filter2.set_shape(conv2_channels, conv1_channels, conv_support);
        bias2.set_shape(conv2_channels);
        filter3.set_shape(conv3_channels, conv2_channels, conv_support);
        bias3.set_shape(conv3_channels);
        filter4.set_shape(conv4_channels, conv3_channels, conv_support);
        bias4.set_shape(conv4_channels);
        filter5.set_shape(conv5_channels, conv4_channels, conv_support);
        bias5.set_shape(conv5_channels);
        filter6.set_shape(conv5_channels);

    }
};

using CostModelInference = CostModel<false>;
using CostModelTraining = CostModel<true>;

HALIDE_REGISTER_GENERATOR(CostModelInference, halide_autoscheduler_cost_model);
HALIDE_REGISTER_GENERATOR(CostModelTraining, halide_autoscheduler_train_cost_model);
