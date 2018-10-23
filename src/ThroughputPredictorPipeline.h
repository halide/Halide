#include <algorithm>
#include <fstream>

#include "Buffer.h"
#include "Generator.h"
#include "BoundaryConditions.h"
#include "Type.h"

extern "C" int halide_autoscheduler_cost_model(int,
                                               int,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *,
                                               halide_buffer_t *);

extern "C" float halide_internal_weights_pipeline_mean[];
extern "C" int halide_internal_weights_pipeline_mean_length;
extern "C" float halide_internal_weights_pipeline_std[];
extern "C" int halide_internal_weights_pipeline_std_length;
extern "C" float halide_internal_weights_schedule_mean[];
extern "C" int halide_internal_weights_schedule_mean_length;
extern "C" float halide_internal_weights_schedule_std[];
extern "C" int halide_internal_weights_schedule_std_length;

extern "C" float halide_internal_weights_head1_conv1_bias[];
extern "C" int halide_internal_weights_head1_conv1_bias_length;
extern "C" float halide_internal_weights_head1_conv1_weight[];
extern "C" int halide_internal_weights_head1_conv1_weight_length;
extern "C" float halide_internal_weights_head2_conv1_bias[];
extern "C" int halide_internal_weights_head2_conv1_bias_length;
extern "C" float halide_internal_weights_head2_conv1_weight[];
extern "C" int halide_internal_weights_head2_conv1_weight_length;
extern "C" float halide_internal_weights_trunk_conv1_bias[];
extern "C" int halide_internal_weights_trunk_conv1_bias_length;
extern "C" float halide_internal_weights_trunk_conv1_weight[];
extern "C" int halide_internal_weights_trunk_conv1_weight_length;
extern "C" float halide_internal_weights_trunk_conv2_bias[];
extern "C" int halide_internal_weights_trunk_conv2_bias_length;
extern "C" float halide_internal_weights_trunk_conv2_weight[];
extern "C" int halide_internal_weights_trunk_conv2_weight_length;
extern "C" float halide_internal_weights_trunk_conv3_bias[];
extern "C" int halide_internal_weights_trunk_conv3_bias_length;
extern "C" float halide_internal_weights_trunk_conv3_weight[];
extern "C" int halide_internal_weights_trunk_conv3_weight_length;
extern "C" float halide_internal_weights_trunk_conv4_bias[];
extern "C" int halide_internal_weights_trunk_conv4_bias_length;
extern "C" float halide_internal_weights_trunk_conv4_weight[];
extern "C" int halide_internal_weights_trunk_conv4_weight_length;
extern "C" float halide_internal_weights_trunk_conv5_bias[];
extern "C" int halide_internal_weights_trunk_conv5_bias_length;
extern "C" float halide_internal_weights_trunk_conv5_weight[];
extern "C" int halide_internal_weights_trunk_conv5_weight_length;
extern "C" float halide_internal_weights_trunk_conv6_bias[];
extern "C" int halide_internal_weights_trunk_conv6_bias_length;
extern "C" float halide_internal_weights_trunk_conv6_weight[];
extern "C" int halide_internal_weights_trunk_conv6_weight_length;


