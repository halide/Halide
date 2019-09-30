#ifndef HALIDE_HEXAGON_ALIGNMENT_H
#define HALIDE_HEXAGON_ALIGNMENT_H

/** \file
 * Class for analyzing Alignment of loads and stores for Hexagon.
 */

#include "IR.h"
#include "ModulusRemainder.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

// TODO: This class is barely stateful, and could probably be replaced with free functions.
class HexagonAlignmentAnalyzer {
    const int required_alignment;

public:
    HexagonAlignmentAnalyzer(int required_alignment)
        : required_alignment(required_alignment) {
        internal_assert(required_alignment != 0);
    }

    /** Analyze the index of a load/store instruction for alignment
     *  Returns true if it can determing that the address of the store or load is aligned, false otherwise.
     */
    template<typename T>
    bool is_aligned_impl(const T *op, int native_lanes, int64_t *aligned_offset) {
        debug(3) << "HexagonAlignmentAnalyzer: Check if " << op->index << " is aligned to a "
                 << required_alignment << " byte boundary\n"
                 << "native_lanes: " << native_lanes << "\n";
        Expr index = op->index;
        const Ramp *ramp = index.as<Ramp>();
        if (ramp) {
            index = ramp->base;
        } else if (index.type().is_vector()) {
            debug(3) << "Is Unaligned\n";
            return false;
        }

        internal_assert(native_lanes != 0) << "Type is larger than required alignment of " << required_alignment << " bytes\n";

        // If this is a parameter, the base_alignment should be
        // host_alignment. Otherwise, this is an internal buffer,
        // which we assume has been aligned to the required alignment.
        if (op->param.defined() && ((op->param.host_alignment() % required_alignment) != 0)) {
            return false;
        }

        bool known_alignment = (op->alignment.modulus % native_lanes) == 0;
        if (known_alignment) {
            *aligned_offset = op->alignment.remainder % native_lanes;
        }
        return known_alignment && (*aligned_offset == 0);
    }

    bool is_aligned(const Load *op, int64_t *aligned_offset) {
        int native_lanes = required_alignment / op->type.bytes();
        return is_aligned_impl<Load>(op, native_lanes, aligned_offset);
    }

    bool is_aligned(const Store *op, int64_t *aligned_offset) {
        int native_lanes = required_alignment / op->value.type().bytes();
        return is_aligned_impl<Store>(op, native_lanes, aligned_offset);
    }
};

}  // namespace Internal
}  // namespace Halide
#endif
