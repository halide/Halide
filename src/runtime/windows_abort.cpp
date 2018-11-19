#include "runtime_internal.h"

extern "C" void exit(int);
extern "C" int raise(int);

#ifdef BITS_64
extern "C" bool IsDebuggerPresent();
#else
extern "C" __stdcall bool IsDebuggerPresent();
#endif

#define SIGABRT 22

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK __attribute__((always_inline)) void halide_abort() {
    if (IsDebuggerPresent()) {
        abort();
    } else {
        // Debug variants of the MSVC runtime will present an "Abort, Retry, Ignore"
        // dialog in response to a call to abort(); we ~never want this unless there
        // is a Debugger attached. This is a close approximation that will kill the
        // process in a similar way. (Note that 3 is the exit code for the "abort" button.)
        raise(SIGABRT);
        exit(3);
    }
}

}}}
