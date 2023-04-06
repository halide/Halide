#ifndef _WEIGHTS
#define _WEIGHTS

#include <cstdint>
#include <iostream>
#include <string>

#include "Featurization.h"
#include "HalideBuffer.h"
#include "NetworkSize.h"

namespace Halide {
namespace Internal {

struct Weights {
    uint32_t pipeline_features_version = PipelineFeatures::version();
    uint32_t schedule_features_version = ScheduleFeatures::version();

    Halide::Runtime::Buffer<float> head1_filter{head1_channels, head1_w, head1_h};
    Halide::Runtime::Buffer<float> head1_bias{head1_channels};

    Halide::Runtime::Buffer<float> head2_filter{head2_channels, head2_w};
    Halide::Runtime::Buffer<float> head2_bias{head2_channels};

    Halide::Runtime::Buffer<float> conv1_filter{conv1_channels, head1_channels + head2_channels};
    Halide::Runtime::Buffer<float> conv1_bias{conv1_channels};

    template<typename F>
    void for_each_buffer(F f) {
        f(head1_filter);
        f(head1_bias);
        f(head2_filter);
        f(head2_bias);
        f(conv1_filter);
        f(conv1_bias);
    }

    void randomize(uint32_t seed);

    bool load(std::istream &i);
    bool save(std::ostream &o) const;

    bool load_from_file(const std::string &filename);
    bool save_to_file(const std::string &filename) const;

    // Load/save from the 'classic' form of six raw data files
    bool load_from_dir(const std::string &dir);
    bool save_to_dir(const std::string &dir) const;
};

}  // namespace Internal
}  // namespace Halide

#endif  // _WEIGHTS
