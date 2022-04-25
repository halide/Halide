#ifndef HALIDE_REALIZATION_H
#define HALIDE_REALIZATION_H

#include <cstdint>
#include <vector>

#include "Buffer.h"
#include "Util.h"  // for all_are_convertible

/** \file
 *
 * Defines Realization - a vector of Buffer for use in pipelines with multiple outputs.
 */

namespace Halide {

/** A Realization is a vector of references to existing Buffer objects.
 *  A pipeline with multiple outputs realize to a Realization.  */
class Realization {
private:
    std::vector<Buffer<void>> images;

public:
    /** The number of images in the Realization. */
    size_t size() const;

    /** Get a const reference to one of the images. */
    const Buffer<void> &operator[](size_t x) const;

    /** Get a reference to one of the images. */
    Buffer<void> &operator[](size_t x);

    /** Single-element realizations are implicitly castable to Buffers. */
    template<typename T, int Dims>
    operator Buffer<T, Dims>() const {
        return images[0].as<T, Dims>();
    }

    /** Construct a Realization that acts as a reference to some
     * existing Buffers. The element type of the Buffers may not be
     * const. */
    template<typename T,
             int Dims,
             typename... Args,
             typename = typename std::enable_if<Internal::all_are_convertible<Buffer<void>, Args...>::value>::type>
    Realization(Buffer<T, Dims> &a, Args &&...args) {
        images = std::vector<Buffer<void>>({a, args...});
    }

    /** Construct a Realization that refers to the buffers in an
     * existing vector of Buffer<> */
    explicit Realization(std::vector<Buffer<void>> &e);

    /** Call device_sync() for all Buffers in the Realization.
     * If one of the calls returns an error, subsequent Buffers won't have
     * device_sync called; thus callers should consider a nonzero return
     * code to mean that potentially all of the Buffers are in an indeterminate
     * state of sync.
     * Calling this explicitly should rarely be necessary, except for profiling. */
    int device_sync(void *ctx = nullptr);
};

}  // namespace Halide

#endif
