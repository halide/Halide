#include "Realization.h"

#include "Buffer.h"
#include "Error.h"

namespace Halide {

/** The number of images in the Realization. */
size_t Realization::size() const {
    return images.size();
}

/** Get a const reference to one of the images. */
const Buffer<void> &Realization::operator[](size_t x) const {
    user_assert(x < images.size()) << "Realization access out of bounds\n";
    return images[x];
}

/** Get a reference to one of the images. */
Buffer<void> &Realization::operator[](size_t x) {
    user_assert(x < images.size()) << "Realization access out of bounds\n";
    return images[x];
}

/** Construct a Realization that refers to the buffers in an
 * existing vector of Buffer<> */
Realization::Realization(std::vector<Buffer<void>> &e)
    : images(e) {
    user_assert(!e.empty()) << "Realizations must have at least one element\n";
}

/** Call device_sync() for all Buffers in the Realization.
 * If one of the calls returns an error, subsequent Buffers won't have
 * device_sync called; thus callers should consider a nonzero return
 * code to mean that potentially all of the Buffers are in an indeterminate
 * state of sync.
 * Calling this explicitly should rarely be necessary, except for profiling. */
int Realization::device_sync(void *ctx) {
    for (auto &b : images) {
        int result = b.device_sync(ctx);
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

}  // namespace Halide