namespace Halide {
namespace Internal {
namespace AutoScheduleModel {

Runtime::Buffer<float> buffer_from_file(const std::string &filename, const std::vector<int> &shape) {
    Runtime::Buffer<float> buf(shape);
    assert_file_exists(filename);

    std::ifstream i(filename.c_str());
    i.read((char *)(buf.data()), buf.size_in_bytes());

    return buf;
}

struct Stats {
    Runtime::Buffer<float> pipeline_mean;
    Runtime::Buffer<float> pipeline_std;
    Runtime::Buffer<float> schedule_mean;
    Runtime::Buffer<float> schedule_std;
};

void load_stats(Stats &stats) {
    std::string stats_dir = get_env_variable("HL_WEIGHTS_DIR");
    if (stats_dir.empty()) {
        stats.pipeline_mean = Runtime::Buffer<float>(halide_internal_weights_pipeline_mean, 7, 56);
        stats.pipeline_mean.transpose(0, 1); // Stored as 7x56, but pipeline will access as 56x7
        internal_assert(halide_internal_weights_pipeline_mean_length == (int)stats.pipeline_mean.size_in_bytes());

        stats.pipeline_std = Runtime::Buffer<float>(halide_internal_weights_pipeline_std, 7, 56);
        stats.pipeline_std.transpose(0, 1); // Stored as 7x56, but pipeline will access as 56x7
        internal_assert(halide_internal_weights_pipeline_std_length == (int)stats.pipeline_std.size_in_bytes());

        stats.schedule_mean = Runtime::Buffer<float>(halide_internal_weights_schedule_mean, 25);
        internal_assert(halide_internal_weights_schedule_mean_length == (int)stats.schedule_mean.size_in_bytes());

        stats.schedule_std = Runtime::Buffer<float>(halide_internal_weights_schedule_std, 25);
        internal_assert(halide_internal_weights_schedule_std_length == (int)stats.schedule_std.size_in_bytes());
    } else {
        stats.pipeline_mean = buffer_from_file(stats_dir + "/pipeline_mean.data", {7, 56});
        stats.pipeline_mean.transpose(0, 1);
        stats.pipeline_std = buffer_from_file(stats_dir + "/pipeline_std.data", {7, 56});
        stats.pipeline_std.transpose(0, 1);
        stats.schedule_mean = buffer_from_file(stats_dir + "/schedule_mean.data", {25});
        stats.schedule_std = buffer_from_file(stats_dir + "/schedule_std.data", {25});
    }
}

struct Weights {
    Runtime::Buffer<float> head1_filter;
    Runtime::Buffer<float> head1_bias;

    Runtime::Buffer<float> head2_filter;
    Runtime::Buffer<float> head2_bias;

    Runtime::Buffer<float> conv1_filter;
    Runtime::Buffer<float> conv1_bias;

    Runtime::Buffer<float> conv2_filter;
    Runtime::Buffer<float> conv2_bias;

    Runtime::Buffer<float> conv3_filter;
    Runtime::Buffer<float> conv3_bias;

    Runtime::Buffer<float> conv4_filter;
    Runtime::Buffer<float> conv4_bias;

    Runtime::Buffer<float> conv5_filter;
    Runtime::Buffer<float> conv5_bias;

    Runtime::Buffer<float> conv6_filter;
    Runtime::Buffer<float> conv6_bias;
};

void load_weights(Weights &weights) {
    std::string weights_dir = get_env_variable("HL_WEIGHTS_DIR");
    if (weights_dir.empty()) {
        weights.head1_filter = Runtime::Buffer<float>(halide_internal_weights_head1_conv1_weight, 7, 56, 20);
        weights.head1_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_head1_conv1_weight_length == (int)weights.head1_filter.size_in_bytes());

        weights.head1_bias = Runtime::Buffer<float>(halide_internal_weights_head1_conv1_bias, 20);
        internal_assert(halide_internal_weights_head1_conv1_bias_length == (int)weights.head1_bias.size_in_bytes());

        weights.head2_filter = Runtime::Buffer<float>(halide_internal_weights_head2_conv1_weight, 25, 20);
        weights.head2_filter.transpose(0, 1);
        internal_assert(halide_internal_weights_head2_conv1_weight_length == (int)weights.head2_filter.size_in_bytes());

        weights.head2_bias = Runtime::Buffer<float>(halide_internal_weights_head2_conv1_bias, 20);
        internal_assert(halide_internal_weights_head2_conv1_bias_length == (int)weights.head2_bias.size_in_bytes());

        weights.conv1_filter = Runtime::Buffer<float>(halide_internal_weights_trunk_conv1_weight, 3, 40, 40);
        weights.conv1_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv1_weight_length == (int)weights.conv1_filter.size_in_bytes());

        weights.conv1_bias = Runtime::Buffer<float>(halide_internal_weights_trunk_conv1_bias, 40);
        internal_assert(halide_internal_weights_trunk_conv1_bias_length == (int)weights.conv1_bias.size_in_bytes());

