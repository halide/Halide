#ifndef HALIDE_DECOMPOSE_VECTOR_SHUFFLE_H
#define HALIDE_DECOMPOSE_VECTOR_SHUFFLE_H

/** \file
 *
 * Perform vector shuffle by decomposing the operation to
 * a sequence of the sub shuffle steps where each step is a shuffle of:
 * - One or two slices as input (slice_a and slice_b)
 * - Produce one slice (dst slice)
 * - All the slices have the same length as target native vector (vl)
 *
 * The structure of the sequence of steps consists of:
 * 1. Outer loop to iterate the slices of dst vector.
 * 2. Inner loop to iterate the native shuffle steps to complete a single dst slice.
 *    This can be multiple steps because a single native shuffle can take
 *    only 2 slices (native vector length x 2) at most, while we may need
 *    to fetch from wider location in the src vector.
 *
 * The following example, log of test code, illustrates how it works.
 *
 * src_lanes: 17, dst_lanes: 7, vl: 4
 *  input a: [0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, ]
 *  input b: [170, 180, 190, 200, 210, 220, 230, 240, 250, 260, 270, 280, 290, 300, 310, 320, 330, ]
 *  indices: [6, 13, 24, 14, 7, 11, 5, ]
 *
 *  slice a:[40, 50, 60, 70, ],  slice b:[120, 130, 140, 150, ],  indices:[2, 5, -1, 6, ]
 *    => slice output:[60, 130, -559038801, 140, ]
 *  slice a:[60, 130, -559038801, 140, ],  slice b:[210, 220, 230, 240, ],  indices:[0, 1, 7, 3, ]
 *    => slice output:[60, 130, 240, 140, ]
 *  slice a:[40, 50, 60, 70, ],  slice b:[80, 90, 100, 110, ],  indices:[3, 7, 1, -1, ]
 *    => slice output:[70, 110, 50, -559038801, ]
 *
 *  output: [60, 130, 240, 140, 70, 110, 50, ]
 *
 */
#include "Util.h"
#include <optional>
#include <unordered_map>
#include <vector>

namespace Halide {
namespace Internal {

/** Base class for the algorithm logic of shuffle decomposition which is implemented
 * independently from the type of vector and the implementation of primitive vector operations.
 * Therefore, the concrete class must provide the following member functions
 * for the specific vector type it handles.
 * - get_vec_length
 * - align_up_vector
 * - slice_vec
 * - concat_vecs
 * - shuffle_vl_aligned
 *  */
template<typename T, typename VecTy>
struct DecomposeVectorShuffle {

    struct NativeShuffle {
        int slice_a;
        int slice_b;
        std::vector<int> lane_map;

        NativeShuffle(int vl, int a, int b)
            : slice_a(a), slice_b(b) {
            lane_map.resize(vl, -1);
        }
    };

    /** Enum to represent the special cases of slice index */
    enum {
        SLICE_INDEX_NONE = -1,
        SLICE_INDEX_CARRY_PREV_RESULT = -2,
    };

    DecomposeVectorShuffle(const VecTy &src_a, const VecTy &src_b, const std::vector<int> &indices, int vl)
        : src_a(src_a), src_b(src_b), indices(indices), vl(vl) {
    }

