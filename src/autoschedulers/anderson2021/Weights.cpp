#include <fstream>
#include <iostream>
#include <random>

#include "Featurization.h"
#include "HalideBuffer.h"
#include "NetworkSize.h"
#include "Weights.h"

namespace Halide {
namespace Internal {

using Halide::Runtime::Buffer;

constexpr uint32_t kSignature = 0x68776631;

void Weights::randomize(uint32_t seed) {
    std::mt19937 rng(seed);
    // Fill the weights with random values
    for_each_buffer([&rng](Buffer<float> &w) {
        w.for_each_value([&rng](float &f) {
            f = ((float)rng()) / ((float)rng.max()) - 0.5f;
        });
    });
}

/*
    Structure of the .weights file format:

    uint32 signature                    always 0x68776631 ('hwf1')
    uint32 PipelineFeatures::version
    uint32 ScheduleFeatures::version
    uint32 buffer-count
        uint32 dimension-count
            uint32x(dimension-count) dimension-extent
            float32x(element-count)  data

    (all values little-endian)
*/

bool Weights::load(std::istream &i) {
    uint32_t signature;
    i.read((char *)&signature, sizeof(signature));
    if (i.fail() || signature != kSignature) return false;

    i.read((char *)&pipeline_features_version, sizeof(pipeline_features_version));
    if (i.fail()) return false;

    i.read((char *)&schedule_features_version, sizeof(schedule_features_version));
    if (i.fail()) return false;

    uint32_t buffer_count;
    i.read((char *)&buffer_count, sizeof(buffer_count));
    if (i.fail() || buffer_count != 6) return false;

    const auto load_one = [&i](Buffer<float> &buf) -> bool {
        uint32_t dimension_count;
        i.read((char *)&dimension_count, sizeof(dimension_count));
        if (i.fail() || dimension_count != (uint32_t)buf.dimensions()) return false;
        for (uint32_t d = 0; d < dimension_count; d++) {
            uint32_t extent;
            i.read((char *)&extent, sizeof(extent));
            if (i.fail() || (int)extent != (int)buf.extent(d)) return false;
        }
        i.read((char *)(buf.data()), buf.size_in_bytes());
        if (i.fail()) return false;
        return true;
    };

    if (!load_one(head1_filter)) return false;
    if (!load_one(head1_bias)) return false;
    if (!load_one(head2_filter)) return false;
    if (!load_one(head2_bias)) return false;
    if (!load_one(conv1_filter)) return false;
    if (!load_one(conv1_bias)) return false;

    return true;
}
bool Weights::load_from_file(const std::string &filename) {
    std::ifstream i(filename, std::ios_base::binary);
    return load(i);
}

bool Weights::save(std::ostream &o) const {
    const uint32_t signature = kSignature;
    o.write((const char *)&signature, sizeof(signature));
    if (o.fail()) return false;

    o.write((const char *)&pipeline_features_version, sizeof(pipeline_features_version));
    if (o.fail()) return false;

    o.write((const char *)&schedule_features_version, sizeof(schedule_features_version));
    if (o.fail()) return false;

    const uint32_t buffer_count = 6;
    o.write((const char *)&buffer_count, sizeof(buffer_count));
    if (o.fail()) return false;

    const auto save_one = [&o](const Buffer<float> &buf) -> bool {
        const uint32_t dimension_count = buf.dimensions();
        o.write((const char *)&dimension_count, sizeof(dimension_count));
        if (o.fail()) return false;
        for (uint32_t d = 0; d < dimension_count; d++) {
            uint32_t extent = buf.extent(d);
            o.write((const char *)&extent, sizeof(extent));
            if (o.fail()) return false;
        }
        o.write((const char *)(buf.data()), buf.size_in_bytes());
        if (o.fail()) return false;
        return true;
    };

    if (!save_one(head1_filter)) return false;
    if (!save_one(head1_bias)) return false;
    if (!save_one(head2_filter)) return false;
    if (!save_one(head2_bias)) return false;
    if (!save_one(conv1_filter)) return false;
    if (!save_one(conv1_bias)) return false;

    return true;
}

bool Weights::save_to_file(const std::string &filename) const {
    std::ofstream o(filename, std::ios_base::trunc | std::ios_base::binary);
    return save(o);
}

bool Weights::load_from_dir(const std::string &dir) {
    const auto buffer_from_file = [](const std::string &filename, Buffer<float> &buf) -> bool {
        std::ifstream i(filename, std::ios_base::binary);
        i.read((char *)(buf.data()), buf.size_in_bytes());
        i.close();
        if (i.fail()) return false;
        return true;
    };

    if (!buffer_from_file(dir + "/head1_conv1_weight.data", head1_filter)) return false;
    if (!buffer_from_file(dir + "/head1_conv1_bias.data", head1_bias)) return false;
    if (!buffer_from_file(dir + "/head2_conv1_weight.data", head2_filter)) return false;
    if (!buffer_from_file(dir + "/head2_conv1_bias.data", head2_bias)) return false;
    if (!buffer_from_file(dir + "/trunk_conv1_weight.data", conv1_filter)) return false;
    if (!buffer_from_file(dir + "/trunk_conv1_bias.data", conv1_bias)) return false;

    // Old style data doesn't record the versions, so just assume they are current
    pipeline_features_version = PipelineFeatures::version();
    schedule_features_version = ScheduleFeatures::version();

    return true;
}

bool Weights::save_to_dir(const std::string &dir) const {
    const auto buffer_to_file = [](const Buffer<float> &buf, const std::string &filename) -> bool {
        std::ofstream o(filename, std::ios_base::trunc | std::ios_base::binary);
        o.write((const char *)(buf.data()), buf.size_in_bytes());
        o.close();
        if (o.fail()) return false;
        return true;
    };

    if (!buffer_to_file(head1_filter, dir + "/head1_conv1_weight.data")) return false;
    if (!buffer_to_file(head1_bias, dir + "/head1_conv1_bias.data")) return false;
    if (!buffer_to_file(head2_filter, dir + "/head2_conv1_weight.data")) return false;
    if (!buffer_to_file(head2_bias, dir + "/head2_conv1_bias.data")) return false;
    if (!buffer_to_file(conv1_filter, dir + "/trunk_conv1_weight.data")) return false;
    if (!buffer_to_file(conv1_bias, dir + "/trunk_conv1_bias.data")) return false;
    return true;
}

}  // namespace Internal
}  // namespace Halide
