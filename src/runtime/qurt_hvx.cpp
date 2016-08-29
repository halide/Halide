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

// Notes:
//  - Prefetches can be queued up to 3 deep (MAX_PREFETCH)
//  - If 3 are already pending, the oldest request is dropped
//  - USR:PFA status bit is set to indicate that prefetches are in progress
//  - A l2fetch with any subfield set to zero cancels all pending prefetches
//
#define MIN(x,y)        (((x)<(y)) ? (x) : (y))
#define MAX(x,y)        (((x)>(y)) ? (x) : (y))
#define MASK16          0xFFFF
#define MAX_PREFETCH    3

// TODO: Opt: Generate control code for prefetch_buffer_t in Prefetch.cpp
// TODO       Passing box info through a buffer_t results in ~30 additional stores/loads
WEAK int halide_hexagon_prefetch_buffer_t(unsigned int dim, const buffer_t *buf)
{
    // Extract needed fields from buffer_t
    const int32_t elem_size_bytes = buf->elem_size;
    const int32_t *min    = buf->min;
    const int32_t *stride = buf->stride;
    const int32_t *extent = buf->extent;
    unsigned char *addr   = buf->host;
    unsigned int boxdim   = dim;  // dimensions of entire box to prefetch
    unsigned int iterdim  = 2;    // dimension to iterate over

    // Compute starting position of box
    int32_t startpos = 0;
    for (unsigned int i = 0; i < boxdim; i++) {
        startpos += min[i] * stride[i];
    }
    addr += startpos * elem_size_bytes;

    // Compute 2-D prefetch descriptor
    // l2fetch(Rs,Rtt): 48 bit descriptor
    uint32_t pdir    = 1;   // 0 row major, 1 = column major
    uint32_t pstride = stride[0] * elem_size_bytes;
    uint32_t pwidth  = extent[0] * elem_size_bytes;
    uint32_t pheight = 1;
    if (boxdim > 1) {
        pheight = extent[1];
    }

#if 0   // TODO: Opt: Box collapse disabled for now - not fully tested, and
        // TODO       collapse candidates seen so far exceed the stride mask size
    // For boxes with dimension > 2 try to "collapse" unit height dimensions
    int32_t newpstride = pstride;
    while (boxdim > 2) {
        if (pheight == 1) {     // if height is currently 1
            newpstride = stride[iterdim-1] * elem_size_bytes;  // update stride
        } else {
            break;              // otherwise, we're done collapsing
        }
        if (newpstride == (newpstride & MASK16)) {  // and if it fits in mask...
            pstride = newpstride;             // ...accept new stride
            pheight = extent[iterdim];        // ...and height
        } else {
            break;              // otherwise, we're done collapsing
        }
        boxdim--;       // remaining non-collapsed dimensions
        iterdim++;      // update innermost iterate dimension
    }
#endif

    pdir    = pdir & 0x1;           // bit  48
    pstride = MIN(pstride, MASK16); // bits 47:32
    pwidth  = MIN(pwidth,  MASK16); // bits 31:16
    pheight = MIN(pheight, MASK16); // bits 15:0

    uint64_t pdir64    = pdir;
    uint64_t pstride64 = pstride;
    uint64_t pdesc = (pdir64<<48) | (pstride64<<32) | (pwidth<<16) | pheight;

    // Hard coded descriptors for testing
    // uint64_t pdesc = 0x1020002000002; // col, 512 width, 2 rows
    // uint64_t pdesc = 0x1020002000001; // col, 512 width, 1 row
    // uint64_t pdesc = 0x1144014400001; // col, 5184 width, 1 row

    // debug(0) << "halide_hexagon_prefetch_buffer_t(" << dim << ", " << buf << ")"
    //          << " addr:" << addr << " pdesc:" << pdesc << "\n";

    if (boxdim <= 2) {  // 2-D box, perform a single prefetch

        // Perform prefetch
        // debug(0) << "  l2fetch(" << addr << "," << pdesc << ")\n";
        __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(pdesc));

    } else { // For higher dimension boxes...

        // debug(0) << "  iterdim: "    << iterdim << "\n";

        // If iterdim is not last dim && iterdim extent is 1...
        while ((iterdim < dim-1) && (extent[iterdim] == 1)) {
            iterdim++;          // ...iterate at the next higher dimension
        }

        // Get iteration stride and extents
        int32_t iterstride = stride[iterdim] * elem_size_bytes;
        int32_t iterextent = extent[iterdim];
        iterextent = MAX(iterextent, MAX_PREFETCH);   // limit max number

        // debug(0) << "  iterdim: "    << iterdim << "\n";
        // debug(0) << "  iterstride: " << iterstride << "\n";
        // debug(0) << "  iterextent: " << iterextent << "\n";

        // TODO: Add support for iterating over multiple higher dimensions?
        // TODO  Currently just iterating over one outer dimension since
        // TODO  only MAX_PREFETCH prefetches can be queued anyway.
        for (int32_t i = 0; i < iterextent; i++) {
            // Perform prefetch
            // debug(0) << "  l2fetch(" << addr << "," << pdesc << ")\n";
            __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(pdesc));
            addr += iterstride;
        }
    }

    // TODO: Opt: Return the size in bytes prefetched? (to see if more can be prefetched)
    // TODO: Opt: Return the number of prefetch instructions issued? (to not exceed MAX_PREFETCH)
    return 0;
}

}
