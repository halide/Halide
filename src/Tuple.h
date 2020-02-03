#ifndef HALIDE_TUPLE_H
#define HALIDE_TUPLE_H

/** \file
 *
 * Defines Tuple - the front-end handle on small arrays of expressions.
 */

#include "IR.h"
#include "Util.h"

namespace Halide {

class FuncRef;

/** Create a small array of Exprs for defining and calling functions
 * with multiple outputs. */
class Tuple {
private:
    std::vector<Expr> exprs;

public:
    /** The number of elements in the tuple. */
    size_t size() const {
        return exprs.size();
    }

    /** Get a reference to an element. */
    Expr &operator[](size_t x) {
        user_assert(x < exprs.size()) << "Tuple access out of bounds\n";
        return exprs[x];
    }

    /** Get a copy of an element. */
    Expr operator[](size_t x) const {
        user_assert(x < exprs.size()) << "Tuple access out of bounds\n";
        return exprs[x];
    }

    /** Construct a Tuple of a single Expr */
    explicit Tuple(Expr e) {
        exprs.push_back(e);
    }

    /** Construct a Tuple from some Exprs. */
    //@{
    template<typename... Args>
    Tuple(Expr a, Expr b, Args &&... args) {
        exprs = std::vector<Expr>{a, b, std::forward<Args>(args)...};
    }
    //@}

    /** Construct a Tuple from a vector of Exprs */
    explicit HALIDE_NO_USER_CODE_INLINE Tuple(const std::vector<Expr> &e)
        : exprs(e) {
        user_assert(!e.empty()) << "Tuples must have at least one element\n";
    }

    /** Construct a Tuple from a function reference. */
    Tuple(const FuncRef &);

    /** Treat the tuple as a vector of Exprs */
    const std::vector<Expr> &as_vector() const {
        return exprs;
    }
};

}  // namespace Halide

#endif
