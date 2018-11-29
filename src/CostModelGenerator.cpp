#ifndef COMPILING_SEPARATELY

// We directly include the headers from the Halide source tree to
// avoid a build dependency on Halide.h
#include "BoundaryConditions.h"
#include "Derivative.h"
#include "InlineReductions.h"
#include "Generator.h"
#include "Simplify.h"

// Define the pipeline that we'll be producing as a nullptr, because
// we're going to be linking to most libHalide with that pipeline
// missing
extern "C" {
void *halide_autoscheduler_cost_model = nullptr;
void *halide_autoscheduler_train_cost_model = nullptr;
}

#else

// We're compiling the generator as a true Halide generator, not as a weird bootstrapping-the-compiler thing
#include "Halide.h"

#endif

using namespace Halide;

// A model weight is either just an input, or an input and an output
// (the updated weights and the ADAM state) depending on whether we're
// doing inference or training.
template<bool training> struct ModelWeight;

template<>
struct ModelWeight<false> : public GeneratorInput<Buffer<float>> {
    ModelWeight(const std::string &name, int dim) : GeneratorInput<Buffer<float>>(name, dim) {}
    void backprop(const Derivative &d, Expr learning_rate, Expr timestep) {}
    void set_shape(int s0 = 0, int s1 = 0, int s2 = 0) {
        if (s0) dim(0).set_bounds(0, s0);
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
        step /= sqrt(smoothed_second_moment * smoothed_second_moment_correction) + 1e-5f;

        new_weight = current_weight - step;
        //new_weight = current_weight - learning_rate * 0.0001f * loss_gradient;
    }
    void set_shape(int s0 = 0, int s1 = 0, int s2 = 0) {
        if (s0) {
            dim(0).set_bounds(0, s0);
            grad.dim(0).set_bounds(0, s0);
            grad.bound(grad.args()[0], 0, s0);
        }
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
    using Generator<CostModel<training>>::auto_schedule;

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

    Output<Buffer<float>> prediction_output{ "prediction_output", 1 };
    Output<Buffer<float>> loss_output { "loss_output", 0 };

    // Zero pad alone the last dimension of a Func
    Func pad_stages(Func f, Expr stages) {
        std::vector<std::pair<Expr, Expr>> bounds(f.dimensions());
        bounds[1].first = 0;
        bounds[1].second = stages;
        return BoundaryConditions::constant_exterior(f, cast(f.value().type(), 0), bounds);
    }

    Expr activation(Expr e) {
        return max(e, 0);
    }

    void generate() {
        Var c("c"), w("w"), n("n"), j("j"), s("s");

        Type working_type = Float(32); //training ? Float(64) : Float(32);

        Func normalized_pipeline_features("normalized_pipeline_features");
        normalized_pipeline_features(c, j, s) =
            cast(working_type, (pipeline_features(c, j, s) - pipeline_mean(c, j)) / max(1, pipeline_std(c, j)));

        Func normalized_schedule_features("normalized_schedule_features");
        normalized_schedule_features(n, c, s) =
            cast(working_type, (fast_log(schedule_features(n, c, s) + 1) - schedule_mean(c)) / max(1, schedule_std(c)));

        const int head1_channels = 24, head1_w = 56, head1_h = 7;
        const int head2_channels = 24, head2_w = 26;
        const int conv1_channels = 24;
        const int conv2_channels = 24;
        const int conv3_channels = 24;
        const int conv4_channels = 24;
        const int conv5_channels = 24;
        const int conv_support = 3;

        Func head1_conv("head1_conv");
        RDom r_head1(0, head1_w, 0, head1_h);
        head1_conv(c, w) = cast(working_type, head1_bias(c));
        head1_conv(c, w) += head1_filter(c, r_head1.x, r_head1.y) * normalized_pipeline_features(r_head1.x, r_head1.y, w);

        Func head1_relu("head1_relu");
        head1_relu(c, w) = activation(head1_conv(c, w));

        Func head1_relu_padded = pad_stages(head1_relu, num_stages);

        Func head2_conv("head2_conv");
        RDom r_head2(0, head2_w);
        head2_conv(c, w, n) = cast(working_type, head2_bias(c));
        head2_conv(c, w, n) += head2_filter(c, r_head2) * normalized_schedule_features(n, r_head2, w);

        Func head2_relu("head2_relu");
        head2_relu(c, w, n) = activation(head2_conv(c, w, n));

        Func head2_relu_padded = pad_stages(head2_relu, num_stages);

        /***** network trunk *****/
        // first 20 input channels are from head1_relu, next 20 input channels are from head2_relu
        // have to do two stages for conv1 to convolve over each head's outputs
        Func conv1_stage1("conv1_stage1");
        RDom r1_stage1(0, head1_channels, 0, conv_support);
        conv1_stage1(c, w) = cast(working_type, bias1(c));
        conv1_stage1(c, w) += filter1(c, r1_stage1.x, r1_stage1.y) * head1_relu_padded(r1_stage1.x, w + r1_stage1.y - 1);

        Func conv1_stage2("conv1_stage2");
        RDom r1_stage2(0, head2_channels, 0, conv_support);
        conv1_stage2(c, w, n) = cast(working_type, conv1_stage1(c, w));  // Broadcast the processed pipeline features across the batch
        conv1_stage2(c, w, n) += (filter1(c, head1_filter.dim(0).extent() + r1_stage2.x, r1_stage2.y) *
                                  head2_relu_padded(r1_stage2.x, w + r1_stage2.y - 1, n));

        Func relu1("relu1");
        relu1(c, w, n) = activation(conv1_stage2(c, w, n));

        Func relu1_padded = pad_stages(relu1, num_stages);

        Func conv2("conv2");
        RDom r2(0, conv1_channels, 0, conv_support);
        conv2(c, w, n) = cast(working_type, bias2(c));
        conv2(c, w, n) += filter2(c, r2.x, r2.y) * relu1_padded(r2.x, w + r2.y - 1, n);

        Func relu2("relu2");
        relu2(c, w, n) = activation(conv2(c, w, n));

        // set boundary conditions for relu2
        Func relu2_padded = pad_stages(relu2, num_stages);

        Func conv3("conv3");
        RDom r3(0, conv2_channels, 0, conv_support);
        conv3(c, w, n) = cast(working_type, bias3(c));
        conv3(c, w, n) += filter3(c, r3.x, r3.y) * relu2_padded(r3.x, w + r3.y - 1, n);

        Func relu3("relu3");
        relu3(c, w, n) = activation(conv3(c, w, n));

        // set boundary conditions for relu3
        Func relu3_padded = pad_stages(relu3, num_stages);

        Func conv4("conv4");
        RDom r4(0, conv3_channels, 0, conv_support);
        conv4(c, w, n) = cast(working_type, bias4(c));
        conv4(c, w, n) += filter4(c, r4.x, r4.y) * relu3_padded(r4.x, w + r4.y - 1, n);

        Func relu4("relu4");
        relu4(c, w, n) = activation(conv4(c, w, n));

        // set boundary conditions for relu4
        Func relu4_padded = pad_stages(relu4, num_stages);

        Func conv5("conv5");
        RDom r5(0, conv4_channels, 0, conv_support);
        conv5(c, w, n) = cast(working_type, bias5(c));
        conv5(c, w, n) += filter5(c, r5.x, r5.y) * relu4_padded(r5.x, w + r5.y - 1, n);

        Func relu5("relu5");
        relu5(c, w, n) = activation(conv5(c, w, n));

        // set boundary conditions for relu5
        Func relu5_padded = pad_stages(relu5, num_stages);

        Func conv6("conv6");
        RDom r6(0, conv5_channels);
        conv6(c, w, n) = cast(working_type, bias6());
        conv6(c, w, n) += filter6(r6) * relu5_padded(r6, w, n);

        /*
        Expr points_computed = schedule_features(n, 4, w);
        Expr inlined_calls = schedule_features(n, 17, w);
        Expr total_points_computed = (points_computed + inlined_calls) / 1000000.0f;
        */
        Func prediction;

        Func relu6("relu6");
        relu6(c, w, n) = activation(conv6(c, w, n));

        RDom r_reduce(0, num_stages);
        prediction(n) += relu6(0, r_reduce, n);

        prediction_output(n) = cast<float>(prediction(n));

        Derivative d_loss_d;
        Func err;

        if (!training) {
            loss_output() = 0.0f;
        } else {

            // The tail end of the reverse-mode pipeline
            RDom r_batch(0, batch_size);

            /*
            Func average_prediction, average_runtime;

            average_prediction() += prediction(r_batch);
            average_prediction() /= batch_size;
            average_runtime() += true_runtime(r_batch);
            average_runtime() /= batch_size;
            */

            //Expr delta = (prediction(n) / average_prediction()) - (true_runtime(n) / average_runtime());
            //Expr delta = 1.0f/(prediction(n) + 0.0001f) - 1.0f/(true_runtime(n) + 0.0001f);
            Expr delta = prediction(n) - true_runtime(n);
            err(n) = delta * delta + 0.001f * sum(-max(conv6(0, r_reduce, n), 0));
            Expr loss = sum(err(r_batch));

            loss_output() = cast<float>(loss);

            d_loss_d = propagate_adjoints(loss_output);

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
        bias6.set_shape();

        // SCHEDULE

        if (auto_schedule) {

            batch_size.set_estimate(1024);
            num_stages.set_estimate(13);
            prediction_output.dim(0).set_bounds_estimate(0, 1024);
            learning_rate.set_estimate(0.001f);
            timestep.set_estimate(37);

        } else {

            Var no;
            prediction_output.specialize(batch_size < 8).split(n, no, n, 1);
            prediction_output.compute_root().split(n, no, n, 8).parallel(no);
            prediction_output.bound(n, 0, batch_size);

            // schedule for the forwards path
            const int vec = 8;

            // A helper function for scheduling conv layers
            auto schedule_conv = [&](Func conv, Func relu, RVar r_channels, RVar r_stencil, Func *pre_conv_padding) {
                Var ci, wi;
                if (!training) {
                    relu.compute_at(prediction_output, n).store_at(prediction_output, no)
                        .tile(c, w, ci, wi, vec*3, 4, TailStrategy::RoundUp)
                        .vectorize(ci, vec).unroll(ci);
                    conv.compute_at(relu, c);
                    if (pre_conv_padding) {
                        pre_conv_padding->in(conv).compute_at(relu, w).vectorize(c);
                    }
                } else {
                    // In training mode, we need the conv activations pre-relu too
                    conv.in().compute_root()
                        .tile(c, w, ci, wi, vec*3, 4, TailStrategy::RoundUp)
                        .vectorize(ci, vec).unroll(ci).unroll(wi).parallel(n, 8);
                    conv.compute_at(conv.in(), c);
                    relu.compute_root().reorder_storage(c, w, n).reorder(c, w, n).vectorize(c, vec).parallel(n, 8);
                    if (pre_conv_padding) {
                        pre_conv_padding->in(conv).compute_at(conv.in(), w).vectorize(c);
                    }
                }
                conv.vectorize(c).unroll(w).update().vectorize(c).unroll(w);
                if (r_stencil.name().empty()) {
                    conv.update().reorder(c, w, r_channels);
                } else {
                    conv.update().reorder(c, w, r_channels, r_stencil);
                }
            };

            // Pipeline features processing
            normalized_pipeline_features.compute_root().vectorize(c, vec);
            head1_relu.compute_root().vectorize(c, vec);
            conv1_stage1.compute_root().vectorize(c, vec);

            // Schedule features processing. The number of schedule
            // features is not close to a multiple of 8, so vectorized
            // across the batch.
            if (!training) {
                normalized_schedule_features
                    .compute_at(prediction_output, no).vectorize(n);
            } else {
                normalized_schedule_features
                    .compute_root().vectorize(n, 8);
            }

            // conv+relu layers
            schedule_conv(head2_conv, head2_relu, r_head2.x, RVar(""), nullptr);
            schedule_conv(conv1_stage2, relu1, r1_stage2.x, r1_stage2.y, &head2_relu_padded);
            schedule_conv(conv2, relu2, r2.x, r2.y, &relu1_padded);
            schedule_conv(conv3, relu3, r3.x, r3.y, &relu2_padded);
            schedule_conv(conv4, relu4, r4.x, r4.y, &relu3_padded);
            schedule_conv(conv5, relu5, r5.x, r5.y, &relu4_padded);
            schedule_conv(conv6, relu6, r6.x, RVar(""), nullptr);

            if (training) {
                // We now use a bespoke mini-autoscheduler to schedule the
                // other reverse stages. TODO: apply the real
                // autoscheduler to this in some sort of staged
                // compilation setup.

                auto reorder_outermost = [](Stage s, VarOrRVar v) {
                    Var t;
                    s.split(Var::outermost(), Var::outermost(), t, 1).reorder(t, v);
                };

                auto vectorize_innermost = [](Func f) {
                    auto storage_dims = f.function().schedule().storage_dims();
                    if (storage_dims.empty()) return;
                    const auto &innermost_storage_dim = storage_dims[0].var;

                    auto vectorize_innermost_of_stage = [&](Stage s) {
                        auto sched = s.get_schedule();

                        // First try vectorizing the innermost storage
                        // dimension, then the innermost pure loop
                        // dimension.
                        for (auto d : sched.dims()) {
                            if (d.var == innermost_storage_dim) {
                                s.vectorize(Var(d.var), vec, TailStrategy::RoundUp);
                                return;
                            }
                        }

                        for (auto d : sched.dims()) {
                            // Only vectorize unsplit dimensions
                            if (d.var.find('.') != std::string::npos) continue;
                            if (d.is_pure()) {
                                if (d.is_rvar()) {
                                    s.vectorize(RVar(d.var), vec);
                                } else {
                                    s.vectorize(Var(d.var), vec, TailStrategy::RoundUp);
                                }
                                return;
                            }
                        }
                    };

                    vectorize_innermost_of_stage(f);
                    for (int i = 0; i < f.num_update_definitions(); i++) {
                        vectorize_innermost_of_stage(f.update(i));
                    }
                };

                auto factor_batch_reduction = [&](Func f) -> Func {
                    RVar batch_reduce_rvar;
                    bool found = false;

                    auto rvars = f.function().update_schedule(0).rvars();
                    for (auto rv : rvars) {
                        Expr extent = simplify(rv.extent);
                        if (Internal::can_prove(extent == batch_size)) {
                            found = true;
                            batch_reduce_rvar = RVar(rv.var);
                        }
                    }

                    Func intm;
                    if (found) {
                        reorder_outermost(f.update(), batch_reduce_rvar);
                        RVar ro, ri;
                        intm = f.update().split(batch_reduce_rvar, ro, ri, 8).rfactor(ro, no);
                        intm.in().compute_root().parallel(no);
                        intm.compute_at(intm.in(), no);
                        vectorize_innermost(intm);
                        vectorize_innermost(intm.in());
                    }

                    f.in().compute_root();
                    vectorize_innermost(f.in());

                    return intm;
                };

                auto schedule_weight_gradient = [&](Func filter, Func bias) {
                    Func dfilter = d_loss_d(filter, -1, false);
                    Func dbias = d_loss_d(bias, -1, false);
                    factor_batch_reduction(dfilter);
                    factor_batch_reduction(dbias);
                };

                auto schedule_activation_gradient = [&](Func a) {
                    Func da = d_loss_d(a, -1, false);

                    reorder_outermost(da.in(), n);
                    da.in().compute_root().parallel(n, 8);
                    da.compute_at(da.in(), n);
                    vectorize_innermost(da);
                    vectorize_innermost(da.in());
                };

                // Convs that compute loss contributions due to each weight
                schedule_weight_gradient(head1_filter, head1_bias);
                schedule_weight_gradient(head2_filter, head2_bias);
                schedule_weight_gradient(filter1, bias1);
                schedule_weight_gradient(filter2, bias2);
                schedule_weight_gradient(filter3, bias3);
                schedule_weight_gradient(filter4, bias4);
                schedule_weight_gradient(filter5, bias5);
                schedule_weight_gradient(filter6, bias6);

                // Convs that compute the activation gradients
                schedule_activation_gradient(head2_relu_padded);
                schedule_activation_gradient(relu1_padded);
                schedule_activation_gradient(relu2_padded);
                schedule_activation_gradient(relu3_padded);
                schedule_activation_gradient(relu4_padded);
                schedule_activation_gradient(relu5_padded);

                // Schedule the reverse Funcs for everything else
                for (Func f : {normalized_schedule_features, normalized_pipeline_features,
                            head1_conv, head1_relu,
                            head2_conv, head2_relu,
                            conv1_stage1, conv1_stage2, relu1,
                            conv2, relu2,
                            conv3, relu3,
                            conv4, relu4,
                            conv5, relu5,
                            conv6, relu6,
                            prediction,
                            err, Func(loss_output)}) {
                    for (auto g : d_loss_d.funcs(f)) {
                        g.compute_root();
                        vectorize_innermost(g);
                    }
                }
            }
        }
    }
};

using CostModelInference = CostModel<false>;
using CostModelTraining = CostModel<true>;

HALIDE_REGISTER_GENERATOR(CostModelInference, halide_autoscheduler_cost_model);
HALIDE_REGISTER_GENERATOR(CostModelTraining, halide_autoscheduler_train_cost_model);
