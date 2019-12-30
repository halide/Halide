#include "runtime_internal.h"

#ifdef BITS_64
#define WIN32API
#else
#define WIN32API __stdcall
#endif

extern "C" WIN32API int32_t Sleep(int32_t timeout);

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK void halide_thread_yield() {
    Sleep(0);
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
