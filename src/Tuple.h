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

/** A Realization is a vector of references to existing Buffer
objects. Funcs with Tuple values return multiple images when you
realize them, and they return them as a Realization. Tuples are to
Exprs as Realizations are to Buffers. */
class Realization {
private:
    std::vector<Buffer<>> images;

public:
    /** The number of images in the Realization. */
    size_t size() const {
        return images.size();
    }

    /** Get a const reference to one of the images. */
    const Buffer<> &operator[](size_t x) const {
        user_assert(x < images.size()) << "Realization access out of bounds\n";
        return images[x];
    }

    /** Get a reference to one of the images. */
    Buffer<> &operator[](size_t x) {
        user_assert(x < images.size()) << "Realization access out of bounds\n";
        return images[x];
    }

    /** Single-element realizations are implicitly castable to Buffers. */
    template<typename T>
    operator Buffer<T>() const {
        return images[0];
    }

    /** Construct a Realization that acts as a reference to some
     * existing Buffers. The element type of the Buffers may not be
     * const. */
    template<typename T,
             typename... Args,
             typename = typename std::enable_if<Internal::all_are_convertible<Buffer<>, Args...>::value>::type>
    Realization(Buffer<T> &a, Args &&... args) {
        images = std::vector<Buffer<>>({a, args...});
    }

    /** Construct a Realization that refers to the buffers in an
     * existing vector of Buffer<> */
    explicit Realization(std::vector<Buffer<>> &e)
        : images(e) {
        user_assert(!e.empty()) << "Realizations must have at least one element\n";
    }

    /** Call device_sync() for all Buffers in the Realization.
     * If one of the calls returns an error, subsequent Buffers won't have
     * device_sync called; thus callers should consider a nonzero return
     * code to mean that potentially all of the Buffers are in an indeterminate
     * state of sync.
     * Calling this explicitly should rarely be necessary, except for profiling. */
    int device_sync(void *ctx = nullptr) {
        for (auto &b : images) {
            int result = b.device_sync(ctx);
            if (result != 0) {
                return result;
            }
        }
        return 0;
    }
};

}  // namespace Halide

#endif
