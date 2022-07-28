#ifndef HALIDE_NETWORK_SIZE_H
#define HALIDE_NETWORK_SIZE_H

namespace Halide {
// The size of the best cost model network found. Needed by the cost
// model and also the cost model training script.
const int head1_channels = 8, head1_w = 40, head1_h = 7;
const int head2_channels = 24, head2_w = 73;
const int conv1_channels = 32;  // Only 30 are used (needs to be a multiple of 8 for vectorization in cost_model_generator.cpp)
}  // namespace Halide

#endif  // HALIDE_NETWORK_SIZE_H
