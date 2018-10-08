#include "Halide.h"

using namespace Halide;

class CostModel : public Generator<CostModel> {
public:
    Input<int> num_stages{ "num_stages" };

    // The weights are parameters instead of baked-in buffers so that
    // they can be swapped out using an environment variable at
    // runtime.
    Input<Buffer<float>> pipeline_features{ "pipeline_features", 3 };
    Input<Buffer<float>> schedule_features{ "schedule_features", 3 };
    Input<Buffer<float>> pipeline_mean{ "pipeline_mean", 2 };
    Input<Buffer<float>> pipeline_std{ "pipeline_std", 2 };
    Input<Buffer<float>> schedule_mean{ "schedule_mean", 1 };
    Input<Buffer<float>> schedule_std{ "schedule_std", 1 };
    Input<Buffer<float>> head1_filter{ "head1_filter", 3 };
    Input<Buffer<float>> head1_bias{ "head1_bias", 1 };
    Input<Buffer<float>> head2_filter{ "head2_filter", 2 };
    Input<Buffer<float>> head2_bias{ "head2_bias", 1 };
    Input<Buffer<float>> filter1{ "filter1", 3 };
    Input<Buffer<float>> bias1{ "bias1", 1 };
    Input<Buffer<float>> filter2{ "filter2", 3 };
    Input<Buffer<float>> bias2{ "bias2", 1 };
    Input<Buffer<float>> filter3{ "filter3", 3 };
    Input<Buffer<float>> bias3{ "bias3", 1 };
    Input<Buffer<float>> filter4{ "filter4", 3 };
    Input<Buffer<float>> bias4{ "bias4", 1 };
    Input<Buffer<float>> filter5{ "filter5", 3 };
    Input<Buffer<float>> bias5{ "bias5", 1 };
    Input<Buffer<float>> filter6{ "filter6", 1 };
    Input<Buffer<float>> bias6{ "bias6", 0 };
    Output<Buffer<float>> prediction{ "prediction", 1 };

    Func f_head1_conv{ "f_head1_conv" }, f_head2_conv{ "f_head2_conv" };
    Func f_head1_relu{ "f_head1_relu" }, f_head2_relu{ "f_head2_relu" };
    Func f_head1_relu_padded{ "f_head1_relu_padded" }, f_head2_relu_padded{ "f_head2_relu_padded" };
    Func f_conv1_stage1{ "f_conv1_stage1" }, f_conv1_stage2{ "f_conv1_stage2" };
    Func f_conv2{ "f_conv2" }, f_conv3{ "f_conv3" }, f_conv4{ "f_conv4" }, f_conv5{ "f_conv5" }, f_conv6{ "f_conv6" };
    Func f_ReLU1{ "f_ReLU1" }, f_relu1_padded{ "f_relu1_padded" };
    Func f_ReLU2{ "f_ReLU2" }, f_relu2_padded{ "f_relu2_padded" };
    Func f_ReLU3{ "f_ReLU3" }, f_relu3_padded{ "f_relu3_padded" };
    Func f_ReLU4{ "f_ReLU4" }, f_relu4_padded{ "f_relu4_padded" };
    Func f_ReLU5{ "f_ReLU5" }, f_relu5_padded{ "f_relu5_padded" };
    Func f_ReLU6{ "f_ReLU6" }, f_relu6_padded{ "f_relu6_padded" };
    Func f_pool3{ "f_pool3" }, f_pool3_padded{ "f_pool3_padded" };
    Func f_pool4{ "f_pool4" }, f_pool4_padded{ "f_pool4_padded" };
    Func f_reduce{ "f_reduce" };
    Func normalized_pipeline_features{ "normalized_pipeline_features" };
    Func normalized_schedule_features{ "normalized_schedule_features" };

    Var c{ "c" }, w{ "w" }, n{ "n" }, i{ "i" }, j{ "j" }, s{ "s" };

    Func pad_stages(Func f, Expr stages) {
        std::vector<std::pair<Expr, Expr>> bounds(f.dimensions());
        bounds.back().first = 0;
        bounds.back().second = stages;
        return BoundaryConditions::constant_exterior(f, 0.0f, bounds);
    }

