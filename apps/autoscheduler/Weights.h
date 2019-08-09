#include <cstdint>
#include <iostream>
#include <string>

#include "HalideBuffer.h"

namespace Halide {
namespace Internal {


struct Weights {
    Halide::Runtime::Buffer<float> head1_filter;
    Halide::Runtime::Buffer<float> head1_bias;

    Halide::Runtime::Buffer<float> head2_filter;
    Halide::Runtime::Buffer<float> head2_bias;

    Halide::Runtime::Buffer<float> conv1_filter;
    Halide::Runtime::Buffer<float> conv1_bias;

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

