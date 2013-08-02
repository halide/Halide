#ifndef HALIDE_TUPLE_H
#define HALIDE_TUPLE_H

/** \file
 *
 * Defines Tuple - the front-end handle on small arrays of expressions.
 */

#include "IR.h"
#include "IROperator.h"
#include "Util.h"
#include <vector>

namespace Halide {

class FuncRefVar;
class FuncRefExpr;

/** Create a small array of Exprs for defining and calling functions
 * with multiple outputs. */
class Tuple {
private:
    std::vector<Expr> exprs;
public:
    /** The number of elements in the tuple. */
    size_t size() const { return exprs.size(); }

    /** Get a reference to an element. */
    Expr &operator[](size_t x) {
        assert(x < exprs.size() && "Tuple access out of bounds");
        return exprs[x];
    }

    /** Get a copy of an element. */
    Expr operator[](size_t x) const {
        assert(x < exprs.size() && "Tuple access out of bounds");
        return exprs[x];
    }

    /** Construct a Tuple from some Exprs. */
    //@{
    Tuple(Expr a, Expr b) :
        exprs(Internal::vec<Expr>(a, b)) {
    }

    Tuple(Expr a, Expr b, Expr c) :
        exprs(Internal::vec<Expr>(a, b, c)) {
    }

    Tuple(Expr a, Expr b, Expr c, Expr d) :
        exprs(Internal::vec<Expr>(a, b, c, d)) {
    }

    Tuple(Expr a, Expr b, Expr c, Expr d, Expr e) :
        exprs(Internal::vec<Expr>(a, b, c, d, e)) {
    }

    Tuple(Expr a, Expr b, Expr c, Expr d, Expr e, Expr f) :
        exprs(Internal::vec<Expr>(a, b, c, d, e, f)) {
    }
    //@}

    /** Construct a Tuple from a vector of Exprs */
    explicit Tuple(const std::vector<Expr> &e) : exprs(e) {
        assert(e.size() > 0 && "Tuples must have at least one element\n");
    }

    /** Construct a Tuple from a function reference. */
    // @{
    Tuple(const FuncRefVar &);
    Tuple(const FuncRefExpr &);
    // @}

    /** Treat the tuple as a vector of Exprs */
    const std::vector<Expr> &as_vector() const {
        return exprs;
    }
};

/** Equivalents of some standard operators for tuples. */
// @{
inline Tuple tuple_select(Tuple condition, const Tuple &true_value, const Tuple &false_value) {
    Tuple result(std::vector<Expr>(condition.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = select(condition[i], true_value[i], false_value[i]);
    }
    return result;
}

inline Tuple tuple_select(Expr condition, const Tuple &true_value, const Tuple &false_value) {
    Tuple result(std::vector<Expr>(true_value.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = select(condition, true_value[i], false_value[i]);
    }
    return result;
}
// @}

}

#endif
