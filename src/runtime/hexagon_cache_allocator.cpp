#include "HalideRuntime.h"
#include "mini_hexagon_dma.h"
#include "HalideRuntimeHexagonDma.h"

namespace Halide { namespace Runtime { namespace Internal {

typedef struct hexagon_cache_block {
    void *l2memory;
    size_t bytes;
    bool used;
    struct hexagon_cache_block *next;
} hexagon_cache_pool_t;

typedef hexagon_cache_pool_t *pcache_pool;

static pcache_pool hexagon_cache_pool = NULL;
halide_mutex hexagon_cache_mutex; 

}}}

using namespace Halide::Runtime::Internal;

extern "C" {
//TO DO Add hjalide_mutex_lock to make it thread sahe 
static inline void *hexagon_cache_pool_get (void *user_context, size_t size) {
    // TODO: Add Mutex locking for access to hexagon_cache_pool ( To be thread safe )
    pcache_pool temp = hexagon_cache_pool;
    pcache_pool prev = NULL;

    // Walk the free list for chunk of memory.
    //Simplistic approach if free block size is less than the size requested assign
    while (temp != NULL) {
        if ((temp->used == false) &&  
            (size == temp->bytes)) {
            halide_mutex_lock(&hexagon_cache_mutex);
            temp->used = true;
            halide_mutex_unlock(&hexagon_cache_mutex);
            return (void *)temp->l2memory;
        }
        prev = temp;
        temp = temp->next;
    }

    // If we are still here that means temp was null.
    temp = (pcache_pool) malloc(sizeof(hexagon_cache_pool_t));
    if (temp == NULL) {
        halide_print(user_context, "Hexagon Cache Pool Allocation Failed.\n");     
        return NULL;
    }
    uint8_t *mem = (uint8_t *)HAP_cache_lock(sizeof(char) * size, NULL);
    if (mem == NULL) {
        //TODO Here need to add code if lot of small size cache allocs are there
        // deallocate and try to allocate again.
        halide_print(user_context, "HAP_cache_lock failed, try deallocating unused cache.\n");
        //Walk the list and deallocate unused blocks
        pcache_pool temp2 = hexagon_cache_pool;
        pcache_pool prev_node = hexagon_cache_pool;
        halide_mutex_lock(&hexagon_cache_mutex);
        while (temp2 != NULL) {
            if (temp2->used == false) {
                HAP_cache_unlock(temp2->l2memory);
                //set previous node details
                prev_node->next = (temp2->next)->next;
                prev_node = temp2->next;
                //set head
                if (temp2 == hexagon_cache_pool) {
                    hexagon_cache_pool = temp2->next;
                 }
                 //free node and reassign the variable
                 free(temp2);
                 temp2 = prev_node;
                }
                prev_node = temp2;
                temp2 = temp2->next;
        }
        halide_mutex_unlock(&hexagon_cache_mutex);
        //Retry one more time after deallocating unused nodes
        mem = (uint8_t *)HAP_cache_lock(sizeof(char) * size, NULL);
        prev = prev_node;
        if (mem == NULL) {
            free(temp);
            halide_print(user_context, "HAP_cache_lock failed.\n");
            return NULL;
        }
    }
    temp->l2memory = (void *)mem;
    temp->bytes = size;
    temp->used = true;

    halide_mutex_lock(&hexagon_cache_mutex);
    if (prev != NULL) {
        prev->next = temp;
    } else if (hexagon_cache_pool == NULL) {
        hexagon_cache_pool = temp;
    }
    halide_mutex_unlock(&hexagon_cache_mutex);

    return (void*) temp->l2memory;
}

static inline void hexagon_cache_pool_put(void *user_context, void *cache_mem) {
    halide_assert(user_context, cache_mem);
    pcache_pool temp = hexagon_cache_pool;
    bool found = false; 
    while (!found && (temp != NULL)) {
        if (temp->l2memory == cache_mem) {
            halide_mutex_lock(&hexagon_cache_mutex);
            temp->used = false;
            halide_mutex_unlock(&hexagon_cache_mutex); 
            found = true;
        }
        temp = temp->next;
    }
}

static inline void hexagon_cache_pool_free(void *user_context) {
    // TODO: Add Mutex locking for access to hexagon_free_pool ( To be Thread safe )
    pcache_pool temp = hexagon_cache_pool;
    halide_mutex_lock(&hexagon_cache_mutex);
    while (temp != NULL) {
        if (temp->l2memory != NULL) {
            HAP_cache_unlock(temp->l2memory);
        }
        temp = temp->next;
        free(hexagon_cache_pool);
        hexagon_cache_pool = temp;
    }
    halide_mutex_unlock(&hexagon_cache_mutex);
}

WEAK void *halide_hexagon_allocate_from_l2_pool(void *user_context, size_t size) {
    halide_print(user_context, "halide_hexagon_allocate_from_l2_pool\n");
    return hexagon_cache_pool_get(user_context, size);
}

WEAK void halide_hexagon_free_from_l2_pool(void *user_context, void *ptr) {
    halide_print(user_context, "halide_hexagon_free_from_l2_pool.\n");
    hexagon_cache_pool_put(user_context, ptr);
}

WEAK int halide_hexagon_allocate_l2_pool(void *user_context) {
   //TODO not sure what is required to be done here ?
   halide_print(user_context, "halide_hexagon_allocate_l2_pool \n");
   return halide_error_code_success;
}

WEAK void halide_hexagon_free_l2_pool(void *user_context) {
    halide_print(user_context, "halide_hexagon_free_l2_pool \n");
    hexagon_cache_pool_free(user_context);
    return;
}

}

