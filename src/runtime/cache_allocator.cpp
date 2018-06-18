#include "HalideRuntime.h"
#include "printer.h"
#include "mini_hexagon_dma.h"
#include "cache_allocator.h"

static pcache_pool hexagon_cache_pool = NULL;

void *cache_pool_get (void *user_context, size_t size) {
    // TODO: Add Mutex locking for access to hexagon_cache_pool ( To be thread safe )
    pcache_pool temp = hexagon_cache_pool;
    pcache_pool prev = NULL;

    // Walk the free list for chunk of memory.
    //Simplistic approach if free block size is less than the size requested assign 
    while (temp != NULL) {
        if ((temp->used == false) && 
            (size <= temp->bytes)) {
            temp->used = true;
            return (void *)temp->l2memory;  
        }           
        prev = temp;
        temp = temp->next;
    }   

    // If we are still here that means temp was null.
    temp = (pcache_pool) malloc(sizeof(cache_pool_t));
    if (temp == NULL) {
        error(user_context) << "malloc failed\n";
        return NULL;
    }
    uint8_t *mem = (uint8_t *)HAP_cache_lock(sizeof(char) * size, NULL);
    if (mem == NULL) {
        //TODO Here need to add code if lot of small size cache allocs are there
        // deallocate and try to allocate again. 
        free(temp);
        error(user_context) << "HAP_cache_lock failed\n";
        return NULL;
    }
    temp->l2memory = (void *)mem;
    temp->bytes = size;
    temp->used = true;

    if (prev != NULL) { 
        prev->next = temp;
    } else {
        hexagon_cache_pool = temp;  
    }
    
    return (void*) temp->l2memory;
}

void cache_pool_put(void *user_context, void *cache_mem) {
    halide_assert(user_context, cache_mem);
    pcache_pool temp = hexagon_cache_pool;
    while (temp != NULL) {
        if (temp->l2memory == cache_mem) {
            temp->used = false;
        }
        temp = temp->next;
    }
}

void cache_pool_free(void *user_context) {
    // TODO: Add Mutex locking for access to hexagon_free_pool ( To be Thread safe )
    pcache_pool temp = hexagon_cache_pool;
    while (temp != NULL) {
        if (temp->l2memory != NULL) {
            HAP_cache_unlock(temp->l2memory);
        }
        temp = temp->next;
        free(hexagon_cache_pool);
        hexagon_cache_pool = temp; 
    }
}