    VecTy shuffle() {
        src_lanes = derived.get_vec_length(src_a);
        dst_lanes = static_cast<int>(indices.size());
        src_lanes_aligned = align_up(src_lanes, vl);

        std::vector<std::vector<NativeShuffle>> all_steps = decompose_to_native_shuffles();

        src_a = derived.align_up_vector(src_a, vl);
        src_b = derived.align_up_vector(src_b, vl);

        // process each block divided by vl
        std::vector<VecTy> shuffled_dst_slices;
        shuffled_dst_slices.reserve(all_steps.size());

        for (auto &steps_for_dst_slice : all_steps) {
            VecTy dst_slice;

            for (const auto &step : steps_for_dst_slice) {
                // Obtain 1st slice a
                VecTy a;
                if (step.slice_a == SLICE_INDEX_CARRY_PREV_RESULT) {
                    a = dst_slice;
                } else {
                    a = get_vl_slice(step.slice_a);
                }
                // Obtain 2nd slice b
                std::optional<VecTy> b;
                if (step.slice_b == SLICE_INDEX_NONE) {
                    b = std::nullopt;
                } else {
                    b = std::optional<VecTy>(get_vl_slice(step.slice_b));
                }
                // Perform shuffle where vector length is aligned
                dst_slice = derived.shuffle_vl_aligned(a, b, step.lane_map, vl);
            }

            shuffled_dst_slices.push_back(dst_slice);
        }

        return derived.slice_vec(derived.concat_vecs(shuffled_dst_slices), 0, dst_lanes);
    }

private:
    std::vector<std::vector<NativeShuffle>> decompose_to_native_shuffles() {

        // Adjust indices so that src vectors are aligned up to multiple of vl
        std::vector<int> aligned_indices = indices;
        for (int &idx : aligned_indices) {
            if (idx >= src_lanes) {
                idx += src_lanes_aligned - src_lanes;
            }
        }

        const int num_dst_slices = align_up(dst_lanes, vl) / vl;
        std::vector<std::vector<NativeShuffle>> all_steps(num_dst_slices);

        for (int dst_slice = 0; dst_slice < num_dst_slices; dst_slice++) {
            std::unordered_map<int, int> slice_to_step;
            auto &steps = all_steps[dst_slice];
            const int dst_start = dst_slice * vl;

            for (int dst_index = dst_start; dst_index < dst_start + vl && dst_index < dst_lanes; ++dst_index) {
                const int src_index = aligned_indices[dst_index];
                const int src_slice = src_index / vl;
                const int lane_in_src_slice = src_index % vl;
                const int lane_in_dst_slice = dst_index - dst_start;
                if (src_index < 0) {
                    continue;

                } else if (steps.empty()) {
                    // first slice in this block
                    slice_to_step[src_slice] = 0;
                    steps.emplace_back(vl, src_slice, SLICE_INDEX_NONE);
                    steps.back().lane_map[lane_in_dst_slice] = lane_in_src_slice;

                } else if (auto itr = slice_to_step.find(src_slice); itr != slice_to_step.end()) {
                    // slice already seen
                    NativeShuffle &step = steps[itr->second];
                    bool is_a = (step.slice_a != SLICE_INDEX_CARRY_PREV_RESULT && step.slice_a == src_slice);
                    int offset = is_a ? 0 : vl;
                    step.lane_map[lane_in_dst_slice] = lane_in_src_slice + offset;

                } else if (steps[0].slice_b == SLICE_INDEX_NONE) {
                    // add as 'b' of first step if b is unused
                    slice_to_step[src_slice] = 0;
                    steps[0].slice_b = src_slice;
                    steps[0].lane_map[lane_in_dst_slice] = lane_in_src_slice + vl;

                } else {
                    // otherwise chain a new step
                    slice_to_step[src_slice] = static_cast<int>(steps.size());
                    // new step uses previous result as 'a', so we use 'b' for this one
                    steps.emplace_back(vl, SLICE_INDEX_CARRY_PREV_RESULT, src_slice);

                    // Except for the first step, we need to arrange indices
                    // so that the output carried from the previous step is kept
                    auto &lane_map = steps.back().lane_map;
                    // initialize lane_map as identical copy
                    for (size_t lane_idx = 0; lane_idx < lane_map.size(); ++lane_idx) {
                        lane_map[lane_idx] = lane_idx;
                    }
                    // update for this index
                    lane_map[lane_in_dst_slice] = lane_in_src_slice + vl;
                }
            }
        }

        return all_steps;
    }

    // Helper to extract slice with lanes=vl
    VecTy get_vl_slice(int slice_index) {
        const int num_slices_a = src_lanes_aligned / vl;
        int start_index = slice_index * vl;
        if (slice_index < num_slices_a) {
            return derived.slice_vec(src_a, start_index, vl);
        } else {
            start_index -= src_lanes_aligned;
            return derived.slice_vec(src_b, start_index, vl);
        }
    }

    T &derived = static_cast<T &>(*this);
    VecTy src_a;
    VecTy src_b;
    std::vector<int> indices;
    int vl;
    int src_lanes;
    int src_lanes_aligned;
    int dst_lanes;
};

// Test called by test/internal.cpp
void decompose_vector_shuffle_test();

}  // namespace Internal
}  // namespace Halide

#endif
