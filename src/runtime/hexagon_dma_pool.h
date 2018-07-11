// API Hidden from user apps
#ifndef _HEXAGON_DMA_POOL_H_
#define _HEXAGON_DMA_POOL_H_

#ifdef __cplusplus
extern "C" {
#endif

WEAK void *halide_hexagon_allocate_dma_resource(void *user_context);
WEAK void *halide_hexagon_allocate_from_dma_pool(void *user_context, void* virtual_engine_id);
WEAK int halide_hexagon_free_to_dma_pool(void *user_context, void* dma_engine, void* virtual_engine_id);
WEAK int halide_hexagon_free_dma_resource(void *user_context, void *virtual_engine_id);
#ifdef __cplusplus
}
#endif

#endif
                   
