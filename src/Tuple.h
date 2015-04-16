#ifndef HALIDE_TUPLE_H
#define HALIDE_TUPLE_H

/** \file
 *
 * Defines Tuple - the front-end handle on small arrays of expressions.
 */

#include "IR.h"
#include "IROperator.h"
#include "Util.h"

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
        user_assert(x < exprs.size()) << "Tuple access out of bounds\n";
        return exprs[x];
    }

    /** Get a copy of an element. */
    Expr operator[](size_t x) const {
        user_assert(x < exprs.size()) << "Tuple access out of bounds\n";
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
        user_assert(e.size() > 0) << "Tuples must have at least one element\n";
    }

    /** Construct a Tuple from a function reference. */
    // @{
    EXPORT Tuple(const FuncRefVar &);
    EXPORT Tuple(const FuncRefExpr &);
    // @}

    /** Treat the tuple as a vector of Exprs */
    const std::vector<Expr> &as_vector() const {
        return exprs;
    }
};

/** Funcs with Tuple values return multiple buffers when you realize
 * them. Tuples are to Exprs as Realizations are to Buffers. */
class Realization {
private:
    std::vector<Buffer> buffers;
public:
    /** The number of buffers in the Realization. */
    size_t size() const { return buffers.size(); }

    /** Get a reference to one of the buffers. */
    Buffer &operator[](size_t x) {
        user_assert(x < buffers.size()) << "Realization access out of bounds\n";
        return buffers[x];
    }

    /** Get one of the buffers. */
    Buffer operator[](size_t x) const {
        user_assert(x < buffers.size()) << "Realization access out of bounds\n";
        return buffers[x];
    }

    /** Single-element realizations are implicitly castable to Buffers. */
    operator Buffer() const {
        user_assert(buffers.size() == 1) << "Can only cast single-element realizations to buffers or images\n";
        return buffers[0];
    }

    /** Construct a Realization from some Buffers. */
    //@{
    Realization(Buffer a, Buffer b) :
        buffers(Internal::vec<Buffer>(a, b)) {}

    Realization(Buffer a, Buffer b, Buffer c) :
        buffers(Internal::vec<Buffer>(a, b, c)) {}

    Realization(Buffer a, Buffer b, Buffer c, Buffer d) :
        buffers(Internal::vec<Buffer>(a, b, c, d)) {}

    Realization(Buffer a, Buffer b, Buffer c, Buffer d, Buffer e) :
        buffers(Internal::vec<Buffer>(a, b, c, d, e)) {}

    Realization(Buffer a, Buffer b, Buffer c, Buffer d, Buffer e, Buffer f) :
        buffers(Internal::vec<Buffer>(a, b, c, d, e, f)) {}
    //@}

    /** Construct a Realization from a vector of Buffers */
    explicit Realization(const std::vector<Buffer> &e) : buffers(e) {
        user_assert(e.size() > 0) << "Realizations must have at least one element\n";
    }

    /** Treat the Realization as a vector of Buffers */
    const std::vector<Buffer> &as_vector() const {
        return buffers;
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
