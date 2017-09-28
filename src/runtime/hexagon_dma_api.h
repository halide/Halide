/**This file contains the header for backend implementation
* In a way it can be considered adaptor class */
#ifndef _HEXAGON_DMA_API_H
#define _HEXAGON_DMA_API_H

/**
 * Error Codes */
typedef enum {
    HEX_SUCCESS = 0,
    HEX_ERROR = -1
} t_dma_error_codes;

/**
 * DmaContext
 * global type of dma context
 */
typedef struct t_dma_context* dma_context;

namespace Halide { namespace Runtime { namespace Internal { namespace Dma {

//Helper class to pass DMA Context
class HexagonDmaContext {
    void* user_ctxt;

public:
    dma_context context;

    HexagonDmaContext(void* user_context);
    HexagonDmaContext(void* user_context, int num_of_frames);
    ~HexagonDmaContext();
};

}}}}

/**
 * Mem Description
 */
typedef struct {
    uintptr_t tcm_region;
    uintptr_t desc_region;
    uintptr_t tcm_vaddr;
    uintptr_t desc_vaddr;
    qurt_size_t tcm_size;
    qurt_size_t desc_size;
} t_mem_desc;

/**
 *  Table to Get DMA Chroma format for the user type selected
 */
const t_eDmaFmt type2_dma_chroma[6]={eDmaFmt_NV12_UV, eDmaFmt_NV12_UV, eDmaFmt_P010_UV, eDmaFmt_TP10_UV, eDmaFmt_NV124R_UV, eDmaFmt_NV124R_UV};

/**
 *  Table to Get DMA Luma format for the user type selected
 */
const t_eDmaFmt type2_dma_luma[6]={eDmaFmt_NV12_Y, eDmaFmt_NV12_Y, eDmaFmt_P010_Y, eDmaFmt_TP10_Y, eDmaFmt_NV124R_Y, eDmaFmt_NV124R_Y};

/**
 * Table for accounting the dimension for various Formats
 */
const float type_size[6]={1, 1, 1, 0.6667, 1, 1};

/**
 * halide_hexagon_dma_comp_get
 * get the component tobe transferreed from roi buf
 */
extern int halide_hexagon_dma_comp_get(void *user_context, halide_buffer_t *roi_buf, halide_hexagon_dma_user_component_t &comp);

/**
 * halide_hexagon_dma_memory_alloc
 * desc: set the comp, width, height, stride, num_folds, padding, type
 * allocate memory
 */
extern void* halide_hexagon_dma_memory_alloc(void* user_context, dma_context pdma_context, halide_hexagon_dma_user_component_t comp,
                                                 int w, int h, int s, int num_fold, bool padding, int type);

/**
 * halide_hexagon_dma_memory_free
 * in: dma_context
 * in: cache mem description
 * free the memory allocations
 */
extern int halide_hexagon_dma_wrap_memory_free(void* user_context, dma_context pdma_context, void* vret);

/**
 * halide_hexagon_dma_memroy_get
 * out: l2 buffer
 * in: roi_buf
 * check if memory is already allocated
 */
extern void *halide_hexagon_dma_memory_get(void *user_context, halide_buffer_t* roi_buf);

/**
 * halide_hexagon_dma_update
 * in: inframe buf
 * in: roi buf
 * prepare the dma for transfer 
 */
extern int halide_hexagon_dma_update(void *user_context, halide_buffer_t *inframe_buf, halide_buffer_t *roi_buf);

#endif // _HEXAGON_DMA_API_H
