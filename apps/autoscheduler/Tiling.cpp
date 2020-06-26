#include "Tiling.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

bool all_ones(const std::vector<int64_t>& nums) {
    for (const auto& n : nums) {
        if (n != 1) {
            return false;
        }
    }
    return true;
}

bool equal_to_existing_size(const std::vector<int64_t>& s, const std::vector<int64_t>& nums) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != nums[i]) {
            return false;
        }
    }
    return true;
}

vector<vector<int64_t>> generate_serial_tilings(const vector<int64_t> &s, int d,
                                                int last_d,
                                                int vectorized_index,
                                                const vector<int> &vec_dim_serial_sizes,
                                                bool filter_small_outer_extents,
                                                bool allow_inner_ones) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        vector<vector<int64_t>> v;
        v = generate_serial_tilings(s, d - 1, last_d, vectorized_index, vec_dim_serial_sizes, filter_small_outer_extents, allow_inner_ones);
        for (auto t : v) {
            t.push_back(0);
            bool used_full_extent = false;
            // include odd serial sizes that encourage multiples of 16 as thread tile size
            if (vec_dim_serial_sizes.size() > 0 && d == vectorized_index) {
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

            int max = s[d] == 3 ? s[d] : 8;
            int factor = s[d] == 3 ? s[d] : 2;

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
vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, int factor,
                                         bool allow_splits,
                                         const vector<int> &inner_sizes) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        vector<vector<int64_t>> v;
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
                        if (is_one && outer == 1) continue;
                        if (is_full && outer == s[d]) continue;
                        t.back() = outer;
                        result.push_back(t);
                    }
                } else {
                    int max_inner = 0;
                    for (int inner = 1; inner < s[d]; inner *= factor) {
                        int outer = (s[d] + inner - 1) / inner;
                        if (is_one && outer == 1) continue;
                        if (is_full && outer == s[d]) continue;
                        // Stop when we hit inner sizes that would do too much recompute
                        if (inner > 1 && inner * outer * 7 > s[d] * 8) break;
                        max_inner = inner;
                        t.back() = outer;
                        result.push_back(t);
                    }

                    for (int outer = 1; outer <= s[d]; outer *= factor) {
                        int inner = (s[d] + outer - 1) / outer;
                        if (is_one && outer == 1) continue;
                        if (is_full && outer == s[d]) continue;
                        // Stop when we get into the regime covered by the loop above.
                        if (outer > 1 && inner < max_inner * 2) break;
                        // Or when the wasted compute gets too bad.
                        if (inner * outer * 7 > s[d] * 8) break;
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



}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

