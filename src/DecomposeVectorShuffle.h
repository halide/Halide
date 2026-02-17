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
#include <vector>

namespace Halide {
namespace Internal {

/** Enum to represent the special cases of slice index */
enum {
    SLICE_INDEX_NONE = -1,
    SLICE_INDEX_CARRY_PREV_RESULT = -2,
};

struct NativeShuffle {
    int slice_a;
    int slice_b;
    std::vector<int> lane_map;

    NativeShuffle(int vl, int a, int b)
        : slice_a(a), slice_b(b) {
        lane_map.resize(vl, SLICE_INDEX_NONE);
    }
};

std::vector<std::vector<NativeShuffle>> decompose_to_native_shuffles(
    int src_lanes, const std::vector<int> &indices, int vl);

/** Base class for the algorithm logic of shuffle decomposition which is implemented
 * independently from the type of vector and the implementation of primitive vector operations.
 * Therefore, the concrete class must provide the following member functions
 * for the specific vector type it handles.
 */
template<typename Ops>
struct DecomposeVectorShuffle {
    using VecTy = typename Ops::VecTy;

    // TODO: when upgrading to C++20, replace with a concept.
    static_assert(std::is_invocable_r_v<VecTy, decltype(&Ops::align_up_vector), Ops &, const VecTy &, int>,
                  "Ops must provide: VecTy align_up_vector(const VecTy &, int)");
    static_assert(std::is_invocable_r_v<VecTy, decltype(&Ops::concat_vecs), Ops &, const std::vector<VecTy> &>,
                  "Ops must provide: VecTy concat_vecs(const std::vector<VecTy> &)");
    static_assert(std::is_invocable_r_v<VecTy, decltype(&Ops::shuffle_vl_aligned), Ops &,
                                        const VecTy &, const std::optional<VecTy> &, const std::vector<int> &, int>,
                  "Ops must provide: VecTy shuffle_vl_aligned(const VecTy &, const std::optional<VecTy> &, const std::vector<int> &, int)");
    static_assert(std::is_invocable_r_v<VecTy, decltype(&Ops::slice_vec), Ops &, const VecTy &, int, size_t>,
                  "Ops must provide: VecTy slice_vec(const VecTy &, int, size_t)");

    DecomposeVectorShuffle(Ops ops, const VecTy &src_a, const VecTy &src_b, int src_lanes, int vl)
        : ops(ops),
          vl(vl),
          src_a(ops.align_up_vector(src_a, vl)),
          src_b(ops.align_up_vector(src_b, vl)),
          src_lanes(src_lanes),
          src_lanes_aligned(align_up(src_lanes, vl)) {
    }

    VecTy codegen(const std::vector<int> &indices) {
        auto shuffle_plan = decompose_to_native_shuffles(src_lanes, indices, vl);
        int dst_lanes = static_cast<int>(indices.size());

        // process each block divided by vl
        std::vector<VecTy> shuffled_dst_slices;
        shuffled_dst_slices.reserve(shuffle_plan.size());

        for (const auto &steps_for_dst_slice : shuffle_plan) {
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
                dst_slice = ops.shuffle_vl_aligned(a, b, step.lane_map, vl);
            }
            shuffled_dst_slices.push_back(dst_slice);
        }

        return ops.slice_vec(ops.concat_vecs(shuffled_dst_slices), 0, dst_lanes);
    }

private:
    // Helper to extract slice with lanes=vl
    VecTy get_vl_slice(int slice_index) {
        const int num_slices_a = src_lanes_aligned / vl;
        int start_index = slice_index * vl;
        if (slice_index < num_slices_a) {
            return ops.slice_vec(src_a, start_index, vl);
        } else {
            start_index -= src_lanes_aligned;
            return ops.slice_vec(src_b, start_index, vl);
        }
    }

    Ops ops;
    int vl;
    VecTy src_a;
    VecTy src_b;
    int src_lanes;
    int src_lanes_aligned;
};

}  // namespace Internal
}  // namespace Halide

#endif