        weights.conv2_filter = Runtime::Buffer<float>(halide_internal_weights_trunk_conv2_weight, 3, 40, 40);
        weights.conv2_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv2_weight_length == (int)weights.conv2_filter.size_in_bytes());

        weights.conv2_bias = Runtime::Buffer<float>(halide_internal_weights_trunk_conv2_bias, 40);
        internal_assert(halide_internal_weights_trunk_conv2_bias_length == (int)weights.conv2_bias.size_in_bytes());

        weights.conv3_filter = Runtime::Buffer<float>(halide_internal_weights_trunk_conv3_weight, 3, 40, 80);
        weights.conv3_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv3_weight_length == (int)weights.conv3_filter.size_in_bytes());

        weights.conv3_bias = Runtime::Buffer<float>(halide_internal_weights_trunk_conv3_bias, 80);
        internal_assert(halide_internal_weights_trunk_conv3_bias_length == (int)weights.conv3_bias.size_in_bytes());

        weights.conv4_filter = Runtime::Buffer<float>(halide_internal_weights_trunk_conv4_weight, 3, 80, 120);
        weights.conv4_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv4_weight_length == (int)weights.conv4_filter.size_in_bytes());

        weights.conv4_bias = Runtime::Buffer<float>(halide_internal_weights_trunk_conv4_bias, 120);
        internal_assert(halide_internal_weights_trunk_conv4_bias_length == (int)weights.conv4_bias.size_in_bytes());

        weights.conv5_filter = Runtime::Buffer<float>(halide_internal_weights_trunk_conv5_weight, 3, 120, 160);
        weights.conv5_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv5_weight_length == (int)weights.conv5_filter.size_in_bytes());

        weights.conv5_bias = Runtime::Buffer<float>(halide_internal_weights_trunk_conv5_bias, 160);
        internal_assert(halide_internal_weights_trunk_conv5_bias_length == (int)weights.conv5_bias.size_in_bytes());

        weights.conv6_filter = Runtime::Buffer<float>(halide_internal_weights_trunk_conv6_weight, 160);
        internal_assert(halide_internal_weights_trunk_conv6_weight_length == (int)weights.conv6_filter.size_in_bytes());

        weights.conv6_bias = Runtime::Buffer<float>::make_scalar(halide_internal_weights_trunk_conv6_bias);
        internal_assert(halide_internal_weights_trunk_conv6_bias_length == (int)weights.conv6_bias.size_in_bytes());
    } else {
        weights.head1_filter = buffer_from_file(weights_dir + "/head1_conv1_weight.data", {7, 56, 20});
        weights.head1_filter.transpose(0, 2);

        weights.head1_bias = buffer_from_file(weights_dir + "/head1_conv1_bias.data", {20});

        weights.head2_filter = buffer_from_file(weights_dir + "/head2_conv1_weight.data", {25, 20});
        weights.head2_filter.transpose(0, 1);

        weights.head2_bias = buffer_from_file(weights_dir + "/head2_conv1_bias.data", {20});

        weights.conv1_filter = buffer_from_file(weights_dir + "/trunk_conv1_weight.data", {3, 40, 40});
        weights.conv1_filter.transpose(0, 2);

        weights.conv1_bias = buffer_from_file(weights_dir + "/trunk_conv1_bias.data", {40});

        weights.conv2_filter = buffer_from_file(weights_dir + "/trunk_conv2_weight.data", {3, 40, 40});
        weights.conv2_filter.transpose(0, 2);

        weights.conv2_bias = buffer_from_file(weights_dir + "/trunk_conv2_bias.data", {40});

        weights.conv3_filter = buffer_from_file(weights_dir + "/trunk_conv3_weight.data", {3, 40, 80});
        weights.conv3_filter.transpose(0, 2);

        weights.conv3_bias = buffer_from_file(weights_dir + "/trunk_conv3_bias.data", {80});

        weights.conv4_filter = buffer_from_file(weights_dir + "/trunk_conv4_weight.data", {3, 80, 120});
        weights.conv4_filter.transpose(0, 2);

        weights.conv4_bias = buffer_from_file(weights_dir + "/trunk_conv4_bias.data", {120});

        weights.conv5_filter = buffer_from_file(weights_dir + "/trunk_conv5_weight.data", {3, 120, 160});
        weights.conv5_filter.transpose(0, 2);

        weights.conv5_bias = buffer_from_file(weights_dir + "/trunk_conv5_bias.data", {160});

        weights.conv6_filter = buffer_from_file(weights_dir + "/trunk_conv6_weight.data", {160});

        weights.conv6_bias = buffer_from_file(weights_dir + "/trunk_conv6_bias.data", {});

    }
}

