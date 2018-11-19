#include "runtime_internal.h"

void abort();

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK __attribute__((always_inline)) void halide_abort() {
    abort();
}

}}}
