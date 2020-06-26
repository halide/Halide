#ifndef TILING_H
#define TILING_H

#include <stdint.h>
#include <vector>

using std::vector;

namespace Halide {
namespace Internal {
namespace Autoscheduler {

bool all_ones(const std::vector<int64_t>& nums);

bool equal_to_existing_size(const std::vector<int64_t>& s, const std::vector<int64_t>& nums);

vector<vector<int64_t>> generate_serial_tilings(const vector<int64_t> &s, int d,
                                                int last_d,
                                                int vectorized_index,
                                                const vector<int> &vec_dim_serial_sizes,
                                                bool filter_small_outer_extents=false,
                                                bool allow_inner_ones=false);

// Given a multi-dimensional box of dimensionality d, generate a list
// of candidate tile sizes for it, logarithmically spacing the sizes
// using the given factor. If 'allow_splits' is false, every dimension
// must either be one, or the full extent of the box. This function is
// used to generate candidate tilings when tiling for
// producer-consumer fusion, or tiling for parallelism.
// inner_sizes is optional vector of fixed sizes to choose from for inner loop.
// used for GPU schedules when we split a 'none' loop into a parallel loop and a serial loop
vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, int factor,
                                         bool allow_splits,
                                         const vector<int> &inner_sizes = vector<int>());

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // TILING_H
