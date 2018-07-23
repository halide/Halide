#ifndef _HEXAGON_DMA_POOL_H_
#define _HEXAGON_DMA_POOL_H_

namespace Halide { namespace Runtime { namespace Internal { namespace Hexagon {
//Global Variables
WEAK void *hexagon_cache_pool = NULL;
}}}}

#ifdef __cplusplus
extern "C" {
#endif

WEAK void *halide_locked_cache_malloc(void *user_context, size_t size); 

WEAK void halide_locked_cache_free(void *user_context, void *ptr); 

WEAK int halide_hexagon_allocate_l2_pool(void *user_context); 

WEAK int halide_hexagon_free_l2_pool(void *user_context); 

#ifdef __cplusplus
}
#endif

#endif
