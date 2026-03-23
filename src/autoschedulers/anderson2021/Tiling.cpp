#include "Tiling.h"

#include <functional>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

bool all_ones(const std::vector<int64_t> &nums) {
    for (const auto &n : nums) {
        if (n != 1) {
            return false;
        }
    }
    return true;
}

bool equal_to_existing_size(const std::vector<int64_t> &s,
                            const std::vector<int64_t> &nums) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != nums[i]) {
            return false;
        }
    }
    return true;
}

std::vector<std::vector<int64_t>> generate_serial_tilings(const std::vector<int64_t> &s,
                                                          int d,
                                                          int last_d,
                                                          int vectorized_index,
                                                          const std::vector<int> &vec_dim_serial_sizes,
                                                          bool filter_small_outer_extents,
                                                          bool allow_inner_ones) {
    std::vector<std::vector<int64_t>> result;
    if (d == -1) {
        result.emplace_back();
    } else {
        std::vector<std::vector<int64_t>> v;
        v = generate_serial_tilings(s,
                                    d - 1,
                                    last_d,
                                    vectorized_index,
                                    vec_dim_serial_sizes,
                                    filter_small_outer_extents,
                                    allow_inner_ones);
        for (auto t : v) {
            t.push_back(0);
            bool used_full_extent = false;
            // include odd serial sizes that encourage multiples of 16 as thread tile size
            if (!vec_dim_serial_sizes.empty() && d == vectorized_index) {
                for (int inner : vec_dim_serial_sizes) {
                    int outer = (s[d] + inner - 1) / inner;
                    if (filter_small_outer_extents && outer < 16) {
                        continue;
                    }
                    t.back() = outer;

                    if (d == last_d && (equal_to_existing_size(s, t) || all_ones(t))) {
                        continue;
                    }
                    used_full_extent = inner == s[d];
                    result.push_back(t);
                }
            }

            int max = (s[d] == 3 || s[d] == 5 || s[d] == 7) ? s[d] : 8;
            int factor = (s[d] == 3 || s[d] == 5 || s[d] == 7) ? s[d] : 2;

            // always consider the even tile sizes: 1, 2, 4, 8
            for (int inner = 1; inner <= max; inner *= factor) {
                if (inner > s[d]) {
                    break;
                }
                if (inner == s[d] && used_full_extent) {
                    continue;
                }
                int outer = (s[d] + inner - 1) / inner;
                if (d == vectorized_index && filter_small_outer_extents && outer < 16) {
                    continue;
                }
                t.back() = outer;
                if (d == last_d && ((!allow_inner_ones && equal_to_existing_size(s, t)) || all_ones(t))) {
                    continue;
                }
                result.push_back(t);
            }
        }
    }
    return result;
}

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
                                                   const std::vector<int> &inner_sizes) {
    std::vector<std::vector<int64_t>> result;
    if (d == -1) {
        result.emplace_back();
    } else {
        std::vector<std::vector<int64_t>> v;
        v = generate_tilings(s, d - 1, factor, allow_splits);
        // If we're already generated too many tiling configurations
        // for the inner loops, search the outer loops with coarser
        // granularity.
        while (v.size() > (size_t)factor * 100) {
            factor *= 2;
        }

        for (auto &t : v) {
            bool is_full = false, is_one = false;
            // Skip trivial tilings
            if ((size_t)d == s.size() - 1) {
                is_one = is_full = true;
                for (int i = 0; i < d; i++) {
                    is_one &= (t[i] == 1);
                    is_full &= (t[i] == s[i]);
                }
            }
            t.push_back(0);
            if (!allow_splits) {
                if (!is_one) {
                    t.back() = 1;
                    result.push_back(t);
                }
                if (s[d] != 1 && !is_full) {
                    t.back() = s[d];
                    result.push_back(t);
                }
            } else {
                if (!inner_sizes.empty()) {  // using fixed set of inner loop extents
                    for (int inner : inner_sizes) {
                        int outer = (s[d] + inner - 1) / inner;
                        if (is_one && outer == 1) {
                            continue;
                        }
                        if (is_full && outer == s[d]) {
                            continue;
                        }
                        t.back() = outer;
                        result.push_back(t);
                    }
                } else {
                    int max_inner = 0;
                    for (int inner = 1; inner < s[d]; inner *= factor) {
                        int outer = (s[d] + inner - 1) / inner;
                        if (is_one && outer == 1) {
                            continue;
                        }
                        if (is_full && outer == s[d]) {
                            continue;
                        }
                        // Stop when we hit inner sizes that would do too much recompute
                        if (inner > 1 && inner * outer * 7 > s[d] * 8) {
                            break;
                        }
                        max_inner = inner;
                        t.back() = outer;
                        result.push_back(t);
                    }

                    for (int outer = 1; outer <= s[d]; outer *= factor) {
                        int inner = (s[d] + outer - 1) / outer;
                        if (is_one && outer == 1) {
                            continue;
                        }
                        if (is_full && outer == s[d]) {
                            continue;
                        }
                        // Stop when we get into the regime covered by the loop above.
                        if (outer > 1 && inner < max_inner * 2) {
                            break;
                        }
                        // Or when the wasted compute gets too bad.
                        if (inner * outer * 7 > s[d] * 8) {
                            break;
                        }
                        t.back() = outer;
                        result.push_back(t);
                    }

                    // The sequence above (in terms of the inner loop)
                    // goes 1 2 4 8 16 ...  but 3 is an important inner
                    // tiling factor for matrix multiply/gemm-type loops
                    // which try to use 12 vector registers.
                    int inner3 = 3;
                    int outer3 = (s[d] + inner3 - 1) / inner3;
                    if (factor == 2 && inner3 < s[d] && outer3 < s[d] && outer3 > 1) {
                        if (inner3 * outer3 * 7 <= s[d] * 8) {
                            t.back() = outer3;
                            result.push_back(t);
                        }
                    }
                }
            }
        }
    }
    return result;
}

