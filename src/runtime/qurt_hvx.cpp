#include "HalideRuntimeQurt.h"
#include "mini_qurt.h"
#include "printer.h"
#include "runtime_internal.h"

using namespace Halide::Runtime::Internal::Qurt;

extern "C" {

WEAK int halide_qurt_hvx_lock(void *user_context) {
    const qurt_hvx_mode_t mode = QURT_HVX_MODE_128B;
    debug(user_context) << "QuRT: qurt_hvx_lock(" << mode << ") ->\n";
    int result = qurt_hvx_lock(mode);
    debug(user_context) << "        " << result << "\n";
    if (result != QURT_EOK) {
        error(user_context) << "qurt_hvx_lock failed";
        return halide_error_code_generic_error;
    }
    return halide_error_code_success;
}

WEAK int halide_qurt_hvx_unlock(void *user_context) {
    debug(user_context) << "QuRT: qurt_hvx_unlock ->\n";
    int result = qurt_hvx_unlock();
    debug(user_context) << "        " << result << "\n";
    if (result != QURT_EOK) {
        error(user_context) << "qurt_hvx_unlock failed";
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

WEAK void halide_qurt_hvx_unlock_as_destructor(void *user_context, void * /*obj*/) {
    (void)halide_qurt_hvx_unlock(user_context);  // ignore errors
}

// These need to inline, otherwise the extern call with the ptr
// parameter breaks a lot of optimizations.
WEAK_INLINE int _halide_prefetch_2d(const void *ptr, int width_bytes, int height, int stride_bytes) {
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

struct hexagon_buffer_t_arg {
    uint64_t device;
    uint8_t *host;
};

WEAK_INLINE uint8_t *_halide_hexagon_buffer_get_host(const hexagon_buffer_t_arg *buf) {
    return buf->host;
}

WEAK_INLINE uint64_t _halide_hexagon_buffer_get_device(const hexagon_buffer_t_arg *buf) {
    return buf->device;
}

WEAK_INLINE int _halide_hexagon_do_par_for(void *user_context, halide_task_t f,
                                           int min, int size, uint8_t *closure,
                                           int use_hvx) {
    if (use_hvx) {
        if (auto result = halide_qurt_hvx_unlock(user_context);
            result != halide_error_code_success) {
            return result;
        }
    }

    if (auto result = halide_do_par_for(user_context, f, min, size, closure);
        result != halide_error_code_success) {
        return result;
    }

    if (use_hvx) {
        if (auto result = halide_qurt_hvx_lock(user_context);
            result != halide_error_code_success) {
            return result;
        }
    }

    return halide_error_code_success;
}
}
