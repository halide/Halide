#include "HalideRuntimeQurt.h"
#include "mini_qurt.h"
#include "printer.h"
#include "runtime_internal.h"

using namespace Halide::Runtime::Internal::Qurt;

extern "C" {

WEAK int halide_qurt_hvx_lock(void *user_context, int size) {
    qurt_hvx_mode_t mode;
    switch (size) {
    case 64:
        mode = QURT_HVX_MODE_64B;
        break;
    case 128:
        mode = QURT_HVX_MODE_128B;
        break;
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

// These need to inline, otherwise the extern call with the ptr
// parameter breaks a lot of optimizations.
WEAK __attribute__((always_inline)) int _halide_prefetch_2d(const void *ptr, int width_bytes, int height, int stride_bytes) {
    // Notes:
    //  - Prefetches can be queued up to 3 deep (MAX_PREFETCH)
    //  - If 3 are already pending, the oldest request is dropped
    //  - USR:PFA status bit is set to indicate that prefetches are in progress
    //  - A l2fetch with any subfield set to zero cancels all pending prefetches
    //  - The l2fetch starting address must be in mapped memory but the range
    //    prefetched can go into unmapped memory without raising an exception
    const int dir = 1;
    uint64_t desc =
        (static_cast<uint64_t>(dir) << 48) |
        (static_cast<uint64_t>(stride_bytes) << 32) |
        (static_cast<uint64_t>(width_bytes) << 16) |
        (static_cast<uint64_t>(height) << 0);
    __asm__ __volatile__("l2fetch(%0,%1)"
                         :
                         : "r"(ptr), "r"(desc));
    return 0;
}

WEAK __attribute__((always_inline)) int _halide_prefetch(const void *ptr, int size) {
    _halide_prefetch_2d(ptr, size, 1, 1);
    return 0;
}

struct hexagon_buffer_t_arg {
    uint64_t device;
    uint8_t *host;
};

WEAK __attribute__((always_inline)) uint8_t *_halide_hexagon_buffer_get_host(const hexagon_buffer_t_arg *buf) {
    return buf->host;
}

WEAK __attribute__((always_inline)) uint64_t _halide_hexagon_buffer_get_device(const hexagon_buffer_t_arg *buf) {
    return buf->device;
}
}
