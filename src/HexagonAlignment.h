#ifndef HALIDE_HEXAGON_ALIGNMENT_H
#define HALIDE_HEXAGON_ALIGNMENT_H

/** \file
 * Class for analyzing Alignment of loads and stores for Hexagon.
 */

#include "IR.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

template<typename T>
bool is_hexagon_aligned(const T *op, int required_alignment, int *aligned_offset) {
    int native_lanes;
    if constexpr (std::is_same_v<T, Load>) {
        native_lanes = required_alignment / op->type.bytes();
    } else {
        native_lanes = required_alignment / op->value.type().bytes();
    }

    debug(3) << "HexagonAlignmentAnalyzer: Check if " << op->index << " is aligned to a "
             << required_alignment << " byte boundary\n"
             << "native_lanes: " << native_lanes << "\n";

    if (Expr index = op->index; !index.as<Ramp>() && index.type().is_vector()) {
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
    int64_t remainder = mod_imp(op->alignment.remainder, native_lanes);
    if (known_alignment && aligned_offset != nullptr) {
        *aligned_offset = static_cast<int>(remainder);
    }
    return known_alignment && remainder == 0;
}

}  // namespace Internal
}  // namespace Halide
#endif
