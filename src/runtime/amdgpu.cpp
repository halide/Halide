#include "HalideRuntimeAMDGPU.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "mini_amdgpu.h"
#include "scoped_spin_lock.h"

#define INLINE inline __attribute__((always_inline))

namespace Halide { namespace Runtime { namespace Internal {namespace Amdgpu {

#define HIP_FN(ret, fn, args) WEAK ret (HIPAPI *fn)args;

#include "hip_functions.h"



}}}}
