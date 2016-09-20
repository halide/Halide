#include "runtime_internal.h"
#include "HalideRuntimeQurt.h"
#include "printer.h"
#include "mini_qurt.h"
#include "hexagon_remote/log.h"

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

// #define DEBUG_PREFETCH  1
#ifdef DEBUG_PREFETCH
// Only produce debug info in this range
#define DBG_PREFETCH_START   1
#define DBG_PREFETCH_STOP    9
//#define DBG_PREFETCH_STOP    0xFFFFFFFF
#define DBG_PREFETCH_V(...)  log_printf(__VA_ARGS__)
#define DBG_PREFETCH(...)    if ((dbg_cnt >= DBG_PREFETCH_START) && \
                                 (dbg_cnt <= DBG_PREFETCH_STOP)) { \
                               log_printf(__VA_ARGS__); \
                             }
#else
#define DBG_PREFETCH_V(...)  {}
#define DBG_PREFETCH(...)    {}
#endif

#define MIN(x,y)        (((x)<(y)) ? (x) : (y))
#define MAX(x,y)        (((x)>(y)) ? (x) : (y))
#define MASK16          0xFFFF
#define MAX_PREFETCH    3

// halide_hexagon_prefetch_buffer_t
//
// Prefetch a multi-dimensional box (subset) from a larger array
//
//   dim        number of dimensions in array
//   elem_size  size in bytes of one element
//   num_elem   total number of elements in array
//   buf        buffer_t describing box to prefetch
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
WEAK int halide_hexagon_prefetch_buffer_t(const uint32_t dim, const int32_t elem_size,
                const uint32_t num_elem, const buffer_t *buf)
{
    // Extract needed fields from buffer_t
    unsigned char *addr   = buf->host;
    const int32_t *extent = buf->extent;
    const int32_t *stride = buf->stride;
    const int32_t *min    = buf->min;
    unsigned int boxdim   = dim;  // dimensions of entire box to prefetch
    unsigned int iterdim  = 2;    // dimension to iterate over
#ifdef DEBUG_PREFETCH
    static uint32_t dbg_cnt = 0;  // limit how many calls debug is generated
    dbg_cnt++;
#endif
    DBG_PREFETCH("halide_hexagon_prefetch_buffer_t(%u, %d)\n", dim, elem_size, num_elem);

    const unsigned char *addr_beg = addr;
    const unsigned char *addr_end = addr + (num_elem * elem_size);
    DBG_PREFETCH("addr range 0x%p => 0x%p\n", addr_beg, addr_end);

    DBG_PREFETCH("buf host=0x%p elem_size=%d\n", addr, buf->elem_size);
    for (unsigned int i = 0; i < boxdim; i++) {
        DBG_PREFETCH("buf stride[%d]=0x%-8x min[%d]=%-6d ext[%d]=%d\n",
                          i, stride[i], i, min[i], i, extent[i]);
    }

    // Compute starting position of box
    int32_t startpos = 0;
    for (unsigned int i = 0; i < boxdim; i++) {
        startpos += min[i] * stride[i];
    }
    addr += startpos * elem_size;
    DBG_PREFETCH("startpos=0x%x => addr=0x%p\n", startpos, addr);
    // Range check starting address
    if ((addr < addr_beg) || (addr >= addr_end)) {
        DBG_PREFETCH_V("l2fetch: 0x%p out of range [0x%p, 0x%p]\n",
                        addr, addr_beg, addr_end);
        return 0;
    }

    // Compute 2-D prefetch descriptor
    // l2fetch(Rs,Rtt): 48 bit descriptor
    uint32_t pdir    = 1;   // 0 row major, 1 = column major
    uint32_t pstride = stride[0] * elem_size;
    uint32_t pwidth  = extent[0] * elem_size;
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
            newpstride = stride[iterdim-1] * elem_size;  // update stride
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

    DBG_PREFETCH("prefetch pdir:0x%x pstride:0x%x pwidth:0x%x pheight:0x%x\n",
                           pdir, pstride, pwidth, pheight);
    DBG_PREFETCH("prefetch addr:0x%p pdesc:0x%llx\n", addr, pdesc);

    if (boxdim <= 2) {  // 2-D box, perform a single prefetch

        // Perform prefetch
        DBG_PREFETCH("l2fetch(0x%p, 0x%llx)\n", addr, pdesc);
        __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(pdesc));

    } else { // For higher dimension boxes...

        DBG_PREFETCH("iterdim:%d\n", iterdim);

        // If iterdim is not last dim && iterdim extent is 1...
        while ((iterdim < dim-1) && (extent[iterdim] == 1)) {
            iterdim++;          // ...iterate at the next higher dimension
        }

        // Get iteration stride and extents
        int32_t iterstride = stride[iterdim] * elem_size;
        int32_t iterextent = extent[iterdim];
        iterextent = MAX(iterextent, MAX_PREFETCH);   // limit max number

        DBG_PREFETCH("idim:%d istride:0x%x iextent:%d\n", iterdim, iterstride, iterextent);

        // TODO: Add support for iterating over multiple higher dimensions?
        // TODO  Currently just iterating over one outer dimension since
        // TODO  only MAX_PREFETCH prefetches can be queued anyway.
        for (int32_t i = 0; i < iterextent; i++) {
            DBG_PREFETCH("%d: l2fetch(0x%p, 0x%llx)\n", i, addr, pdesc);
            // Range check starting address
            if ((addr >= addr_beg) && (addr < addr_end)) {
                // Perform prefetch
                __asm__ __volatile__ ("l2fetch(%0,%1)" : : "r"(addr), "r"(pdesc));
            } else {
                DBG_PREFETCH_V("%d: l2fetch: +0x%x => 0x%p out of range [0x%p, 0x%p]\n",
                                i, iterstride, addr, addr_beg, addr_end);
            }
            addr += iterstride;
        }
    }

    // TODO: Opt: Return the size in bytes prefetched? (to see if more can be prefetched)
    // TODO: Opt: Return the number of prefetch instructions issued? (to not exceed MAX_PREFETCH)
    return 0;
}

}
