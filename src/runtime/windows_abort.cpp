#include "runtime_internal.h"

void exit(int);
int raise(int);

#define SIGABRT 22

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK __attribute__((always_inline)) void halide_abort() {
    // Debug variants of the MSVC runtime will present an "Abort, Retry, Ignore"
    // dialog in response to a call to abort(); we ~never want this. This is
    // a close approximation that will kill the process in a similar way.
    // (Note that 3 is the exit code for the "abort" button.)
    raise(SIGABRT);
    exit(3);
}

}}}
