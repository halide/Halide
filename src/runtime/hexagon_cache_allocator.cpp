#include "HalideRuntime.h"
#include "hexagon_dma_pool.h"
#include "mini_hexagon_dma.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Hexagon {

typedef struct hexagon_local_cache {
    void *l2memory;
    bool used;
    size_t bytes;
    struct hexagon_local_cache *next;
} hexagon_cache_pool_t;

typedef hexagon_cache_pool_t *pcache_pool;

WEAK pcache_pool hexagon_cache_pool = nullptr;
WEAK halide_mutex hexagon_cache_mutex;

}  // namespace Hexagon
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::Hexagon;

namespace {

inline void *free_unused_buffers(void *user_context) {
    // Walk the list and deallocate unused blocks.
    ScopedMutexLock lock(&hexagon_cache_mutex);
    pcache_pool temp2 = hexagon_cache_pool;
    pcache_pool prev_node = hexagon_cache_pool;
    while (temp2 != nullptr) {
        if (temp2->used == false) {
            int err = HAP_cache_unlock(temp2->l2memory);
            if (err != 0) {
                error(user_context) << "Hexagon: HAP_cache_unlock failure on unused free\n";
                return nullptr;
            }
            // Set previous node details.
            prev_node->next = (temp2->next)->next;
            prev_node = temp2->next;
            // Set Head.
            if (temp2 == hexagon_cache_pool) {
                hexagon_cache_pool = temp2->next;
            }
            // Free node and reassign the variable.
            free(temp2);
            temp2 = prev_node;
        }
        prev_node = temp2;
        temp2 = temp2->next;
    }
    return (void *)prev_node;
}

// Retry logic if enabled will walk the list and deallocate unused blocks to make room for a larger block size
// Once all unused blocks are deallocated it will try to allocate a larger block
inline void *hexagon_cache_pool_get(void *user_context, size_t size, bool retry) {

    pcache_pool prev = nullptr;
    pcache_pool temp = hexagon_cache_pool;
    // Walk the list to find free buffer
    {
        ScopedMutexLock lock(&hexagon_cache_mutex);
        while (temp != nullptr) {
            if ((temp->used == false) &&
                (size == temp->bytes)) {
                temp->used = true;
                return (void *)temp->l2memory;
            }
            prev = temp;
            temp = temp->next;
        }
    }

    // If we are still here that means temp was null.
    temp = (pcache_pool)malloc(sizeof(hexagon_cache_pool_t));
    if (temp == nullptr) {
        error(user_context) << "Hexagon: Out of memory (Cache Pool Allocation Failed)\n";
        return nullptr;
    }
    uint8_t *mem = (uint8_t *)HAP_cache_lock(sizeof(char) * size, nullptr);
    if ((mem == nullptr) && retry) {
        pcache_pool prev_node = (pcache_pool)free_unused_buffers(user_context);
        // Retry one more time after deallocating unused nodes.
        mem = (uint8_t *)HAP_cache_lock(sizeof(char) * size, nullptr);
        prev = prev_node;
        if (mem == nullptr) {
            free(temp);
            error(user_context) << "Hexagon: Out of memory (HAP_cache_lock retry failed)\n";
            return nullptr;
        }
    } else if (mem == nullptr) {
        free(temp);
        error(user_context) << "Hexagon: Out of memory (HAP_cache_lock failed)\n";
        return nullptr;
    }
    temp->l2memory = (void *)mem;
    temp->bytes = size;
    temp->used = true;
    temp->next = nullptr;

    {
        ScopedMutexLock lock_obj(&hexagon_cache_mutex);
        if (prev != nullptr) {
            prev->next = temp;
        } else if (hexagon_cache_pool == nullptr) {
            hexagon_cache_pool = temp;
        }
    }
    return (void *)temp->l2memory;
}

inline void hexagon_cache_pool_put(void *user_context, void *cache_mem) {
    ScopedMutexLock lock(&hexagon_cache_mutex);
    halide_abort_if_false(user_context, cache_mem);
    pcache_pool temp = hexagon_cache_pool;
    while (temp != nullptr) {
        if (temp->l2memory == cache_mem) {
            temp->used = false;
            return;
        }
        temp = temp->next;
    }
}

inline halide_error_code_t hexagon_cache_pool_free(void *user_context) {
    ScopedMutexLock lock(&hexagon_cache_mutex);
    pcache_pool temp = hexagon_cache_pool;
    pcache_pool prev = hexagon_cache_pool;
    int err = QURT_EOK;
    while (temp != nullptr) {
        if (temp->l2memory != nullptr) {
            err = HAP_cache_unlock(temp->l2memory);
            if (err != QURT_EOK) {
                error(user_context) << "Hexagon: HAP_cache_unlock failed on pool free";
                return halide_error_code_generic_error;
            }
        }
        prev = temp->next;
        free(temp);
        temp = prev;
    }
    hexagon_cache_pool = nullptr;
    return halide_error_code_success;
}

}  // namespace

extern "C" {

WEAK void *halide_locked_cache_malloc(void *user_context, size_t size) {
    // TODO Currently option to retry allocation is disabled, we will have to decide if can be
    // set by user or pipeline.
    bool retry = false;
    debug(user_context) << "halide_locked_cache_malloc\n";
    // Our caller will check the result for null
    return hexagon_cache_pool_get(user_context, size, retry);
}

WEAK void halide_locked_cache_free(void *user_context, void *ptr) {
    debug(user_context) << "halide_locked_cache_free\n";
    hexagon_cache_pool_put(user_context, ptr);
}

WEAK int halide_hexagon_allocate_l2_pool(void *user_context) {
    // TODO not sure what is required to be done here ?
    debug(user_context) << "halide_hexagon_allocate_l2_pool\n";
    return halide_error_code_success;
}

WEAK int halide_hexagon_free_l2_pool(void *user_context) {
    debug(user_context) << "halide_hexagon_free_l2_pool\n";
    return hexagon_cache_pool_free(user_context);
}
}
