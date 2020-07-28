#include "runtime_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK_INLINE int halide_malloc_alignment() {
    return 128;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
