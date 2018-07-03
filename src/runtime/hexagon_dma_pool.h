// API Hidden from user apps
#ifndef _HEXAGON_DMA_POOL_H_
#define _HEXAGON_DMA_POOL_H_

#ifdef __cplusplus
extern "C" {
#endif

WEAK void* halide_hexagon_dma_allocate_pool(void *user_context, int num);
WEAK int halide_hexagon_free_dma_pool(void *user_context, void *pool_addr);
WEAK void *halide_hexagon_allocate_from_dma_pool(void *user_context, void *pool_addr);
WEAK int halide_hexagon_free_from_dma_pool(void *user_context, void *pool_addr, void *engine_addr);

#ifdef __cplusplus
}
#endif

#endif
                   
