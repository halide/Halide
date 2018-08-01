#ifndef HALIDE_INTERNAL_THROUGHPUT_PREDICTOR_LOADER_H
#define HALIDE_INTERNAL_THROUGHPUT_PREDICTOR_LOADER_H

#include "Buffer.h"
#include <iostream>
#include <fstream>
#include <assert.h>

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

Buffer<float> buffer_from_file(const std::string &filename, const std::vector<int> &shape) {
    Buffer<float> buf(shape, filename);
    assert_file_exists(filename);

    std::ifstream i(filename.c_str());
    i.read((char *)(buf.data()), buf.size_in_bytes());

    return buf;
}

struct Stats {
    Buffer<float> pipeline_mean;
    Buffer<float> pipeline_std;
    Buffer<float> schedule_mean;
    Buffer<float> schedule_std;
};

Stats load_stats() {
    Stats stats;

    std::string stats_dir = get_env_variable("HL_WEIGHTS_DIR");
    if (stats_dir.empty()) {

        stats.pipeline_mean = Buffer<float>(halide_internal_weights_pipeline_mean, 7, 56);
        stats.pipeline_mean.transpose(0, 1); // Stored as 7x56, but pipeline will access as 56x7
        internal_assert(halide_internal_weights_pipeline_mean_length == (int)stats.pipeline_mean.size_in_bytes());

        stats.pipeline_std = Buffer<float>(halide_internal_weights_pipeline_std, 7, 56);
        stats.pipeline_std.transpose(0, 1); // Stored as 7x56, but pipeline will access as 56x7
        internal_assert(halide_internal_weights_pipeline_std_length == (int)stats.pipeline_std.size_in_bytes());

        stats.schedule_mean = Buffer<float>(halide_internal_weights_schedule_mean, 18);
        internal_assert(halide_internal_weights_schedule_mean_length == (int)stats.schedule_mean.size_in_bytes());

        stats.schedule_std = Buffer<float>(halide_internal_weights_schedule_std, 18);
        internal_assert(halide_internal_weights_schedule_std_length == (int)stats.schedule_std.size_in_bytes());
    } else {
        stats.pipeline_mean = buffer_from_file(stats_dir + "/pipeline_mean.data", {7, 56});
        stats.pipeline_mean.transpose(0, 1);
        stats.pipeline_std = buffer_from_file(stats_dir + "/pipeline_std.data", {7, 56});
        stats.pipeline_std.transpose(0, 1);
        stats.schedule_mean = buffer_from_file(stats_dir + "/schedule_mean.data", {18});
        stats.schedule_std = buffer_from_file(stats_dir + "/schedule_std.data", {18});
    }
    return stats;
}

struct WeightShapes {
    int head1_filter[3] = {20, 56, 7};
    int head1_bias[1] = {20};

    int head2_filter[2] = {20, 18};
    int head2_bias[1] = {20};

    int conv1_filter[3] = {40, 40, 3};
    int conv1_bias[1] = {40};

    int conv2_filter[3] = {40, 40, 3};
    int conv2_bias[1] = {40};

    int conv3_filter[3] = {80, 40, 3};
    int conv3_bias[1] = {80};

    int conv4_filter[3] = {120, 80, 3};
    int conv4_bias[1] = {120};

    int conv5_filter[3] = {160, 120, 3};
    int conv5_bias[1] = {160};

    int conv6_filter[1] = {160};
    int conv6_bias[1] = {1};
};

struct Weights {
    Buffer<float> head1_filter;
    Buffer<float> head1_bias;

    Buffer<float> head2_filter;
    Buffer<float> head2_bias;

    Buffer<float> conv1_filter;
    Buffer<float> conv1_bias;

    Buffer<float> conv2_filter;
    Buffer<float> conv2_bias;

    Buffer<float> conv3_filter;
    Buffer<float> conv3_bias;

    Buffer<float> conv4_filter;
    Buffer<float> conv4_bias;

    Buffer<float> conv5_filter;
    Buffer<float> conv5_bias;

    Buffer<float> conv6_filter;
    Buffer<float> conv6_bias;
};

