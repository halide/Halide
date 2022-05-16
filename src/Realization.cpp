#include "Realization.h"

#include "Buffer.h"
#include "Error.h"

namespace Halide {

size_t Realization::size() const {
    return images.size();
}

const Buffer<void> &Realization::operator[](size_t x) const {
    user_assert(x < images.size()) << "Realization access out of bounds\n";
    return images[x];
}

Buffer<void> &Realization::operator[](size_t x) {
    user_assert(x < images.size()) << "Realization access out of bounds\n";
    return images[x];
}

Realization::Realization(const Buffer<void> &e)
    : images({e}) {
}

Realization::Realization(Buffer<void> &&e)
    : images({std::move(e)}) {
}

Realization::Realization(const std::vector<Buffer<void>> &e)
    : images(e) {
    user_assert(!images.empty()) << "Realizations must have at least one element\n";
}

Realization::Realization(std::vector<Buffer<void>> &&e)
    : images(std::move(e)) {
    user_assert(!images.empty()) << "Realizations must have at least one element\n";
}

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
