#include "HalideRuntime.h"

#ifdef BITS_64
typedef uint64_t addr_t;
#else
typedef uint32_t addr_t;
#endif

extern addr_t __DTOR_LIST__;
extern "C" {

__attribute__((section(".fini")))
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

}
