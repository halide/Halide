#include "Halide.h"
#include "Derivative.h"

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
    using Generator<CostModel<training>>::get_pipeline;

    // Inputs
    Input<int> num_stages{ "num_stages", 1 };
    Input<int> batch_size{ "batch_size", 1 };
    Input<int> num_cores{ "num_cores", 1 };
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
        const int conv1_channels = 16;
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

        // Unpack all of the schedule features
        int idx = 0;
        Expr num_realizations = schedule_features(n, idx++, w);
        Expr num_productions = schedule_features(n, idx++, w);
        Expr points_computed_per_realization = schedule_features(n, idx++, w);
        Expr points_computed_per_production = schedule_features(n, idx++, w);
        Expr points_computed_total = schedule_features(n, idx++, w);
        Expr points_computed_minimum = schedule_features(n, idx++, w);
        Expr innermost_loop_extent = schedule_features(n, idx++, w);
        Expr innermost_pure_loop_extent = schedule_features(n, idx++, w);
        Expr inner_parallelism = schedule_features(n, idx++, w);
        Expr outer_parallelism = schedule_features(n, idx++, w);

        Expr bytes_at_realization = schedule_features(n, idx++, w);
        Expr bytes_at_production = schedule_features(n, idx++, w);
        Expr bytes_at_root = schedule_features(n, idx++, w);
        Expr innermost_bytes_at_realization = schedule_features(n, idx++, w);
        Expr innermost_bytes_at_production = schedule_features(n, idx++, w);
        Expr innermost_bytes_at_root = schedule_features(n, idx++, w);

        Expr inlined_calls = schedule_features(n, idx++, w);
        Expr unique_bytes_read_per_realization = schedule_features(n, idx++, w);
        Expr unique_lines_read_per_realization = schedule_features(n, idx++, w);
        Expr allocation_bytes_read_per_realization = schedule_features(n, idx++, w);
        Expr working_set = schedule_features(n, idx++, w);

        Expr vector_size = schedule_features(n, idx++, w);
        Expr native_vector_size = schedule_features(n, idx++, w);
        Expr num_vectors = schedule_features(n, idx++, w);
        Expr vector_loads_per_vector = schedule_features(n, idx++, w);
        Expr scalar_loads_per_vector = schedule_features(n, idx++, w);
        assert(idx == head2_w);

        // Account for idle simd lanes
        Expr vector_recompute = native_vector_size / max(1, vector_size);

        // Account for idle cores
        Expr tasks_per_core = (inner_parallelism * outer_parallelism) / max(1, num_cores);

        // tasks_per_core = max(1, tasks_per_core); // Avoid NaNs on corrupted input data

        Expr idle_core_wastage = ceil(tasks_per_core) / tasks_per_core;

        // Extract a few of them as things that might have a runtime cost per instance
        Expr terms[conv1_channels] = {num_realizations, // cost per allocation
                                      inner_parallelism * num_productions, // cost per thread pool task
                                      select(inner_parallelism > 1.0f, num_productions, 0), // cost per parallel job launch
                                      points_computed_total * vector_recompute * idle_core_wastage, // cost per point computed
                                      inlined_calls * vector_recompute * idle_core_wastage,  // cost per inlined evaluation of the Func
                                      bytes_at_production * num_realizations, // cost per byte stored
                                      num_vectors, // cost per vector stored
                                      scalar_loads_per_vector * num_vectors * idle_core_wastage, // cost per scalar load
                                      vector_loads_per_vector * num_vectors * idle_core_wastage, // cost per vector load
                                      unique_bytes_read_per_realization * num_realizations, // cost per byte pulled into cache
                                      (bytes_at_realization / max(1, innermost_bytes_at_realization)) * num_realizations, // cost per line stored
                                      unique_lines_read_per_realization * num_realizations, // cost per line pulled into cache
                                      working_set * num_realizations, // cost per temporary byte allocated during realization
                                      0.0f,
                                      0.0f,
                                      1.0f};

        Expr e = cast(working_type, 0);
        for (int i = 0; i < conv1_channels; i++) {
            e += terms[i] * relu1(i, w, n);
        }

        Func runtime_per_stage;
        runtime_per_stage(n, w) = e * 1e-9f;

        Func prediction;
        RDom r_reduce(0, num_stages);
        prediction(n) += runtime_per_stage(n, r_reduce);

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
            */

            // We believe the coefficients on all the various
            // components of cost should be positive, even before the
            // relu, and even before schedule-specific features are
            // taken into account. The network shouldn't be telling us
            // that things would be cheaper if we would do more
            // mallocs, or compute more values, or launch more
            // parallel tasks.
            RDom r_conv1_output(0, conv1_channels, 0, num_stages);
            Expr regularize1 = sum(-min(conv1_stage2(r_conv1_output.x, r_conv1_output.y, n), 0));
            Expr regularize2 = sum(-min(conv1_stage1(r_conv1_output.x, r_conv1_output.y), 0));

            auto sigmoid = [](Expr x) {
                return (1 - 1 / abs(x)) * select(x > 0, 1.0f, -1.0f);
            };


            Expr n2 = (n + 1) % batch_size;
            Expr scale = 1.0f / max(1, true_runtime(0));
            Expr p1 = prediction(n) * scale, p2 = prediction(n2) * scale;
            Expr r1 = true_runtime(n) * scale, r2 = true_runtime(n2) * scale;

            // The network should predict runtime
            Expr delta = abs(p1 - r1);

            // More importantly, the network should predict runtimes
            // in the correct order

            // A reward for being confident and correct, a penalty for
            // being confident and wrong, and nothing when the result
            // is not confident. Size of reward or penalty is also
            // scaled by just how different the two true runtimes are.
            Expr confidence = 1 - 1 / (abs(p1 - p2) + 1);
            Expr significance = 1 - 1 / (abs(r1 - r2) + 1);
            Expr correct_order = confidence * significance * select((r1 > r2) == (p1 > p2), -1.0f, 1.0f);
            err(n) = correct_order + 1e-3f * delta + 1e-10f * regularize1;

            Expr loss = sum(err(r_batch));

            loss_output() = cast<float>(loss) + 1e-10f * regularize2;

            d_loss_d = propagate_adjoints(loss_output);

            Weight *weights[] = {&head1_filter, &head1_bias,
                                 &head2_filter, &head2_bias,
                                 &filter1, &bias1};

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

        batch_size.set_estimate(80);
        num_stages.set_estimate(13);
        prediction_output.dim(0).set_bounds_estimate(0, 80);
        learning_rate.set_estimate(0.001f);
        timestep.set_estimate(37);

        // SCHEDULE

        if (auto_schedule) {
            // Blank

        } else if (training) {
            // Output by the autoscheduler in autotuning mode

            #include "cost_model_schedule.h"
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
                        .tile(c, w, ci, wi, vec, 1, TailStrategy::RoundUp)
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
                                s.vectorize(Var(d.var), vec);
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
                                    s.vectorize(Var(d.var), vec);
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
                        //vectorize_innermost(intm);
                        //vectorize_innermost(intm.in());
                    }

                    f.in().compute_root();
                    //vectorize_innermost(f.in());

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
                    //vectorize_innermost(da);
                    //vectorize_innermost(da.in());
                };

                // Convs that compute loss contributions due to each weight
                schedule_weight_gradient(head1_filter, head1_bias);
                schedule_weight_gradient(head2_filter, head2_bias);
                schedule_weight_gradient(filter1, bias1);

                // Convs that compute the activation gradients
                schedule_activation_gradient(head2_relu_padded);
                schedule_activation_gradient(relu1);

                // Schedule the reverse Funcs for everything else
                for (Func f : {normalized_schedule_features, normalized_pipeline_features,
                            head1_conv, head1_relu,
                            head2_conv, head2_relu,
                            conv1_stage1, conv1_stage2,
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

HALIDE_REGISTER_GENERATOR(CostModelInference, cost_model);
HALIDE_REGISTER_GENERATOR(CostModelTraining, train_cost_model);
