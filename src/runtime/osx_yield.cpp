#include "runtime_internal.h"

extern "C" int swtch_pri(int);

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK void halide_thread_yield() {
    swtch_pri(0);
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
