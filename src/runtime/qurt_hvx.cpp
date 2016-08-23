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
    // get starting position of box
    unsigned char *addr = buf->host;
    addr += buf->elem_size * ((buf->min[0]) * buf->stride[0] +
                              (buf->min[1]) * buf->stride[1] +
                              (buf->min[2]) * buf->stride[2] +
                              (buf->min[3]) * buf->stride[3]);

    // compute descriptor
    // l2fetch(Rs,Rtt): 48 bit descriptor
    unsigned int stride = (buf->stride[0]) * buf->elem_size;
    unsigned int width  = (buf->extent[0] - buf->min[0]) * buf->elem_size;
    unsigned int height = (buf->extent[1] - buf->min[1]);
    unsigned int dir    = 0;  // 0 row major, 1 = colum major

    dir    = dir & 0x1;           // bit  48
    stride = MIN(stride, 0xFFFF); // bits 47:32
    width  = MIN(width,  0xFFFF); // bits 31:16
    height = MIN(height, 0xFFFF); // bits 15:0

    unsigned long long dirull    = dir;
    unsigned long long strideull = stride;
    unsigned long long desc = (dirull<<48) | (strideull<<32) | (width<<16) | height;

    // FIXME: hard coded descriptors for testing
    // unsigned long long desc = 0x1020002000002; // 512 width, 2 rows
    // unsigned long long desc = 0x1020002000001; // 512 width, 1 row
    // unsigned long long desc = 0x1144014400001; // 5184 width, 1 row

    // FIXME: iterate over buffer_t 3rd & 4th dimensions

    // perform prefetch
    // notes:
    //  - Prefetches can be queued up to 3 deep
    //  - If 3 are already pending, the olds requrest is dropped
    //  - USR:PFA status bit is set to indicate that prefetches are in progress
    //  - A l2fetch with any subfield set to zero cancels all pending prefetches
    __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(desc));

    return 0;
}

}
