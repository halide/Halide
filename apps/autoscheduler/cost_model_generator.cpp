#include "Halide.h"
#include "Derivative.h"

#include "cost_model_schedule.h"
#include "NetworkSize.h"

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
            dim(0).set_bounds_estimate(0, s0);
            grad.dim(0).set_bounds(0, s0);
            grad.dim(0).set_bounds_estimate(0, s0);
            grad.bound(grad.args()[0], 0, s0);
            grad.estimate(grad.args()[0], 0, s0);
        }
        if (s1) {
            dim(1).set_bounds(0, s1);
            dim(1).set_bounds_estimate(0, s1);
            grad.dim(1).set_bounds(0, s1);
            grad.dim(1).set_bounds_estimate(0, s1);
            grad.bound(grad.args()[1], 0, s1);
            grad.estimate(grad.args()[1], 0, s1);

        }
        if (s2) {
            dim(2).set_bounds(0, s2);
            dim(2).set_bounds_estimate(0, s2);
            grad.dim(2).set_bounds(0, s2);
            grad.dim(2).set_bounds_estimate(0, s2);
            grad.bound(grad.args()[2], 0, s2);
            grad.estimate(grad.args()[2], 0, s2);
        }
        grad.dim(dimensions()).set_bounds(0, 4);
        grad.dim(dimensions()).set_bounds_estimate(0, 4);
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

    // Network weights. These are parameters instead of baked-in
    // buffers so that they can be swapped out using an environment
    // variable at runtime. In training mode they are also outputs.
    using Weight = ModelWeight<training>;
    Weight head1_filter{ "head1_filter", 3 };
    Weight head1_bias{ "head1_bias", 1 };
    Weight head2_filter{ "head2_filter", 2 };
    Weight head2_bias{ "head2_bias", 1 };
    Weight filter1{ "filter1", 2 };
    Weight bias1{ "bias1", 1 };

    // Some extra inputs for training mode. Really should be conditional on 'training'.
    Input<float> learning_rate{ "learning_rate", 1.0f };
    Input<int> timestep{ "timestep", 0 }; // Needed by ADAM
    Input<int> reference{ "reference", 0 }; // Which schedule should we compare everything to for the pairwise ranking loss?
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

    Expr sigmoid(Expr e) {
        return 1 / (1 + exp(-e));
    }

    void generate() {
        Var c("c"), w("w"), n("n"), j("j"), s("s");

        Func normalized_schedule_features("normalized_schedule_features");
        normalized_schedule_features(n, c, s) = fast_log(schedule_features(n, c, s) + 1);

        Func squashed_head1_filter("squashed_head1_filter");
        squashed_head1_filter(c, w, n) = sigmoid(head1_filter(c, w, n));

        Func head1_conv("head1_conv");
        RDom r_head1(0, head1_w, 0, head1_h);
        head1_conv(c, w) = head1_bias(c);
        head1_conv(c, w) += squashed_head1_filter(c, r_head1.x, r_head1.y) * pipeline_features(r_head1.x, r_head1.y, w);

        // No point in a relu - the inputs and weights are positive

        Func head2_conv("head2_conv");
        RDom r_head2(0, head2_w);
        head2_conv(c, w, n) = head2_bias(c);
        head2_conv(c, w, n) += head2_filter(c, r_head2) * normalized_schedule_features(n, r_head2, w);

        Func head2_relu("head2_relu");
        head2_relu(c, w, n) = activation(head2_conv(c, w, n));

        Func conv1_stage1("conv1_stage1");
        RDom r1_stage1(0, head1_channels);
        conv1_stage1(c, w) = bias1(c);
        conv1_stage1(c, w) += filter1(c, r1_stage1.x) * head1_conv(r1_stage1.x, w);

        Func conv1_stage2("conv1_stage2");
        RDom r1_stage2(0, head2_channels);
        conv1_stage2(c, w, n) = conv1_stage1(c, w);
        conv1_stage2(c, w, n) += filter1(c, head1_filter.dim(0).extent() + r1_stage2.x) * head2_relu(r1_stage2.x, w, n);

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
        Expr unrolled_loop_extent = schedule_features(n, idx++, w);

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
        Expr num_scalars = schedule_features(n, idx++, w);
        Expr vector_loads_per_vector = schedule_features(n, idx++, w);
        Expr scalar_loads_per_vector = schedule_features(n, idx++, w);
        Expr scalar_loads_per_scalar = schedule_features(n, idx++, w);
        Expr bytes_at_task = schedule_features(n, idx++, w);
        Expr innermost_bytes_at_task = schedule_features(n, idx++, w);
        Expr unique_bytes_read_per_vector = schedule_features(n, idx++, w);
        Expr unique_lines_read_per_vector = schedule_features(n, idx++, w);
        Expr unique_bytes_read_per_task = schedule_features(n, idx++, w);
        Expr unique_lines_read_per_task = schedule_features(n, idx++, w);
        Expr working_set_at_task = schedule_features(n, idx++, w);
        Expr working_set_at_production = schedule_features(n, idx++, w);
        Expr working_set_at_realization = schedule_features(n, idx++, w);
        Expr working_set_at_root = schedule_features(n, idx++, w);
        assert(idx == head2_w);

        /*

        // Count up the number of things computed
        Expr compute_cost = select(inlined_calls == 0,
                                   (vector_size * num_vectors * relu1(0, w, n) +
                                    num_scalars * relu1(1, w, n)),
                                   (vector_size * num_vectors * relu1(2, w, n) +
                                    num_scalars * relu1(3, w, n)));

        Expr num_tasks = max(1, inner_parallelism * outer_parallelism);
        Expr tasks_per_core = num_tasks / num_cores;
        Expr idle_core_wastage = ceil(tasks_per_core) / max(1, tasks_per_core);
        compute_cost *= idle_core_wastage;

        Expr load_cost = (num_realizations * unique_lines_read_per_realization * relu1(5, w, n) +
                          num_realizations * unique_bytes_read_per_realization * relu1(6, w, n) +
                          num_vectors * vector_loads_per_vector * relu1(7, w, n) +
                          num_scalars * scalar_loads_per_scalar * relu1(8, w, n) +
                          num_vectors * scalar_loads_per_vector * relu1(9, w, n) +
                          num_scalars * unique_bytes_read_per_vector * relu1(10, w, n) +
                          num_vectors * unique_bytes_read_per_vector * relu1(11, w, n) +
                          num_scalars * unique_lines_read_per_vector * relu1(12, w, n) +
                          num_vectors * unique_lines_read_per_vector * relu1(13, w, n) +
                          num_tasks * unique_bytes_read_per_task * relu1(14, w, n) +
                          num_tasks * unique_lines_read_per_task * relu1(15, w, n));

        // Estimate the number of cache misses on the data that this writes to and their cost
        Expr lines_written_per_realization = inner_parallelism * (bytes_at_task / max(1, innermost_bytes_at_task));

        // Use separate coefficients for things with internal
        // parallelism, because for stages with internal parallelism,
        // most values produced will be consumed on another core, so
        // they will get punted out to L3 no matter how small.
        Expr alpha = select(inner_parallelism > 1, relu1(16, w, n),
                            w == 0, relu1(17, w, n),
                            relu1(18, w, n));
        Expr beta = select(inner_parallelism > 1, relu1(19, w, n),
                           w == 0, relu1(20, w, n),
                           relu1(21, w, n));

        Expr store_cost = num_realizations * (lines_written_per_realization * alpha +
                                              bytes_at_realization * beta);

        // Now account for false sharing of cache lines. The
        // probability of a store hitting a cache line also hit by
        // another core is inversely proportional to
        // innermost_bytes_at_task, and the cost is paid on every
        // store.
        Expr cost_of_false_sharing = select(inner_parallelism > 1, relu1(22, w, n) * (num_vectors + num_scalars) / max(1, innermost_bytes_at_task), 0.0f);

        store_cost += cost_of_false_sharing;

        // Now add a term for false sharing of pages. The maximum
        // number of threads that could all fault on the same page at
        // the same time is:
        Expr max_threads_hitting_same_page_fault = min(inner_parallelism, 4096 / max(1, innermost_bytes_at_task));

        // The total number of page faults is proportionate to the number of bytes allocated
        Expr num_page_faults = bytes_at_production;

        // And page faults are serviced serially, so the total CPU time gets multiplied by the thread count again!
        Expr cost_of_page_faults = num_page_faults * max_threads_hitting_same_page_fault * inner_parallelism * outer_parallelism * relu1(23, w, n);

        store_cost += cost_of_page_faults;

        // Malloc aint free. Small allocations should go on the stack, but this isn't totally reliable.
        Expr cost_of_malloc = relu1(24, w, n) * num_realizations;

        Expr cost_of_parallel_launches = num_productions * select(inner_parallelism > 1, relu1(25, w, n), 0.0f);

        Expr cost_of_parallel_tasks = num_productions * (inner_parallelism - 1) * relu1(26, w, n);

        Expr cost_of_parallelism = cost_of_parallel_tasks + cost_of_parallel_launches;

        // Penalize working sets that start to fall out of cache
        Expr cost_of_working_set = working_set * relu1(27, w, n);

        Expr cost = compute_cost + store_cost + load_cost + store_cost + cost_of_malloc + cost_of_parallelism + cost_of_working_set;
        */

        // Aggressively simplified model
        Expr cost = relu1(0, w, n) * (num_vectors * vector_size + num_scalars) + relu1(1, w, n);

        // Keep the schedule fixed by adding a dependence to all out channels
        for (int i = 0; i < conv1_channels; i++) {
            cost += 0.0f * relu1(i, w, n);
        }

        Func runtime_per_stage;
        runtime_per_stage(n, w) = cost * 1e-9f;

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
            Expr regularize = sum(-min(conv1_stage2(r_conv1_output.x, r_conv1_output.y, n), 0));

            Expr n2 = clamp(reference, 0, batch_size-1);
            Expr scale = 1.0f / true_runtime(n2);
            Expr p1 = prediction(n) * scale;
            Expr p2 = prediction(n2) * scale;
            Expr r1 = true_runtime(n) * scale;

            // The network should predict runtimes in the correct
            // order

            // Assume there's noise in the runtime measurements, and
            // use a similar sigmoid to determine the probability that
            // A really *is* faster than B. We scale the absolute
            // difference in runtime so that if a sample is 30% slower
            // we're 90% confident that it is indeed slower.
            Expr significance = 1 - 1 / r1;

            // p1 should be at least 1 larger than p2, in units of the true runtime of the fastest schedule
            Expr correct_order = significance * max(0, p2 + 1 - p1);
            err(n) = correct_order + 1e-5f * regularize;

            Expr loss = sum(err(r_batch));

            loss_output() = loss;

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
        filter1.set_shape(conv1_channels, head1_channels + head2_channels);
        bias1.set_shape(conv1_channels);
        num_cores.set_estimate(32);

        reference.set_estimate(0);
        batch_size.set_estimate(80);
        num_stages.set_estimate(13);
        prediction_output.dim(0).set_bounds_estimate(0, 80);
        learning_rate.set_estimate(0.001f);
        timestep.set_estimate(37);
        pipeline_features
            .dim(0).set_bounds_estimate(0, head1_w)
            .dim(1).set_bounds_estimate(0, head1_h)
            .dim(2).set_bounds_estimate(0, 13);
        schedule_features
            .dim(0).set_bounds_estimate(0, 80)
            .dim(1).set_bounds_estimate(0, head2_w)
            .dim(2).set_bounds_estimate(0, 13);
        true_runtime
            .dim(0).set_bounds_estimate(0, 80);

        // SCHEDULE
        if (training && !auto_schedule) {
            do_cost_model_schedule(get_pipeline());
        } else if (auto_schedule) {
            // Blank
        } else {

            Var no;
            prediction_output.specialize(batch_size < 8).split(n, no, n, 1);
            prediction_output.compute_root().split(n, no, n, 8).parallel(no);
            prediction_output.bound(n, 0, batch_size);

            // schedule for the forwards path
            const int vec = 8;

            // A helper function for scheduling conv layers
            auto schedule_conv = [&](Func conv, Func relu, RVar r_channels) {
                Var ci, wi;
                if (!training) {
                    relu.compute_at(prediction_output, n).store_at(prediction_output, no)
                        .tile(c, w, ci, wi, vec, 4, TailStrategy::RoundUp)
                        .vectorize(ci);
                    conv.compute_at(relu, c);
                } else {
                    // In training mode, we need the conv activations pre-relu too
                    conv.in().compute_root()
                        .tile(c, w, ci, wi, vec, 1, TailStrategy::RoundUp)
                        .vectorize(ci).unroll(wi).parallel(n, 8);
                    conv.compute_at(conv.in(), c);
                    relu.compute_root().reorder_storage(c, w, n).reorder(c, w, n).vectorize(c, vec).parallel(n, 8);
                }
                conv.vectorize(c).unroll(w).update().vectorize(c).unroll(w).reorder(c, w, r_channels);
            };

            // Pipeline features processing
            conv1_stage1.compute_root().vectorize(c);
            squashed_head1_filter.compute_root().vectorize(c);

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
            schedule_conv(head2_conv, head2_relu, r_head2.x);
            schedule_conv(conv1_stage2, relu1, r1_stage2.x);
        }
    }
};

using CostModelInference = CostModel<false>;
using CostModelTraining = CostModel<true>;

HALIDE_REGISTER_GENERATOR(CostModelInference, cost_model);
HALIDE_REGISTER_GENERATOR(CostModelTraining, train_cost_model);
