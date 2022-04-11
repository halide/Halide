#include "HalideRuntime.h"
#include "runtime_internal.h"

extern "C" struct mxArray { int32_t unused; };

namespace Halide {
namespace Runtime {
namespace Internal {

struct AllTheTypes {
    halide_filter_metadata_t a;
    halide_filter_argument_t b;
    halide_scalar_value_t c;
    halide_semaphore_t d;
    mxArray e;
};

WEAK void halide_unused_force_include_types() {
    static __attribute__((used)) AllTheTypes a;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
