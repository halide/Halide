#ifndef HALIDE_ASSOCIATIVITY_H
#define HALIDE_ASSOCIATIVITY_H

/** \file
 *
 * Methods for extracting an associative operator from a Func's update definition
 * if there is any and computing the identity of the associative operator.
 */

#include "AssociativeOpsTable.h"
#include "IR.h"
#include "IREquality.h"

#include <functional>

namespace Halide {
namespace Internal {

/**
 * Represent the equivalent associative op of an update definition.
 * For example, the following associative Expr, min(f(x), g(r.x) + 2),
 * where f(x) is the self-recurrence term, is represented as:
 \code
 AssociativeOp assoc(
    AssociativePattern(min(x, y), +inf, true),
    {Replacement("x", f(x))},
    {Replacement("y", g(r.x) + 2)},
    true
 );
 \endcode
 *
 * 'pattern' contains the list of equivalent binary/unary operators (+ identities)
 * for each Tuple element in the update definition. 'pattern' also contains
 * a boolean that indicates if the op is also commutative. 'xs' and 'ys'
 * contain the corresponding definition of each variable in the list of
 * binary operators.
 *
 * For unary operator, 'xs' is not set, i.e. it will be a pair of empty string
 * and undefined Expr: {"", Expr()}. 'pattern' will only contain the 'y' term in
 * this case. For example, min(g(r.x), 4), will be represented as:
 \code
 AssociativeOp assoc(
    AssociativePattern(y, 0, false),
    {Replacement("", Expr())},
    {Replacement("y", min(g(r.x), 4))},
    true
 );
 \endcode
 *
 * Self-assignment, f(x) = f(x), will be represented as:
 \code
 AssociativeOp assoc(
    AssociativePattern(x, 0, true),
    {Replacement("x", f(x))},
    {Replacement("", Expr())},
    true
 );
 \endcode
 * For both unary operator and self-assignment cases, the identity does not
 * matter. It can be anything.
 */
struct AssociativeOp {
    struct Replacement {
        /** Variable name that is used to replace the expr in 'op'. */
        std::string var;
        Expr expr;

        Replacement() = default;
        Replacement(const std::string &var, Expr expr) : var(var), expr(expr) {}

        bool operator==(const Replacement &other) const {
            return (var == other.var) && equal(expr, other.expr);
        }
        bool operator!=(const Replacement &other) const {
            return !(*this == other);
        }
    };

    /** List of pairs of binary associative op and its identity. */
    AssociativePattern pattern;
    std::vector<Replacement> xs;
    std::vector<Replacement> ys;
    bool is_associative;

    AssociativeOp() : is_associative(false) {}
    AssociativeOp(size_t size) : pattern(size), xs(size), ys(size), is_associative(false) {}
    AssociativeOp(const AssociativePattern &p, const std::vector<Replacement> &xs,
                  const std::vector<Replacement> &ys, bool is_associative)
        : pattern(p), xs(xs), ys(ys), is_associative(is_associative) {}

    bool associative() const { return is_associative; }
    bool commutative() const { return pattern.is_commutative; }
    size_t size() const { return pattern.size(); }
};

/**
 * Given an update definition of a Func 'f', determine its equivalent
 * associative binary/unary operator if there is any. 'is_associative'
 * indicates if the operation was successfuly proven as associative.
 */
AssociativeOp prove_associativity(
    const std::string &f, std::vector<Expr> args, std::vector<Expr> exprs);

void associativity_test();

}  // namespace Internal
}  // namespace Halide

#endif
