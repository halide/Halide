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

#define MIN(x,y)  ((x)<(y)) ? (x) : (y)
#define MAX(x,y)  ((x)>(y)) ? (x) : (y)

WEAK int halide_hexagon_prefetch_buffer_t(unsigned int dim, buffer_t *buf)
{
    // Extract needed fields from buffer_t
    int32_t elem_size_bytes = buf->elem_size;
    int32_t extent0 = buf->extent[0];
    int32_t extent1 = buf->extent[1];
    int32_t stride0 = buf->stride[0];
    int32_t stride1 = buf->stride[1];
    int32_t stride2 = buf->stride[2];
    int32_t stride3 = buf->stride[3];
    int32_t min0    = buf->min[0];
    int32_t min1    = buf->min[1];
    int32_t min2    = buf->min[2];
    int32_t min3    = buf->min[3];

    // Compute starting position of box
    unsigned char *addr = buf->host;
    addr += elem_size_bytes * (min0 * stride0 +
                               min1 * stride1 +
                               min2 * stride2 +
                               min3 * stride3);

    // Compute prefetch descriptor
    // l2fetch(Rs,Rtt): 48 bit descriptor
    uint32_t dir    = 1;   // 0 row major, 1 = column major
    uint32_t stride = stride0 * elem_size_bytes;
    uint32_t width  = extent0 * elem_size_bytes;
    uint32_t height = extent1;

    dir    = dir & 0x1;           // bit  48
    stride = MIN(stride, 0xFFFF); // bits 47:32
    width  = MIN(width,  0xFFFF); // bits 31:16
    height = MIN(height, 0xFFFF); // bits 15:0

    uint64_t dir64    = dir;
    uint64_t stride64 = stride;
    uint64_t desc = (dir64<<48) | (stride64<<32) | (width<<16) | height;

    // Hard coded descriptors for testing
    // uint64_t desc = 0x1020002000002; // col, 512 width, 2 rows
    // uint64_t desc = 0x1020002000001; // col, 512 width, 1 row
    // uint64_t desc = 0x1144014400001; // col, 5184 width, 1 row

    // FIXME: iterate over buffer_t 3rd & 4th dimensions

    // Perform prefetch
    // notes:
    //  - Prefetches can be queued up to 3 deep
    //  - If 3 are already pending, the oldest request is dropped
    //  - USR:PFA status bit is set to indicate that prefetches are in progress
    //  - A l2fetch with any subfield set to zero cancels all pending prefetches
    __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(desc));

    return 0;
}

}
