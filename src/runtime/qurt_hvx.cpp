#include "runtime_internal.h"
#include "HalideRuntimeQurt.h"
#include "printer.h"
#include "mini_qurt.h"

using namespace Halide::Runtime::Internal::Qurt;

extern "C" {

enum qurt_hvx_mode_t {
    QURT_HVX_MODE_64B = 0,
    QURT_HVX_MODE_128B = 1,
};

extern int qurt_hvx_lock(qurt_hvx_mode_t);
extern int qurt_hvx_unlock();

WEAK int halide_qurt_hvx_lock(void *user_context, int size) {
    qurt_hvx_mode_t mode;
    switch (size) {
    case 64: mode = QURT_HVX_MODE_64B; break;
    case 128: mode = QURT_HVX_MODE_128B; break;
    default:
        error(user_context) << "HVX lock size must be 64 or 128.\n";
        return -1;
    }

    debug(user_context) << "QuRT: qurt_hvx_lock(" << mode << ") ->\n";
    int result = qurt_hvx_lock(mode);
    debug(user_context) << "        " << result << "\n";
    if (result != QURT_EOK) {
        error(user_context) << "qurt_hvx_lock failed\n";
        return -1;
    }

    return 0;
}

WEAK int halide_qurt_hvx_unlock(void *user_context) {
    debug(user_context) << "QuRT: qurt_hvx_unlock ->\n";
    int result = qurt_hvx_unlock();
    debug(user_context) << "        " << result << "\n";
    if (result != QURT_EOK) {
        error(user_context) << "qurt_hvx_unlock failed\n";
        return -1;
    }

    return 0;
}

WEAK void halide_qurt_hvx_unlock_as_destructor(void *user_context, void * /*obj*/) {
    halide_qurt_hvx_unlock(user_context);
}

namespace {

// Construct a prefetch descriptor.
inline uint64_t make_prefetch_desc(uint16_t width, uint16_t height, uint16_t stride, uint16_t dir = 1) {
    return
        (static_cast<uint64_t>(dir)    << 48) |
        (static_cast<uint64_t>(stride) << 32) |
        (static_cast<uint64_t>(width)  << 16) |
        (static_cast<uint64_t>(height) << 0);
}

// Notes:
//  - Prefetches can be queued up to 3 deep (MAX_PREFETCH)
//  - If 3 are already pending, the oldest request is dropped
//  - USR:PFA status bit is set to indicate that prefetches are in progress
//  - A l2fetch with any subfield set to zero cancels all pending prefetches
//  - The l2fetch starting address must be in mapped memory but the range
//    prefetched can go into unmapped memory without raising an exception
inline void l2_fetch_desc(const void *addr, uint64_t desc) {
    __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(desc));
}

inline void l2_fetch(const void *addr, uint16_t width, uint16_t height, uint16_t stride, uint16_t dir = 1) {
    l2_fetch_desc(addr, make_prefetch_desc(width, height, stride, dir));
}

const int uint16_max = (1 << 16) - 1;

uint16_t uint16_sat(int x) {
    return static_cast<uint16_t>(min(x, uint16_max));
}

// Prefetch the memory indicated by dimensions [0, dim] in buf.
inline void prefetch_buffer_dim(int dim, const uint8_t *host, const buffer_t *buf) {
    if (buf->extent[dim] == 0) {
        // Nothing to do for this dimension.
        if (dim > 0) {
            // Move to the next dimension.
            prefetch_buffer_dim(dim - 1, host, buf);
        } else {
            // This buffer was empty.
        }
        return;
    }

    const int elem_size = buf->elem_size;

    if (dim == 0) {
        // prefetch a linear range of memory.
        l2_fetch(host, uint16_sat(buf->extent[0] * elem_size), 1, 1);
    } else if (dim == 1 && buf->stride[1] * elem_size <= uint16_max) {
        // Prefetch the first 2 dimensions of buf.
        l2_fetch(host,
                 uint16_sat(buf->extent[0] * elem_size),
                 uint16_sat(buf->extent[1]),
                 buf->stride[1] * elem_size);
    } else {
        // We need to prefetch a higher dimensional region, do it recursively.
        const int extent_dim = buf->extent[dim];
        const int stride_dim = buf->stride[dim];
        for (int i = 0; i < extent_dim; i++) {
            prefetch_buffer_dim(dim - 1, host, buf);
            host += stride_dim * elem_size;
        }
    }
}

}  // namespace

WEAK int halide_hexagon_prefetch_buffer_t(const buffer_t *buf) {
    // Start at the outermost dimension.
    prefetch_buffer_dim(3, buf->host, buf);
    return 0;
}

}
