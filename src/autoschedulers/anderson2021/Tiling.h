#ifndef TILING_H
#define TILING_H

#include <cstdint>
#include <vector>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

bool all_ones(const std::vector<int64_t> &nums);

bool equal_to_existing_size(const std::vector<int64_t> &s,
                            const std::vector<int64_t> &nums);

std::vector<std::vector<int64_t>> generate_serial_tilings(const std::vector<int64_t> &s,
                                                          int d,
                                                          int last_d,
                                                          int vectorized_index,
                                                          const std::vector<int> &vec_dim_serial_sizes,
                                                          bool filter_small_outer_extents = false,
                                                          bool allow_inner_ones = false);

// Given a multi-dimensional box of dimensionality d, generate a list
// of candidate tile sizes for it, logarithmically spacing the sizes
// using the given factor. If 'allow_splits' is false, every dimension
// must either be one, or the full extent of the box. This function is
// used to generate candidate tilings when tiling for
// producer-consumer fusion, or tiling for parallelism.
// inner_sizes is optional vector of fixed sizes to choose from for inner loop.
// used for GPU schedules when we split a 'none' loop into a parallel loop and a serial loop
std::vector<std::vector<int64_t>> generate_tilings(const std::vector<int64_t> &s,
                                                   int d,
                                                   int factor,
                                                   bool allow_splits,
                                                   const std::vector<int> &inner_sizes = std::vector<int>());

/** moves vectorized dimension first and also removes dimensions with size 1
    to reflect actual thread dimensions when loop nests are lowered **/
void lowered_dims(const std::vector<int64_t> &size,
                  int vector_loop_i,
                  std::vector<int64_t> &lowered_size);

// creates tilings for gpu threads loops.
// Innermost thread loop is always the vectorized dim and its extent is a multiple of 32.
// Other loop extents are sized to be powers of 2 such that total extent is < 1024
// called either when we are creating parallel -> (blocks, threads) loop when computing at root
// OR when we are creating none -> (threads, SIMD) loop when computing at a serial loop
// serial_inner = True when we're generating (thread, serial) tilings, False when generating (block,thread) tilings
// max_s hold max gpu_thread counts of all siblings in each dimension. Used to make sure union of
// thread counts is under 1024 threshold.
std::vector<std::vector<int64_t>> generate_gpu_tilings(const std::vector<std::vector<int64_t>> &stage_sizes,
                                                       const std::vector<std::vector<int>> &pure_dims,
                                                       const std::vector<int64_t> &max_s,
                                                       int d,
                                                       const std::vector<int> &vectorized_indices,
                                                       bool serial_inner,
                                                       bool is_compute_root_stage);

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // TILING_H
