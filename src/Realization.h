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
        // use our operator[] overload so that we get proper range-checking
        return (*this)[0].as<T, Dims>();
    }

    /** Construct a Realization that acts as a reference to a single
     * existing Buffer. The element type of the Buffer may not be
     * const. */
    // @{
    explicit Realization(const Buffer<void> &e);
    explicit Realization(Buffer<void> &&e);
    // @}

    /** Construct a Realization that refers to the buffers in an
     * existing vector of Buffer<>. The element type of the Buffer(s) may not be
     * const */
    // @{
    explicit Realization(const std::vector<Buffer<void>> &e);
    explicit Realization(std::vector<Buffer<void>> &&e);
    // This ctor allows us to avoid ambiguity when the vector is specified as
    // a braced literal, e.g. `Realization({first, second})`
    explicit Realization(std::initializer_list<Buffer<void>> e)
        : Realization(std::vector<Buffer<void>>{e}) {
    }
    // @}

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
