#include "HalideRuntime.h"

#ifdef BITS_64
typedef uint64_t addr_t;
#else
typedef uint32_t addr_t;
#endif

extern addr_t __DTOR_LIST__;
extern addr_t __CTOR_END__;
extern "C" {
__attribute__((section(".fini.halide")))
void run_dtors() {
    typedef void(*dtor_func)();
    addr_t *dtor_p = &__DTOR_LIST__;
    while (1) {
        dtor_func dtor = (dtor_func) *dtor_p;
        if (!dtor) {
            break;
        } else {
            dtor();
        }
        dtor_p++;
    }
}
__attribute__((section(".init.halide")))
void run_ctors() {
    typedef void(*ctor_func)();
    addr_t *ctor_p = &__CTOR_END__;
    while (1) {
        ctor_func ctor = (ctor_func) *(--ctor_p);
        if (!ctor) {
            break;
        } else {
            ctor();
        }
    }
}
} // extern "C"
