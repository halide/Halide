#ifndef HALIDE_BUFFER_PTR_H
#define HALIDE_BUFFER_PTR_H

/** \file
 * Defines BufferPtr - A named shared pointer to a Halide::Buffer
 */

#include "runtime/HalideBuffer.h"
#include "Expr.h"
#include "Util.h"

namespace Halide {
namespace Internal {
struct BufferContents;

/** A named reference-counted handle on a Halide::Buffer<> */
class BufferPtr {
private:
    Internal::IntrusivePtr<Internal::BufferContents> contents;

public:
    BufferPtr() : contents(nullptr) {}
    EXPORT BufferPtr(const Buffer<> &buf, std::string name = "");

    template<typename T, int D> BufferPtr(const Buffer<T, D> &buf, std::string name = "") :
        BufferPtr(Buffer<>(buf), name) {}

    EXPORT BufferPtr(Type t, const std::vector<int> &size, std::string name = "");

    /** Compare two buffers for identity (not equality of data). */
    EXPORT bool same_as(const BufferPtr &other) const;

    /** Get the underlying Image */
    EXPORT Buffer<> &get();
    EXPORT const Buffer<> &get() const;

    /** Check if this buffer handle actually points to data. */
    EXPORT bool defined() const;

    /** Get the runtime name of this buffer used for debugging. */
    EXPORT const std::string &name() const;

    /** Get the Halide type of the underlying buffer */
    EXPORT Type type() const;

    /** Get the dimensionality of the underlying buffer */
    EXPORT int dimensions() const;

    /** Get a dimension from the underlying buffer. */
    EXPORT Buffer<>::Dimension dim(int i) const;

    /** Access to the mins, strides, extents. Will be deprecated. Do not use. */
    // @{
    EXPORT int min(int i) const;
    EXPORT int extent(int i) const;
    EXPORT int stride(int i) const;
    // @}

    /** Get the size in bytes of the allocation */
    EXPORT size_t size_in_bytes() const;

    /** Get a pointer to the raw buffer */
    EXPORT halide_buffer_t *raw_buffer() const;

    /** Get the host pointer */
    EXPORT uint8_t *host_ptr() const;

    /** Convert a buffer to a typed and dimensioned Image. Does
     * runtime type checks. */
    template<typename T, int D>
    operator Buffer<T, D>() const {
        return Buffer<T, D>(get());
    }

    /** Make a Call node to a specific site in this buffer. */
    // @{
    EXPORT Expr operator()(const std::vector<Expr> &loc) const;

    template<typename ...Args,
             typename = std::enable_if<(Internal::all_are_convertible<Expr, Args...>::value)>>
    NO_INLINE Expr operator()(Expr first, Args... rest) const {
        const std::vector<Expr> vec = {first, rest...};
        return (*this)(vec);
    }
    // @}
};

}

/** An adaptor so that it's possible to access a Halide::Image using Exprs. */
template<typename T, int D, typename ...Args,
         typename = std::enable_if<(Internal::all_are_convertible<Expr, Args...>::value)>>
NO_INLINE Expr image_accessor(const Buffer<T, D> &im, Expr first, Args... rest) {
    return Internal::BufferPtr(im)(first, rest...);
}

template<typename T, int D>
NO_INLINE Expr image_accessor(const Buffer<T, D> &im, const std::vector<Expr> &args) {
    return Internal::BufferPtr(im)(args);
}

}

#endif
