#include "runtime_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK __attribute__((always_inline)) int halide_get_minimum_alignment() {
    return 128;
}

}}}
