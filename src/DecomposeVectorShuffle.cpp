#include "DecomposeVectorShuffle.h"

#include <unordered_map>

namespace Halide::Internal {

std::vector<std::vector<NativeShuffle>> decompose_to_native_shuffles(
    int src_lanes, const std::vector<int> &indices, int vl) {

    int dst_lanes = static_cast<int>(indices.size());
    int src_lanes_aligned = align_up(src_lanes, vl);

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
            if (src_index < 0) {
                continue;
            }

            const int src_slice = src_index / vl;
            const int lane_in_src_slice = src_index % vl;
            const int lane_in_dst_slice = dst_index - dst_start;

            if (steps.empty()) {
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

}  // namespace Halide::Internal
