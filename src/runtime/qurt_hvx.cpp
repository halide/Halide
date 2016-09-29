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

/////////////////////////////////////////////////////////////////////////////
//
// halide_hexagon_prefetch_buffer_t
//
// Prefetch a multi-dimensional box (subset) from a larger array
//
//   dim_count  number of dimensions in array
//   elem_size  size in bytes of one element
//   num_elem   total number of elements in array
//   buf        buffer_t describing box to prefetch
//              (note: extent/stride/min are all assumed to be positive)
//
// Notes:
//  - Prefetches can be queued up to 3 deep (MAX_PREFETCH)
//  - If 3 are already pending, the oldest request is dropped
//  - USR:PFA status bit is set to indicate that prefetches are in progress
//  - A l2fetch with any subfield set to zero cancels all pending prefetches
//  - The l2fetch starting address must be in mapped memory but the range
//    prefetched can go into unmapped memory without raising an exception
//
// TODO: Opt: Generate more control code for prefetch_buffer_t directly in
// TODO       Prefetch.cpp to avoid passing box info through a buffer_t
// TODO       (which results in ~30 additional stores/loads)
//
#define MAX_PREFETCH    3
#define MASK16          0xFFFF
#define MIN(x,y)        (((x)<(y)) ? (x) : (y))
#define MAX(x,y)        (((x)>(y)) ? (x) : (y))
//
WEAK int halide_hexagon_prefetch_buffer_t(const uint32_t dim_count, const int32_t elem_size,
                const uint32_t num_elem, const buffer_t *buf)
{
    // Extract needed fields from buffer_t
    unsigned char *addr   = buf->host;
    const int32_t *extent = buf->extent;
    const int32_t *stride = buf->stride;
    const int32_t *min    = buf->min;
    const unsigned char *addr_beg = addr;
    const unsigned char *addr_end = addr + (num_elem * elem_size);

    // Compute starting position of box
    int32_t startpos = 0;
    for (uint32_t i = 0; i < dim_count; i++) {
        startpos += min[i] * stride[i];
    }
    addr += startpos * elem_size;

    // Range check starting address
    if ((addr < addr_beg) || (addr >= addr_end)) {
        return 0;
    }

    // Compute the 2-D prefetch descriptor fields
    uint32_t pdir    = 1;       // Prefetch direction: 0=rows, 1=columns
    uint32_t pstride = stride[0] * elem_size;  // Stride ignored for 1-D
    uint32_t pwidth  = extent[0] * elem_size;  // 1-D width in bytes
    uint32_t pheight = 1;       // Initially a 1-D descriptor

    uint32_t iterdim = 1;       // Dimension to iterate over
    if (dim_count > 1) {        // Potential for 2-D descriptor
        uint32_t newpstride = stride[1] * elem_size;
        if (newpstride == (newpstride & MASK16)) {   // If stride fits...
            pstride = newpstride;       // 2-D stride in bytes
            pheight = extent[1];        // 2-D height (rows)
            iterdim++;                  // Iterate over next dimension
        }
    }

    // Create the prefetch descriptor for l2fetch
    //   l2fetch(Rs,Rtt): 48 bit descriptor
    pdir    = pdir & 0x1;           // bit  48
    pstride = MIN(pstride, MASK16); // bits 47:32
    pwidth  = MIN(pwidth,  MASK16); // bits 31:16
    pheight = MIN(pheight, MASK16); // bits 15:0

    uint64_t pdir64    = pdir;
    uint64_t pstride64 = pstride;
    uint64_t pdesc = (pdir64<<48) | (pstride64<<32) | (pwidth<<16) | pheight;

    const uint32_t pbytes = pwidth * pheight;   // Bytes in prefetch descriptor
    uint32_t tbytes = 0;                        // Total bytes prefetched
    uint32_t tcnt = 0;                          // Total count of prefetch ops

    // If iterdim extent is unity then move to next higher dimension
    while ((iterdim < dim_count) && (extent[iterdim] == 1)) {
        iterdim++;
    }

    if (iterdim >= dim_count) {       // No dimension remaining to iterate over

        // Perform a single prefetch
        __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(pdesc));
        tbytes += pbytes;
        tcnt++;

    } else {    // Iterate for higher dimension boxes

        // Get iteration stride and extents
        int32_t iterstride = stride[iterdim] * elem_size;
        int32_t iterextent = extent[iterdim];
        iterextent = MIN(iterextent, MAX_PREFETCH);   // Limit # of prefetches

        // TODO: Add support for iterating over multiple higher dimensions?
        // TODO  Currently just iterating over one outer (non-unity) dimension
        // TODO  since only MAX_PREFETCH prefetches can be queued anyway.

        for (int32_t i = 0; i < iterextent; i++) {
            // Range check starting address
            if ((addr >= addr_beg) && (addr < addr_end)) {
                // Perform prefetch
                __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(pdesc));
                tbytes += pbytes;
                tcnt++;
            }
            addr += iterstride;
        }
    }

    // TODO: Opt: Return the number of prefetch instructions (tcnt) issued?
    //       to not exceed MAX_PREFETCH in a sequence of prefetch ops
    // TODO: Opt: Return the size in bytes (tbytes) prefetched?
    //       to avoid prefetching too much data in one sequence
    return 0;
}

}
