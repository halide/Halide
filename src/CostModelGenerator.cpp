#include "Halide.h"

using namespace Halide;

class CostModel : public Generator<CostModel> {
public:
    // Inputs
    Input<int> num_stages{ "num_stages" };
    Input<Buffer<float>> pipeline_features{ "pipeline_features", 3 };
    Input<Buffer<float>> schedule_features{ "schedule_features", 3 };

    // Network weights. These are parameters instead of baked-in
    // buffers so that they can be swapped out using an environment
    // variable at runtime.
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

    // Zero pad alone the last dimension of a Func
    Func pad_stages(Func f, Expr stages) {
        std::vector<std::pair<Expr, Expr>> bounds(f.dimensions());
        bounds.back().first = 0;
        bounds.back().second = stages;
        return BoundaryConditions::constant_exterior(f, 0.0f, bounds);
    }

    void generate() {
        Var c("c"), w("w"), n("n"), i("i"), j("j"), s("s");

        // The memory layout of the weights is matrix-style
        for (auto *b : {&filter1, &filter2, &filter3, &filter4,
                    &filter5, &filter6, &head1_filter, &head2_filter,
                    &pipeline_mean, &pipeline_std} ) {
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

        Func head1_conv("head1_conv");
        RDom r_head1(head1_filter.dim(1).min(), head1_filter.dim(1).extent(),
                     head1_filter.dim(2).min(), head1_filter.dim(2).extent());
        head1_conv(c, w) = head1_bias(c);
        head1_conv(c, w) += head1_filter(c, r_head1.x, r_head1.y) * normalized_pipeline_features(r_head1.x, r_head1.y, w);

        Func head1_relu("head1_relu");
        head1_relu(c, w) = max(0, head1_conv(c, w));

        Func head1_relu_padded = pad_stages(head1_relu, padded_stages);

        Func head2_conv("head2_conv");
        RDom r_head2(head2_filter.dim(1).min(), head2_filter.dim(1).extent());
        head2_conv(n, c, w) = head2_bias(c);
        head2_conv(n, c, w) += head2_filter(c, r_head2) * normalized_schedule_features(n, r_head2, w);

        Func head2_relu("head2_relu");
        head2_relu(n, c, w) = max(0, head2_conv(n, c, w));

        Func head2_relu_padded = pad_stages(head2_relu, padded_stages);

        /***** network trunk *****/
        // first 20 input channels are from head1_relu, next 20 input channels are from head2_relu
        // have to do two stagees for conv1 to convolve over each head's outputs
        Func conv1_stage1("conv1_stage1");
        RDom r1_stage1(head1_filter.dim(0).min(), head1_filter.dim(0).extent(),
                       filter1.dim(2).min(), filter1.dim(2).extent());
        conv1_stage1(c, w) = bias1(c);
        conv1_stage1(c, w) += filter1(c, r1_stage1.x, r1_stage1.y) * head1_relu_padded(r1_stage1.x, w + r1_stage1.y - 1);

        Func conv1_stage2("conv1_stage2");
        RDom r1_stage2(head2_filter.dim(0).min(), head2_filter.dim(0).extent(),
                       filter1.dim(2).min(), filter1.dim(2).extent());
        conv1_stage2(n, c, w) = conv1_stage1(c, w);  // Broadcast the processed pipeline features across the batch
        conv1_stage2(n, c, w) += (filter1(c, head1_filter.dim(0).extent() + r1_stage2.x, r1_stage2.y) *
                                  head2_relu_padded(n, r1_stage2.x, w + r1_stage2.y - 1));

        Func relu1("relu1");
        relu1(n, c, w) = max(0, conv1_stage2(n, c, w));

        Func relu1_padded = pad_stages(relu1, padded_stages);

        Func conv2("conv2");
        RDom r2(filter2.dim(1).min(), filter2.dim(1).extent(),
                filter2.dim(2).min(), filter2.dim(2).extent());
        conv2(n, c, w) = bias2(c);
        conv2(n, c, w) += filter2(c, r2.x, r2.y) * relu1_padded(n, r2.x, w + r2.y - 1);

        Func relu2("relu2");
        relu2(n, c, w) = max(0, conv2(n, c, w));

        // set boundary conditions for relu2
        Func relu2_padded = pad_stages(relu2, padded_stages);

        Func conv3("conv3");
        RDom r3(filter3.dim(1).min(), filter3.dim(1).extent(),
                filter3.dim(2).min(), filter3.dim(2).extent());
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
        RDom r4(filter4.dim(1).min(), filter4.dim(1).extent(),
                filter4.dim(2).min(), filter4.dim(2).extent());
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
        RDom r5(filter5.dim(1).min(), filter5.dim(1).extent(),
                filter5.dim(2).min(), filter5.dim(2).extent());
        conv5(n, c, w) = bias5(c);
        conv5(n, c, w) += filter5(c, r5.x, r5.y) * pool4_padded(n, r5.x, w + r5.y - 1);

        Func relu5("relu5");
        relu5(n, c, w) = max(0, conv5(n, c, w));

        // set boundary conditions for relu5
        Func relu5_padded = pad_stages(relu5, (padded_stages + 6) / 4);

        Func conv6("conv6");
        RDom r6(filter6.dim(0).min(), filter6.dim(0).extent());
        conv6(n, w) = bias6();
        conv6(n, w) += filter6(r6) * relu5_padded(n, r6, w);

        Func relu6("relu6");
        relu6(n, w) = max(0, conv6(n, w));

        // reduce over a region that expands to 3x1 convs from the first two stages to the last two stages with zero padding
        RDom r_reduce(0, (padded_stages + 6) / 4);
        prediction(n) = sum(relu6(n, r_reduce));

        // schedule
        Expr batch_size = prediction.dim(0).extent();
        prediction.bound(n, 0, batch_size);

        const int vec = natural_vector_size<float>();

        // Pipeline features processing
        normalized_pipeline_features.compute_root()
            .vectorize(i, vec).update().vectorize(i, vec);
        head1_relu.compute_root().vectorize(c, vec);
        conv1_stage1.compute_root().vectorize(c, vec);

        // Schedule features processing
        for (Func f : {normalized_schedule_features, head2_relu, relu1, relu2, relu3, pool3, relu4, pool4, relu5, relu6}) {
            f.compute_at(prediction, n).specialize(batch_size >= vec).vectorize(n, vec);
        }

        prediction.compute_root()
            .specialize(batch_size >= vec)
            .vectorize(n, vec)
            .specialize(batch_size >= 2*vec)
            .parallel(n, 2);
    }
};

HALIDE_REGISTER_GENERATOR(CostModel, halide_autoscheduler_cost_model);