    void generate() {

        // The memory layout of the weights is matrix-style
        for (auto *b : {&filter1, &filter2, &filter3, &filter4,
                    &filter5, &filter6, &head1_filter, &head2_filter,
                    &pipeline_mean, &pipeline_std} ) {
            b->dim(0).set_stride(Expr()).dim(b->dimensions() - 1).set_stride(1);
        }

        Expr padded_stages = max(num_stages, 22);
        Expr first_valid = max(0, (padded_stages - num_stages) / 2);

        normalized_pipeline_features(i, j, s) = 0.0f;
        RDom r_s(first_valid, num_stages);
        normalized_pipeline_features(i, j, r_s) =
            (pipeline_features(i, j, r_s - first_valid) - pipeline_mean(i, j)) / pipeline_std(i, j);

        normalized_schedule_features(n, i, s) = 0.0f;
        normalized_schedule_features(n, i, r_s) =
            (fast_log(schedule_features(n, i, r_s - first_valid) + 1) - schedule_mean(i)) / schedule_std(i);

        RDom r_head1(head1_filter.dim(1).min(), head1_filter.dim(1).extent(),
                     head1_filter.dim(2).min(), head1_filter.dim(2).extent());
        f_head1_conv(c, w) = head1_bias(c);
        f_head1_conv(c, w) += head1_filter(c, r_head1.x, r_head1.y) * normalized_pipeline_features(r_head1.x, r_head1.y, w);
        f_head1_relu(c, w) = max(0, f_head1_conv(c, w));

        RDom r_head2(head2_filter.dim(1).min(), head2_filter.dim(1).extent());
        f_head2_conv(n, c, w) = head2_bias(c);
        f_head2_conv(n, c, w) += head2_filter(c, r_head2) * normalized_schedule_features(n, r_head2, w);
        f_head2_relu(n, c, w) = max(0, f_head2_conv(n, c, w));

        // we want to enforce boundary conditions on f_head1_relu and f_head2_relu because conv1 pads with 1 zero on either
        // side of the width (i.e. final dimension) of its input. We set the valid range of the width from 0 to num_stages.
        f_head1_relu_padded = pad_stages(f_head1_relu, padded_stages);
        f_head2_relu_padded = pad_stages(f_head2_relu, padded_stages);

        /***** network trunk *****/
        // first 20 input channels are from head1_relu, next 20 input channels are from head2_relu
        // have to do two stagees for conv1 to convolve over each head's outputs
        RDom r1_stage1(head1_filter.dim(0).min(), head1_filter.dim(0).extent(),
                       filter1.dim(2).min(), filter1.dim(2).extent());
        f_conv1_stage1(c, w) = bias1(c);
        f_conv1_stage1(c, w) += filter1(c, r1_stage1.x, r1_stage1.y) * f_head1_relu_padded(r1_stage1.x, w + r1_stage1.y - 1);

        RDom r1_stage2(head2_filter.dim(0).min(), head2_filter.dim(0).extent(),
                       filter1.dim(2).min(), filter1.dim(2).extent());
        f_conv1_stage2(n, c, w) = f_conv1_stage1(c, w);  // Broadcast the processed pipeline features across the batch
        f_conv1_stage2(n, c, w) += filter1(c, head1_filter.dim(0).extent() + r1_stage2.x, r1_stage2.y) * f_head2_relu_padded(n, r1_stage2.x, w + r1_stage2.y - 1);
        f_ReLU1(n, c, w) = max(0, f_conv1_stage2(n, c, w));

        // set boundary conditions for f_ReLU1
        f_relu1_padded = pad_stages(f_ReLU1, padded_stages);

        RDom r2(filter2.dim(1).min(), filter2.dim(1).extent(),
                filter2.dim(2).min(), filter2.dim(2).extent());
        f_conv2(n, c, w) = bias2(c);
        f_conv2(n, c, w) += filter2(c, r2.x, r2.y) * f_relu1_padded(n, r2.x, w + r2.y - 1);

        f_ReLU2(n, c, w) = max(0, f_conv2(n, c, w));

        // set boundary conditions for f_ReLU2
        f_relu2_padded = pad_stages(f_ReLU2, padded_stages);

        RDom r3(filter3.dim(1).min(), filter3.dim(1).extent(),
                filter3.dim(2).min(), filter3.dim(2).extent());
        f_conv3(n, c, w) = bias3(c);
        f_conv3(n, c, w) += filter3(c, r3.x, r3.y) * f_relu2_padded(n, r3.x, w + r3.y - 1);
        f_ReLU3(n, c, w) = max(0, f_conv3(n, c, w));

        // set boundary conditions for f_ReLU3
        f_relu3_padded = pad_stages(f_ReLU3, padded_stages);

        f_pool3(n, c, w) = 0.5f * (f_relu3_padded(n, c, w * 2 - 1) + f_relu3_padded(n, c, w * 2));

        // set boundary conditions for f_pool3
        f_pool3_padded = pad_stages(f_pool3, padded_stages / 2 + 1);

        RDom r4(filter4.dim(1).min(), filter4.dim(1).extent(),
                filter4.dim(2).min(), filter4.dim(2).extent());

        f_conv4(n, c, w) = bias4(c);
        f_conv4(n, c, w) += filter4(c, r4.x, r4.y) * f_pool3_padded(n, r4.x, w + r4.y - 1);
        f_ReLU4(n, c, w) = max(0, f_conv4(n, c, w));

        // set boundary conditions for f_ReLU4
        f_relu4_padded = pad_stages(f_ReLU4, padded_stages / 2 + 1);

        f_pool4(n, c, w) = 0.5f * (f_relu4_padded(n, c, w * 2 - 1) + f_relu4_padded(n, c, w * 2));

        // set boundary conditions for f_pool4
        f_pool4_padded = pad_stages(f_pool4, (padded_stages + 6) / 4);

        RDom r5(filter5.dim(1).min(), filter5.dim(1).extent(),
                filter5.dim(2).min(), filter5.dim(2).extent());

        f_conv5(n, c, w) = bias5(c);
        f_conv5(n, c, w) += filter5(c, r5.x, r5.y) * f_pool4_padded(n, r5.x, w + r5.y - 1);
        f_ReLU5(n, c, w) = max(0, f_conv5(n, c, w));

        // set boundary conditions for f_ReLU5
        f_relu5_padded = pad_stages(f_ReLU5, (padded_stages + 6) / 4);

        RDom r6(filter6.dim(0).min(), filter6.dim(0).extent());
        f_conv6(n, w) = bias6();
        f_conv6(n, w) += filter6(r6) * f_relu5_padded(n, r6, w);
        f_ReLU6(n, w) = max(0, f_conv6(n, w));

        // set boundary conditions for f_ReLU5 (TODO: This is not necessary)
        f_relu6_padded = pad_stages(f_ReLU6, (padded_stages + 6) / 4);

        // reduce over a region that expands to 3x1 convs from the first two stages to the last two stages with zero padding
        RDom r_reduce(0, (padded_stages + 6) / 4);
        f_reduce(n) = 0.0f;
        f_reduce(n) += f_relu6_padded(n, r_reduce);

        prediction(n) = f_reduce(n);
    }

    void schedule() {
        // schedule
        Expr batch_size = prediction.dim(0).extent();
        prediction.bound(n, 0, batch_size);

        // Pipeline features processing
        normalized_pipeline_features.compute_root().vectorize(i, 8).update().vectorize(i, 8);
        normalized_schedule_features.compute_at(prediction, n)
            .specialize(batch_size >= 8)
            .vectorize(n, 8);
        f_head1_relu.compute_root().vectorize(c, 8);
        f_conv1_stage1.compute_root().vectorize(c, 8);

        // TODO: memoize?

        // Schedule features processing
        normalized_schedule_features.update().specialize(batch_size >= 8).vectorize(n, 8);
        f_head2_relu.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        f_ReLU1.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        f_ReLU2.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        f_ReLU3.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        f_pool3.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        f_ReLU4.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        f_pool4.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        f_ReLU5.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        f_ReLU6.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
        prediction.compute_root()
            .specialize(batch_size >= 8)
            .vectorize(n, 8)
            .specialize(batch_size >= 16)
            .parallel(n, 2);
    }
};

HALIDE_REGISTER_GENERATOR(CostModel, halide_autoscheduler_cost_model);
