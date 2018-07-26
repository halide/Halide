#ifndef _HEXAGON_DMA_POOL_H_
#define _HEXAGON_DMA_POOL_H_

<<<<<<< HEAD
namespace Halide { namespace Runtime { namespace Internal { namespace Hexagon {

typedef struct hexagon_local_cache {
    void *l2memory;
    bool used;
    size_t bytes;
    struct hexagon_local_cache *next; 
} hexagon_cache_pool_t;

typedef hexagon_cache_pool_t* pcache_pool;
//Global Variables
WEAK pcache_pool hexagon_cache_pool = NULL;
WEAK halide_mutex hexagon_cache_mutex;

}}}}

=======
>>>>>>> origin/hex-dma2
#ifdef __cplusplus
extern "C" {
#endif

<<<<<<< HEAD
=======
WEAK void *halide_hexagon_allocate_dma_resource(void *user_context);
WEAK void *halide_hexagon_allocate_from_dma_pool(void *user_context, void* virtual_engine_id);
WEAK int halide_hexagon_free_to_dma_pool(void *user_context, void* dma_engine, void* virtual_engine_id);
WEAK int halide_hexagon_free_dma_resource(void *user_context, void *virtual_engine_id);
>>>>>>> origin/hex-dma2
WEAK void *halide_locked_cache_malloc(void *user_context, size_t size); 

WEAK void halide_locked_cache_free(void *user_context, void *ptr); 

WEAK int halide_hexagon_allocate_l2_pool(void *user_context); 

WEAK int halide_hexagon_free_l2_pool(void *user_context); 

#ifdef __cplusplus
}
#endif

#endif
