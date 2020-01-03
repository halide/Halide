#include "runtime_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK __attribute__((always_inline)) int halide_malloc_alignment() {
    return 128;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