// Moves vectorized dimension first and also removes dimensions with size 1
// to reflect actual thread dimensions when loop nests are lowered
void lowered_dims(const std::vector<int64_t> &size,
                  int vector_loop_i,
                  std::vector<int64_t> &lowered_size) {
    if (vector_loop_i >= 0 && size[vector_loop_i] > 1) {
        lowered_size.push_back(size[vector_loop_i]);
    }
    for (int dim = 0; dim < (int)(size.size()); dim++) {
        if (dim != vector_loop_i && size[dim] > 1) {
            lowered_size.push_back(size[dim]);
        }
    }
}

// Creates tilings for gpu thread loops.
// Innermost thread loop is always the vectorized dim and its extent is a multiple of 32.
// Other loop extents are sized to be powers of 2 such that total extent is < 1024
// called either when we are creating parallel -> (blocks, threads) loop when computing at root
// OR when we are creating none -> (threads, SIMD) loop when computing at a serial loop
// serial_inner = True when we're generating (thread, serial) tilings, False when generating (block,thread) tilings
// max_s holds max gpu_thread counts across all sibling loop nests in each dimension. Used to
// make sure union of thread counts is under 1024 threshold.
std::vector<std::vector<int64_t>> generate_gpu_tilings(const std::vector<std::vector<int64_t>> &stage_sizes,
                                                       const std::vector<std::vector<int>> &pure_dims,
                                                       const std::vector<int64_t> &max_s,
                                                       int d,
                                                       const std::vector<int> &vectorized_indices,
                                                       bool serial_inner,
                                                       bool is_compute_root_stage) {
    std::vector<std::vector<int64_t>> result;
    if (d == -1) {
        result.emplace_back();
    } else {
        // set max thread count 64 for now in all dims
        int64_t max_threads_extent = 64, total_threads_limit = 1024;  // less than 1024 to limit states
        int factor = 2, innermost_warp_extent = 16, max_serial_ext = 16;

        if (is_compute_root_stage && pure_dims[0].size() == 1) {
            innermost_warp_extent = 1;
        }

        std::vector<std::vector<int64_t>> v;
        v = generate_gpu_tilings(stage_sizes,
                                 pure_dims,
                                 max_s,
                                 d - 1,
                                 vectorized_indices,
                                 serial_inner,
                                 is_compute_root_stage);

        for (auto t : v) {
            enum validity {
                serial_count_err,
                thread_count_err,
                valid_tiling
            };

            // helper function detects whether tiling is legal: cannot exceed max thread count,
            // have more than three dimensions with ext > 1, or result in large serial loops
            std::function<validity()> is_valid_tiling = [&]() {
                if (d == ((int)(stage_sizes[0].size()) - 1)) {
                    std::vector<int64_t> lowered_size, thread_t;
                    thread_t = t;
                    lowered_dims(thread_t, vectorized_indices[0], lowered_size);
                    // see how tiling will be applied to other stages of this func and update max_s accordingly
                    std::vector<int64_t> new_max_s = max_s;
                    for (size_t stage = 0; stage < pure_dims.size(); stage++) {
                        std::vector<int64_t> stage_thread_t, stage_lowered_size;
                        for (int i : pure_dims[stage]) {
                            if (i >= 0) {
                                stage_thread_t.push_back(thread_t[i]);
                            } else {  // impure dims have extent 1
                                stage_thread_t.push_back(1);
                            }
                        }
                        lowered_dims(stage_thread_t, vectorized_indices[stage], stage_lowered_size);
                        // adjust max_size to account for other stages thread counts when we apply this tiling
                        for (size_t dim = 0; dim < stage_lowered_size.size(); dim++) {
                            if (dim >= new_max_s.size()) {
                                new_max_s.push_back(stage_lowered_size[dim]);
                            } else {
                                new_max_s[dim] = std::max(new_max_s[dim], stage_lowered_size[dim]);
                            }
                        }
                    }
                    int64_t union_threads;
                    int64_t total_threads_used = 1, not_ext1 = 0;
                    int max_dim = std::max((int)(new_max_s.size()), (int)(lowered_size.size()));
                    for (int dim = 0; dim < max_dim; dim++) {
                        if (dim >= (int)(new_max_s.size())) {
                            union_threads = lowered_size[dim];
                        } else if (dim >= (int)(lowered_size.size())) {
                            union_threads = new_max_s[dim];
                        } else {
                            union_threads = std::max(lowered_size[dim], new_max_s[dim]);
                        }
                        not_ext1 = not_ext1 + ((union_threads > 1) ? 1 : 0);
                        total_threads_used *= union_threads;
                    }
                    if (total_threads_used > total_threads_limit || not_ext1 > 3) {
                        return thread_count_err;
                    }
                    if (serial_inner) {
                        for (int dd = 0; dd < (int)(stage_sizes[0].size()); dd++) {
                            int64_t other_ext = (stage_sizes[0][dd] + t[dd] - 1) / t[dd];
                            if (other_ext > max_serial_ext) {
                                return serial_count_err;
                            }
                        }
                    }
                }
                return valid_tiling;
            };

            t.push_back(0);

            // if the vector dimension has extent < innermost_warp_extent we use 1 warp for it
            int64_t min_threads = (d == vectorized_indices[0]) ? innermost_warp_extent : 1;
            bool full_extent_considered = false;

            for (int64_t threads_ext = min_threads; threads_ext <= max_threads_extent; threads_ext *= factor) {
                full_extent_considered |= threads_ext == stage_sizes[0][d];
                if (threads_ext > stage_sizes[0][d]) {
                    break;
                }
                // reject if inner exceeds hardware thread limit
                if ((d == vectorized_indices[0] && threads_ext > max_threads_extent) ||
                    (d != vectorized_indices[0] && threads_ext > 16)) {
                    break;
                }
                int64_t other_ext = (stage_sizes[0][d] + threads_ext - 1) / threads_ext;
                if (d != vectorized_indices[0] &&
                    threads_ext > 1 &&
                    threads_ext * other_ext * 7 > stage_sizes[0][d] * 8) {
                    break;
                }
                t.back() = threads_ext;
                validity valid_result = is_valid_tiling();
                if (valid_result == serial_count_err) {
                    continue;
                } else if (valid_result == thread_count_err) {
                    break;
                } else {
                    result.push_back(t);
                }

                if (threads_ext >= stage_sizes[0][d]) {
                    break;
                }
            }

            if (!full_extent_considered && stage_sizes[0][d] < max_threads_extent) {
                t.back() = stage_sizes[0][d];
                validity valid_result = is_valid_tiling();
                if (valid_result != serial_count_err && valid_result != thread_count_err) {
                    result.push_back(t);
                }
            }
        }
    }
    return result;
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
