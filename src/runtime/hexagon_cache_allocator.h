#ifndef _HEXAGON_CACHE_ALLOCATOR_H
#define _HEXAGON_CACHE_ALLOCATOR_H

namespace Halide { namespace Runtime { namespace Internal { 

typedef struct hexagon_cache_block {
    void *l2memory;
    uint8_t bytes;
    bool used;
    struct hexagon_cache_block *next;
} hexagon_cache_pool_t;

typedef hexagon_cache_pool_t *pcache_pool;

extern "C" {
WEAK void hexagon_cache_pool_free (void *user_context);
}

}}}

#endif







