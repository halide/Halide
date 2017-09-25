#ifndef _DMA_API_H
#define _DMA_API_H

/**dma_desc_image
 * in frame buf values
 */
typedef struct {
    bool last_frame;
    int width;
    int height;
    int stride;
    int num_of_frames;
    int padding;
    int type;
    bool read;
    int plane;
    unsigned char* buffer;
    uint64_t size;
    int fold_width;
    int fold_height;
    int fold_stride;
} dma_desc_image_t;

/**
 * t_cache_mem
 * roi buf values
 */
typedef struct {
   uintptr_t fold_vaddr;
   int fold_idx;
} cache_mem_t;

/**
 * DmaContext
 * global type of dma context
 */
typedef DmaContext* dma_context;

/**
 * halide_hexagon_dmart_set_context
 * set DMA context 
 **/
extern void halide_hexagon_set_dma_context(void* user_context, dma_context context);

/**
 * halide_hexagon_dmart_get_context
 * get DMA context from hexagon context
 * dma_context = DMA context */
extern void halide_hexagon_get_dma_context (void* user_context, dma_context* context);

#endif