Weights load_weights() {
    Weights weights;

    std::string weights_dir = get_env_variable("HL_WEIGHTS_DIR");
    if (weights_dir.empty()) {
        weights.head1_filter = Buffer<float>(halide_internal_weights_head1_conv1_weight, 7, 56, 20);
        weights.head1_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_head1_conv1_weight_length == (int)weights.head1_filter.size_in_bytes());

        weights.head1_bias = Buffer<float>(halide_internal_weights_head1_conv1_bias, 20);
        internal_assert(halide_internal_weights_head1_conv1_bias_length == (int)weights.head1_bias.size_in_bytes());

        weights.head2_filter = Buffer<float>(halide_internal_weights_head2_conv1_weight, 18, 20);
        weights.head2_filter.transpose(0, 1);
        internal_assert(halide_internal_weights_head2_conv1_weight_length == (int)weights.head2_filter.size_in_bytes());

        weights.head2_bias = Buffer<float>(halide_internal_weights_head2_conv1_bias, 20);
        internal_assert(halide_internal_weights_head2_conv1_bias_length == (int)weights.head2_bias.size_in_bytes());

        weights.conv1_filter = Buffer<float>(halide_internal_weights_trunk_conv1_weight, 3, 40, 40);
        weights.conv1_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv1_weight_length == (int)weights.conv1_filter.size_in_bytes());

        weights.conv1_bias = Buffer<float>(halide_internal_weights_trunk_conv1_bias, 40);
        internal_assert(halide_internal_weights_trunk_conv1_bias_length == (int)weights.conv1_bias.size_in_bytes());

        weights.conv2_filter = Buffer<float>(halide_internal_weights_trunk_conv2_weight, 3, 40, 40);
        weights.conv2_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv2_weight_length == (int)weights.conv2_filter.size_in_bytes());

        weights.conv2_bias = Buffer<float>(halide_internal_weights_trunk_conv2_bias, 40);
        internal_assert(halide_internal_weights_trunk_conv2_bias_length == (int)weights.conv2_bias.size_in_bytes());

        weights.conv3_filter = Buffer<float>(halide_internal_weights_trunk_conv3_weight, 3, 40, 80);
        weights.conv3_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv3_weight_length == (int)weights.conv3_filter.size_in_bytes());

        weights.conv3_bias = Buffer<float>(halide_internal_weights_trunk_conv3_bias, 80);
        internal_assert(halide_internal_weights_trunk_conv3_bias_length == (int)weights.conv3_bias.size_in_bytes());

        weights.conv4_filter = Buffer<float>(halide_internal_weights_trunk_conv4_weight, 3, 80, 120);
        weights.conv4_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv4_weight_length == (int)weights.conv4_filter.size_in_bytes());

        weights.conv4_bias = Buffer<float>(halide_internal_weights_trunk_conv4_bias, 120);
        internal_assert(halide_internal_weights_trunk_conv4_bias_length == (int)weights.conv4_bias.size_in_bytes());

        weights.conv5_filter = Buffer<float>(halide_internal_weights_trunk_conv5_weight, 3, 120, 160);
        weights.conv5_filter.transpose(0, 2);
        internal_assert(halide_internal_weights_trunk_conv5_weight_length == (int)weights.conv5_filter.size_in_bytes());

        weights.conv5_bias = Buffer<float>(halide_internal_weights_trunk_conv5_bias, 160);
        internal_assert(halide_internal_weights_trunk_conv5_bias_length == (int)weights.conv5_bias.size_in_bytes());

        weights.conv6_filter = Buffer<float>(halide_internal_weights_trunk_conv6_weight, 160);
        internal_assert(halide_internal_weights_trunk_conv6_weight_length == (int)weights.conv6_filter.size_in_bytes());

        weights.conv6_bias = Buffer<float>::make_scalar(halide_internal_weights_trunk_conv6_bias);
        internal_assert(halide_internal_weights_trunk_conv6_bias_length == (int)weights.conv6_bias.size_in_bytes());
    } else {
        weights.head1_filter = buffer_from_file(weights_dir + "/head1_conv1_weight.data", {7, 56, 20});
        weights.head1_filter.transpose(0, 2);

        weights.head1_bias = buffer_from_file(weights_dir + "/head1_conv1_bias.data", {20});

        weights.head2_filter = buffer_from_file(weights_dir + "/head2_conv1_weight.data", {18, 20});
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

    return weights;
}

}
}
}

#endif
