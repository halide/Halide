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

class FuncRef;

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

    /** Construct a Tuple of a single Expr */
    explicit Tuple(Expr e) {
        exprs.push_back(e);
    }

    /** Construct a Tuple from some Exprs. */
    //@{
    template<typename ...Args>
    Tuple(Expr a, Expr b, Args&&... args) {
        exprs = std::vector<Expr>{a, b, std::forward<Args>(args)...};
    }
    //@}

    /** Construct a Tuple from a vector of Exprs */
    explicit NO_INLINE Tuple(const std::vector<Expr> &e) : exprs(e) {
        user_assert(e.size() > 0) << "Tuples must have at least one element\n";
    }

    /** Construct a Tuple from a function reference. */
    EXPORT Tuple(const FuncRef &);

    /** Treat the tuple as a vector of Exprs */
    const std::vector<Expr> &as_vector() const {
        return exprs;
    }
};

/** A Realization is a vector of references to existing Buffer
objects. Funcs with Tuple values return multiple images when you
realize them, and they return them as a Realization. Tuples are to
Exprs as Realizations are to Buffers. */
class Realization {
private:
    std::vector<BufferRef<>> images;
public:
    /** The number of images in the Realization. */
    size_t size() const { return images.size(); }

    /** Get a const reference to one of the images. */
    const Buffer<void, 0> &operator[](size_t x) const {
        user_assert(x < images.size()) << "Realization access out of bounds\n";
        return *images[x];
    }

    /** Get a reference to one of the images. */
    Buffer<void, 0> &operator[](size_t x) {
        user_assert(x < images.size()) << "Realization access out of bounds\n";
        return *images[x];
    }

    /** Single-element realizations are implicitly castable to Buffers. */
    template<typename T, int D>
    operator Buffer<T, D>() const {
        return *images[0];
    }

    /** Construct a Realization from some Buffers. */
    //@{
    template<typename T,
             int D,
             typename ...Args,
             typename = std::enable_if<Internal::all_are_convertible<Buffer<>, Args...>::value>>
    Realization(Buffer<T, D> &a, Args&&... args) {
        images = std::vector<BufferRef<>>({
                a.make_shared_ref(),
                args.make_shared_ref()...
            });
    }
    //@}

    /** Construct a Realization that refers to the buffers in an
     * existing vector of Buffer<> */
    explicit Realization(std::vector<Buffer<>> &e) {
        user_assert(e.size() > 0) << "Realizations must have at least one element\n";
        for (Buffer<> &im : e) {
            images.push_back(im.make_shared_ref());
        }
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
