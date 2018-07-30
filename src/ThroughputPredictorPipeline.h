#include <algorithm>

#include "Buffer.h"
#include "Generator.h"
#include "BoundaryConditions.h"
#include "ThroughputPredictorLoader.h"
#include "Type.h"
#include "../tools/halide_benchmark.h"

namespace Halide {
namespace Internal {
namespace AutoScheduleModel {

class ThroughputPredictorPipeline {
public:

    ImageParam pipeline_features{Float(32), 4, "pipeline_features"};
    ImageParam schedule_features{Float(32), 3, "schedule_features"};

    Stats feature_stats;

    Buffer<float> schedule_std;
    Buffer<float> schedule_mean;

    Buffer<float> head1_filter;
    Buffer<float> head1_bias;

    Buffer<float> head2_filter;
    Buffer<float> head2_bias;

    Buffer<float> filter1;
    Buffer<float> bias1;
    Buffer<float> filter2;
    Buffer<float> bias2;
    Buffer<float> filter3;
    Buffer<float> bias3;
    Buffer<float> filter4;
    Buffer<float> bias4;
    Buffer<float> filter5;
    Buffer<float> bias5;
    Buffer<float> filter6;
    Buffer<float> bias6;

    Func f_head1_conv{"f_head1_conv"}, f_head2_conv{"f_head2_conv"};
    Func f_head1_relu{"f_head1_relu"}, f_head2_relu{"f_head2_relu"};
    Func f_head1_relu_padded{"f_head1_relu_padded"}, f_head2_relu_padded{"f_head2_relu_padded"};

    Func f_conv1_stage1{"f_conv1_stage1"}, f_conv1_stage2{"f_conv1_stage2"};
    Func f_conv2{"f_conv2"}, f_conv3{"f_conv3"}, f_conv4{"f_conv4"}, f_conv5{"f_conv5"}, f_conv6{"f_conv6"};
    Func f_ReLU1{"f_ReLU1"}, f_relu1_padded{"f_relu1_padded"};
    Func f_ReLU2{"f_ReLU2"}, f_relu2_padded{"f_relu2_padded"};
    Func f_ReLU3{"f_ReLU3"}, f_relu3_padded{"f_relu3_padded"};
    Func f_ReLU4{"f_ReLU4"}, f_relu4_padded{"f_relu4_padded"};
    Func f_ReLU5{"f_ReLU5"}, f_relu5_padded{"f_relu5_padded"};
    Func f_ReLU6{"f_ReLU6"}, f_relu6_padded{"f_relu6_padded"};
    Func f_pool3{"f_pool3"}, f_pool3_padded{"f_pool3_padded"};
    Func f_pool4{"f_pool4"}, f_pool4_padded{"f_pool4_padded"};
    Func f_reduce{"f_reduce"}, prediction{"prediction"};

    ThroughputPredictorPipeline(Weights weights, Stats stats) :
        feature_stats(stats),
        head1_filter(weights.head1_filter), head1_bias(weights.head1_bias),
        head2_filter(weights.head2_filter), head2_bias(weights.head2_bias),
        filter1(weights.conv1_filter), bias1(weights.conv1_bias),
        filter2(weights.conv2_filter), bias2(weights.conv2_bias),
        filter3(weights.conv3_filter), bias3(weights.conv3_bias),
        filter4(weights.conv4_filter), bias4(weights.conv4_bias),
        filter5(weights.conv5_filter), bias5(weights.conv5_bias),
        filter6(weights.conv6_filter), bias6(weights.conv6_bias) {


        Var c("c"), w("w"), n("n");

        RDom r_head1(head1_filter.dim(1).min(), head1_filter.dim(1).extent(),
                     head1_filter.dim(2).min(), head1_filter.dim(2).extent());

        RDom r_head2(head2_filter.dim(1).min(), head2_filter.dim(1).extent());

        RDom r1_stage1(head1_filter.dim(0).min(), head1_filter.dim(0).extent(),
                       filter1.dim(2).min(), filter1.dim(2).extent());

        RDom r1_stage2(head2_filter.dim(0).min(), head2_filter.dim(0).extent(),
                       filter1.dim(2).min(), filter1.dim(2).extent());

        RDom r2(filter2.dim(1).min(), filter2.dim(1).extent(),
                filter2.dim(2).min(), filter2.dim(2).extent());

        RDom r3(filter3.dim(1).min(), filter3.dim(1).extent(),
                filter3.dim(2).min(), filter3.dim(2).extent());

        RDom r4(filter4.dim(1).min(), filter4.dim(1).extent(),
                filter4.dim(2).min(), filter4.dim(2).extent());

        RDom r5(filter5.dim(1).min(), filter5.dim(1).extent(),
                filter5.dim(2).min(), filter5.dim(2).extent());

        RDom r6(filter6.dim(0).min(), filter6.dim(0).extent());

        // reduce over a region that expands to 3x1 convs from the first two stages to the last two stages with zero padding
        RDom r_reduce(0, ( schedule_features.dim(2).extent() + 6 ) / 4);

        f_head1_conv(n, c, w) = head1_bias(c);
        f_head1_conv(n, c, w) += head1_filter(c, r_head1.x, r_head1.y) * pipeline_features(n, r_head1.x, r_head1.y, w);
        f_head1_relu(n, c, w) = max(0, f_head1_conv(n, c, w));

        f_head2_conv(n, c, w) = head2_bias(c);
        f_head2_conv(n, c, w) += head2_filter(c, r_head2) * schedule_features(n, r_head2, w);
        f_head2_relu(n, c, w) = max(0, f_head2_conv(n, c, w));

        // we want to enforce boundary conditions on f_head1_relu and f_head2_relu because conv1 pads with 1 zero on either
        // side of the width (i.e. final dimension) of its input. We set the valid range of the width from 0 to num_stages.
        f_head1_relu_padded = Halide::BoundaryConditions::constant_exterior(f_head1_relu, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, schedule_features.dim(2).extent()}});
        f_head2_relu_padded = Halide::BoundaryConditions::constant_exterior(f_head2_relu, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, schedule_features.dim(2).extent()}});

