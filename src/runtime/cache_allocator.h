#include "HalideRuntime.h"
#include "mini_hexagon_dma.h"

typedef struct cache_pool {
    void *l2memory;
    uint8_t bytes;
    bool used;
    struct cache_pool *next;
} cache_pool_t;

typedef cache_pool_t *pcache_pool;

void *cache_pool_get (void *user_context, size_t size);

void cache_pool_put (void *user_context, void *cache_mem);

void cache_pool_free (void *user_context); 