class ThroughputPredictorPipeline {
    Weights weights;
    Stats stats;
    Runtime::Buffer<float> schedule_feat_queue, pipeline_feat_queue, costs;
    Runtime::Buffer<double *> cost_ptrs;
    int cursor, num_stages;
public:

    ThroughputPredictorPipeline() {
        load_weights(weights);
        load_stats(stats);
    }

    void set_pipeline_features(const Runtime::Buffer<float> &pipeline_feats) {
        pipeline_feat_queue = pipeline_feats;
    }

    void enqueue(int ns, Runtime::Buffer<float> *schedule_feats, double *cost_ptr) {
        num_stages = ns;

        // We know the most stages that will ever be enqueued from the schedule features
        internal_assert(pipeline_feat_queue.data()) << "Call set_schedule_features before calling enqueue\n";
        const int max_num_stages = pipeline_feat_queue.dim(2).extent();
        internal_assert(num_stages <= max_num_stages)
            << "schedule features has more stages (" << num_stages
            << ") than pipeline features (" << max_num_stages << ")\n";

        const int batch_size = 1024;
        if (!schedule_feat_queue.data() ||
            schedule_feat_queue.dim(2).extent() < max_num_stages) {
            internal_assert(cursor == 0);
            schedule_feat_queue = Runtime::Buffer<float>(batch_size, 25, max_num_stages);
            if (!costs.data()) {
                internal_assert(!cost_ptrs.data());
                costs = Runtime::Buffer<float>(batch_size);
                cost_ptrs = Runtime::Buffer<double *>(batch_size);
            }
        }

        if (cursor == batch_size) {
            evaluate_costs();
        }

        *schedule_feats = schedule_feat_queue.sliced(0, cursor);
        cost_ptrs(cursor) = cost_ptr;

        cursor++;
    }

    void evaluate_costs() {
        if (cursor == 0 || !schedule_feat_queue.data()) return;

        internal_assert(pipeline_feat_queue.data());
        internal_assert(schedule_feat_queue.data());

        Runtime::Buffer<float> dst = costs.cropped(0, 0, cursor);

        halide_autoscheduler_cost_model(num_stages,
                                        cursor,
                                        pipeline_feat_queue,
                                        schedule_feat_queue,
                                        stats.pipeline_mean,
                                        stats.pipeline_std,
                                        stats.schedule_mean,
                                        stats.schedule_std,
                                        weights.head1_filter, weights.head1_bias,
                                        weights.head2_filter, weights.head2_bias,
                                        weights.conv1_filter, weights.conv1_bias,
                                        weights.conv2_filter, weights.conv2_bias,
                                        weights.conv3_filter, weights.conv3_bias,
                                        weights.conv4_filter, weights.conv4_bias,
                                        weights.conv5_filter, weights.conv5_bias,
                                        weights.conv6_filter, weights.conv6_bias,
                                        dst);

        for (int i = 0; i < cursor; i++) {
            internal_assert(cost_ptrs(i)) << "Cost queue entry was null: " << i << "\n";
            *(cost_ptrs(i)) = dst(i);
        }

        cursor = 0;
    }

    // Discard any enqueued but unevaluated schedules
    void reset() {
        cursor = 0;
    }

};

}
}
}
