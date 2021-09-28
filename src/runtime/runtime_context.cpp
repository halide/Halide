// Ignore deprecation warnings inside our own runtime
#define HALIDE_ALLOW_DEPRECATED 1

#include "runtime_context.h"
#include "HalideRuntime.h"
#include "HalideRuntimeCuda.h"
#include "HalideRuntimeD3D12Compute.h"
#include "HalideRuntimeHexagonHost.h"
#include "HalideRuntimeMetal.h"
#include "HalideRuntimeOpenCL.h"
#include "HalideRuntimeOpenGLCompute.h"
#include "HalideRuntimeQurt.h"
#include "cpu_features.h"

extern "C" {

/* Note that this is NOT weak-linkage. */
static halide_context_t g_halide_default_context = {
    nullptr,       // user_context
    halide_print,  // print
    {0},           // reserved
};

/* Note that this is NOT weak-linkage. */
struct halide_context_t *halide_default_context() {
    return &g_halide_default_context;
}

}  // extern "C"
