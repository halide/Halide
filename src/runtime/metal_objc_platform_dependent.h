#ifndef HALIDE_OBJC_METAL_PLATFORM_DEPENDENT_H
#define HALIDE_OBJC_METAL_PLATFORM_DEPENDENT_H

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

struct mtl_compute_command_encoder;

void dispatch_threadgroups(mtl_compute_command_encoder *encoder,
                           int32_t blocks_x, int32_t blocks_y, int32_t blocks_z,
                           int32_t threads_x, int32_t threads_y, int32_t threads_z);

}}}}

#endif
