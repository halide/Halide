#include "HalideRuntime.h"
#include "objc_support.h"
#include "metal_objc_platform_dependent.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

WEAK void dispatch_threadgroups(mtl_compute_command_encoder *encoder,
                                int32_t blocks_x, int32_t blocks_y, int32_t blocks_z,
                                int32_t threads_x, int32_t threads_y, int32_t threads_z) {
#if BITS_64
    struct MTLSize {
        unsigned long width;
        unsigned long height;
        unsigned long depth;
    };

    MTLSize threadgroupsPerGrid;
    threadgroupsPerGrid.width = blocks_x;
    threadgroupsPerGrid.height = blocks_y;
    threadgroupsPerGrid.depth = blocks_z;

    MTLSize threadsPerThreadgroup;
    threadsPerThreadgroup.width = threads_x;
    threadsPerThreadgroup.height = threads_y;
    threadsPerThreadgroup.depth = threads_z;

#if ARM_COMPILE
    typedef void (*dispatch_threadgroups_method)(objc_id encoder, objc_sel sel,
                                                 MTLSize *threadgroupsPerGrid, MTLSize *threadsPerThreadgroup);
    dispatch_threadgroups_method method = (dispatch_threadgroups_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("dispatchThreadgroups:threadsPerThreadgroup:"),
              &threadgroupsPerGrid, &threadsPerThreadgroup);
#elif X86_COMPILE
    typedef void (*dispatch_threadgroups_method)(objc_id encoder, objc_sel sel,
                                                 MTLSize threadgroupsPerGrid, MTLSize threadsPerThreadgroup);
    dispatch_threadgroups_method method = (dispatch_threadgroups_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("dispatchThreadgroups:threadsPerThreadgroup:"),
              threadgroupsPerGrid, threadsPerThreadgroup);
#endif
#else
    typedef void (*dispatch_threadgroups_method)(objc_id encoder, objc_sel sel,
                                                 int32_t blocks_x, int32_t blocks_y, int32_t blocks_z,
                                                 int32_t threads_x, int32_t threads_y, int32_t threads_z);
    dispatch_threadgroups_method method = (dispatch_threadgroups_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("dispatchThreadgroups:threadsPerThreadgroup:"),
              blocks_x, blocks_y, blocks_z, threads_x, threads_y, threads_z);
#endif
}

}}}}
