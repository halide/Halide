#ifndef HALIDE_REALIZATION_H
#define HALIDE_REALIZATION_H

#include <cstdint>
#include <vector>

#include "Buffer.h"

/** \file
 *
 * Defines Realization - a vector of Buffer for use in pipelines with multiple outputs.
 */

namespace Halide {

/** A Realization is a vector of references to existing Buffer objects.
 *  A pipeline with multiple outputs realize to a Realization.  */
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
