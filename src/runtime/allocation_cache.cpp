#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "scoped_mutex_lock.h"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK bool halide_reuse_device_allocations_flag = false;

WEAK halide_mutex allocation_pools_lock;
WEAK halide_device_allocation_pool *device_allocation_pools = NULL;

}}}

extern "C" {

WEAK int halide_reuse_device_allocations(void *user_context, bool flag) {
    __atomic_store_n(&halide_reuse_device_allocations_flag, flag, __ATOMIC_RELEASE);

    int err = 0;
    if (!flag) {
        ScopedMutexLock lock(&allocation_pools_lock);
        for (halide_device_allocation_pool *p = device_allocation_pools; p != NULL; p = p->next) {
            int ret = p->release_unused(user_context);
            if (ret) {
                err = ret;
            }
        }
    }
    return err;
}

/** Determines whether on device_free the memory is returned
 * immediately to the device API, or placed on a free list for future
 * use. Override and switch based on the user_context for
 * finer-grained control. By default just returns the value most
 * recently set by the method above. */
WEAK bool halide_can_reuse_device_allocations(void *user_context) {
    return __atomic_load_n(&halide_reuse_device_allocations_flag, __ATOMIC_ACQUIRE);
}

WEAK void halide_register_device_allocation_pool(struct halide_device_allocation_pool *pool) {
    ScopedMutexLock lock(&allocation_pools_lock);
    pool->next = device_allocation_pools;
    device_allocation_pools = pool;
}

}
