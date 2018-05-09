#ifndef HALIDE_ASSOCIATIVE_OPS_TABLE_H
#define HALIDE_ASSOCIATIVE_OPS_TABLE_H

/** \file
 * Tables listing associative operators and their identities.
 */

#include "IREquality.h"
#include "IROperator.h"

#include <iostream>
#include <vector>

namespace Halide {
namespace Internal {

/**
 * Represent an associative op with its identity. The op may be multi-dimensional,
 * e.g. complex multiplication. 'is_commutative' is set to true if the op is also
 * commutative in addition to being associative.
 *
 * For example, complex multiplication is represented as:
 \code
 AssociativePattern pattern(
    {x0 * y0 - x1 * y1, x1 * y0 + x0 * y1},
    {one, zero},
    true
 );
 \endcode
 */
struct AssociativePattern {
    /** Contain the binary operators for each dimension of the associative op. */
    std::vector<Expr> ops;
    /** Contain the identities for each dimension of the associative op. */
    std::vector<Expr> identities;
    /** Indicate if the associative op is also commutative. */
    bool is_commutative;

    AssociativePattern() : is_commutative(false) {}
    AssociativePattern(size_t size) : ops(size), identities(size), is_commutative(false) {}
    AssociativePattern(const std::vector<Expr> &ops, const std::vector<Expr> &ids, bool is_commutative)
        : ops(ops), identities(ids), is_commutative(is_commutative) {}
    AssociativePattern(Expr op, Expr id, bool is_commutative)
        : ops({op}), identities({id}), is_commutative(is_commutative) {}

    bool operator==(const AssociativePattern &other) const {
        if ((is_commutative != other.is_commutative) || (ops.size() != other.ops.size())) {
            return false;
        }
        for (size_t i = 0; i < size(); ++i) {
            if (!equal(ops[i], other.ops[i]) || !equal(identities[i], other.identities[i])) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const AssociativePattern &other) const { return !(*this == other); }
    size_t size() const { return ops.size(); }
    bool commutative() const { return is_commutative; }
};

const std::vector<AssociativePattern> &get_ops_table(const std::vector<Expr> &exprs);

}  // namespace Internal
}  // namespace Halide

#endif