        /***** network trunk *****/
        // first 20 input channels are from head1_relu, next 20 input channels are from head2_relu
        // have to do two stagees for conv1 to convolve over each head's outputs
        f_conv1_stage1(n, c, w) = bias1(c);
        f_conv1_stage1(n, c, w) += filter1(c, r1_stage1.x, r1_stage1.y) * f_head1_relu_padded(n, r1_stage1.x, w + r1_stage1.y-1);

        f_conv1_stage2(n, c, w) = f_conv1_stage1(n, c, w);
        f_conv1_stage2(n, c, w) += filter1(c, head1_filter.dim(0).extent()+r1_stage2.x, r1_stage2.y) * f_head2_relu_padded(n, r1_stage2.x, w+r1_stage2.y-1);
        f_ReLU1(n, c, w) = max(0, f_conv1_stage2(n, c, w));

        // set boundary conditions for f_ReLU1
        f_relu1_padded = Halide::BoundaryConditions::constant_exterior(f_ReLU1, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, schedule_features.dim(2).extent()}});

        f_conv2(n, c, w) = bias2(c);
        f_conv2(n, c, w) += filter2(c, r2.x, r2.y) * f_relu1_padded(n, r2.x, w+r2.y-1);

        f_ReLU2(n, c, w) = max(0, f_conv2(n, c, w));

        // set boundary conditions for f_ReLU2
        f_relu2_padded = Halide::BoundaryConditions::constant_exterior(f_ReLU2, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, schedule_features.dim(2).extent()}});

        f_conv3(n, c, w) = bias3(c);
        f_conv3(n, c, w) += filter3(c, r3.x, r3.y) * f_relu2_padded(n, r3.x, w+r3.y-1);
        f_ReLU3(n, c, w) = max(0, f_conv3(n, c, w));

        // set boundary conditions for f_ReLU3
        f_relu3_padded = Halide::BoundaryConditions::constant_exterior(f_ReLU3, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, schedule_features.dim(2).extent()}});

        f_pool3(n, c, w) = 0.5f * (f_relu3_padded(n, c, w*2-1) + f_relu3_padded(n, c, w*2));

        // set boundary conditions for f_pool3
        f_pool3_padded = Halide::BoundaryConditions::constant_exterior(f_pool3, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, schedule_features.dim(2).extent()/2 + 1}});

        f_conv4(n, c, w) = bias4(c);
        f_conv4(n, c, w) += filter4(c, r4.x, r4.y) * f_pool3_padded(n, r4.x, w+r4.y-1);
        f_ReLU4(n, c, w) = max(0, f_conv4(n, c, w));

        // set boundary conditions for f_ReLU4
        f_relu4_padded = Halide::BoundaryConditions::constant_exterior(f_ReLU4, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, schedule_features.dim(2).extent()/2 + 1}});

        f_pool4(n, c, w) = 0.5f * (f_relu4_padded(n, c, w*2-1) + f_relu4_padded(n, c, w*2));

        // set boundary conditions for f_pool4
        f_pool4_padded = Halide::BoundaryConditions::constant_exterior(f_pool4, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, (schedule_features.dim(2).extent()+6) / 4}});

        f_conv5(n, c, w) = bias5(c);
        f_conv5(n, c, w) += filter5(c, r5.x, r5.y) * f_pool4_padded(n, r5.x, w+r5.y-1);
        f_ReLU5(n, c, w) = max(0, f_conv5(n, c, w));

        // set boundary conditions for f_ReLU5
        f_relu5_padded = Halide::BoundaryConditions::constant_exterior(f_ReLU5, 0.0f, {{Expr(), Expr()}, {Expr(), Expr()}, {0, (schedule_features.dim(2).extent()+6) / 4}});

        f_conv6(n, w) = bias6();
        f_conv6(n, w) += filter6(r6) * f_relu5_padded(n, r6, w);
        f_ReLU6(n, w) = max(0, f_conv6(n, w));

        // set boundary conditions for f_ReLU5
        f_relu6_padded = Halide::BoundaryConditions::constant_exterior(f_ReLU6, 0.0f, {{Expr(), Expr()}, {0, (schedule_features.dim(2).extent()+6) / 4}});

        f_reduce(n) = 0.0f;
        f_reduce(n) += f_relu6_padded(n, r_reduce);

        prediction(n) = f_reduce(n);


        // schedule
        Expr batch_size = prediction.output_buffers()[0].dim(0).extent();
        prediction.bound(n, 0, batch_size);
        // pipeline_features.dim(0).set_bounds(0, batch_size);
        // schedule_features.dim(0).set_bounds(0, batch_size);
        Expr pipeline_length = schedule_features.dim(2).extent();
        pipeline_features.dim(3).set_bounds(0, pipeline_length);
        schedule_features.dim(2).set_bounds(0, pipeline_length);

        Target t = get_jit_target_from_environment();
        f_head1_relu.compute_at(prediction, n).specialize(batch_size >= 8).vectorize(n, 8);
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
            .specialize(batch_size >= 8).vectorize(n, 8)
            .specialize(batch_size >= 16).parallel(n, 2);

        prediction.compile_jit(t);
    }

    void benchmark() {
        const int batch_size = 800;
        Buffer<float> pipeline_feats(batch_size, 56, 7, 20), schedule_feats(batch_size, 18, 20);
        pipeline_feats.fill(0.0f);
        schedule_feats.fill(0.0f);
        set_inputs(pipeline_feats, schedule_feats);
        Buffer<float> out(batch_size);
        auto t = Halide::Tools::benchmark([&]() {prediction.realize(out);});
        debug(0) << "Throughput predictor runtime: " << ((t/batch_size) * 1000000) << " us\n";
    }

    void set_inputs(Buffer<float> pipeline_feats, Buffer<float> schedule_feats) {
        pipeline_features.set(Buffer<float>(*pipeline_feats.raw_buffer()));
        schedule_features.set(Buffer<float>(*schedule_feats.raw_buffer()));
    }


    Buffer<float> pipeline_feat_queue, schedule_feat_queue;
    std::vector<double *> cost_queue;
    int cursor;
    int enqueue(int padded_stages, Buffer<float> *pipeline_feats, Buffer<float> *schedule_feats, double *cost) {
        const int batch_size = 1024;
        if (!pipeline_feat_queue.defined() || pipeline_feat_queue.dim(3).extent() != padded_stages) {
            pipeline_feat_queue = Buffer<float>(batch_size, 56, 7, padded_stages);
            schedule_feat_queue = Buffer<float>(batch_size, 18, padded_stages);
            pipeline_feat_queue.fill(0.0f);
            schedule_feat_queue.fill(0.0f);
            cost_queue.clear();
            cost_queue.resize(batch_size, nullptr);
            cursor = 0;
        }

        if (cursor == batch_size) {
            evaluate_costs();
        }

        *pipeline_feats = pipeline_feat_queue;
        *schedule_feats = schedule_feat_queue;

        cost_queue[cursor] = cost;
        return cursor++;
    }

    void evaluate_costs() {
        if (cursor == 0) return;

        set_inputs(pipeline_feat_queue, schedule_feat_queue);
        Buffer<float> costs = prediction.realize(cursor);

        for (int i = 0; i < cursor; i++) {
            internal_assert(cost_queue[i]) << "Cost queue entry was null: " << i << "\n";
            *(cost_queue[i]) = costs(i);
        }

        pipeline_feat_queue.fill(0.0f);
        schedule_feat_queue.fill(0.0f);
        std::fill(cost_queue.begin(), cost_queue.end(), nullptr);
        cursor = 0;
    }

};

}
}
}
