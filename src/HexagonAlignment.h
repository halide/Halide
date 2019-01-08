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

class HexagonAlignmentAnalyzer {
    Scope<ModulusRemainder> alignment_info;
    int required_alignment;
public:
    HexagonAlignmentAnalyzer(int required_alignment,
                             const Scope<ModulusRemainder>& alignment_info) : required_alignment(required_alignment) {
        this->alignment_info.set_containing_scope(&alignment_info);
    }
    /** Analyze the index of a load/store instruction for alignment
     *  Returns true if it can determing that the address of the store or load is aligned, false otherwise.
     */
    template<typename T>
    bool is_aligned_impl(const T *op, int native_lanes, int *aligned_offset) {
        DEBUG(3) << "HexagonAlignmentAnalyzer: Check if " << op->index << " is aligned to a "
                 << required_alignment << " byte boundary\n";
        DEBUG(3) << "native_lanes: " << native_lanes << "\n";
        Expr index = op->index;
        const Ramp *ramp = index.as<Ramp>();
        if (ramp) {
            index = ramp->base;
        } else if (index.type().is_vector()) {
            DEBUG(3) << "Is Unaligned\n";
            return false;
        }
        // If this is a parameter, the base_alignment should be
        // host_alignment. Otherwise, this is an internal buffer,
        // which we assume has been aligned to the required alignment.
        int base_alignment =
            op->param.defined() ? op->param.host_alignment() : required_alignment;

        *aligned_offset = 0;
        bool known_alignment = false;
        if (base_alignment % required_alignment == 0) {
            // We know the base is aligned. Try to find out the offset
            // of the ramp base from an aligned offset.
            known_alignment = reduce_expr_modulo(index, native_lanes, aligned_offset,
                                                 alignment_info);
        }
        if (known_alignment && (*aligned_offset == 0)) {
            DEBUG(3) << "Is Aligned\n";
            return true;
        }
        DEBUG(3) << "Is Unaligned\n";
        return false;
    }
    bool is_aligned(const Load *op, int *aligned_offset) {
        int native_lanes = required_alignment / op->type.bytes();
        return is_aligned_impl<Load>(op, native_lanes, aligned_offset);
    }
    bool is_aligned(const Store *op, int *aligned_offset) {
        int native_lanes = required_alignment / op->value.type().bytes();
        return is_aligned_impl<Store>(op, native_lanes, aligned_offset);
    }

    Scope<ModulusRemainder>& get() { return alignment_info; }

    void push(const std::string &name, Expr v) {
        alignment_info.push(name, modulus_remainder(v, alignment_info));
    }
    void pop(const std::string &name) {
        alignment_info.pop(name);
    }
};

}  // namespace Internal
}  // namespace Halide
#endif
