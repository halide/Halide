/**
 * This file has the necessary structures and APIs for Initiating, executing and finishing a Hexagon DMA transfer
 * The functions in this header file are the interfacing functions between Halide runtime and the Hexagon DMA driver
 * The functions in this file lead to the hexagon DMA driver calls in case of availability of DMA driver and hexagon DMA tools
 * In case of un availabilty of hexagon SDK tools and DMA drivers these function while mimic DMA with dummy transfers  */

#ifndef _DMA_DEVICE_SHIM_H_
#define _DMA_DEVICE_SHIM_H_

#include "hexagon_mini_dma.h"
#include "mini_qurt.h"

#ifdef __cplusplus
extern "C" {
#endif

__inline static int align(int x,int a) {
    return ( (x+a-1) & (~(a-1)) );    
} 

/**
 * Params needed for Prepare for transfer
 */
typedef struct {
    void* handle;
    uintptr_t host_address;
    int frame_width;
    int frame_height;
    int frame_stride;
    int roi_width;
    int roi_height;
    int luma_stride;
    int chroma_stride;
    bool read;
    t_eDmaFmt chroma_type;
    t_eDmaFmt luma_type;
    int ncomponents;
    bool padding;
    bool is_ubwc;
    int num_folds;
    uintptr_t desc_address;
    int desc_size;
} t_dma_prepare_params;

/**
 * Params needed to move data
 */
typedef struct {
    void* handle;
    int xoffset;
    int yoffset;
    int roi_width;
    int roi_height;
    int offset;
    int l2_chroma_offset;
    int ncomponents;
    uintptr_t ping_buffer;
} t_dma_move_params;

/**
 * Params for alignment of roi and frame
 */
typedef struct {
    int u16W;
    int u16H;
} t_dma_pix_align_info;

/**
 * Check for DMA Driver Availability
 * out: ERR if not available
 */
int dma_is_dma_driver_ready();

/**
 * Get Format Alignment
 * desc: check if the frame is aligned */
int dma_get_format_alignment(t_eDmaFmt fmt, bool is_ubwc, t_dma_pix_align_info &pix_align);


/**
 * dma_lookup_physical_address
 * in: uintptr_t virtual adrress
 * out: uintptr_t physical address
 * desc: look up the physical address
 */
uintptr_t dma_lookup_physical_address(uintptr_t vaddr);

/**
 *  Get Minimum ROI Size
 * in:t_eDmaFmt
 * in:bool UBWC Type
 * out:dma_tPixAlignInfo
 * out:int ERR if not aligned
 * desc: check if roi size is aligned
 */
int dma_get_min_roi_size(t_eDmaFmt fmt, bool is_ubwc, t_dma_pix_align_info &pix_align);

/**
 *  Allocate DMA Engine
 * in: t_EDma_WaitType waitType
 * out:void* dmaHandle;
 * allocate dma engtines
 */
void* dma_allocate_dma_engine();

/**
 * Get Descriptor Size
 * in:t_eDmaFmt* fmtType
 * out:qurt_size_t
 * get the descriptor size
 */
qurt_size_t dma_get_descriptor_size(t_eDmaFmt* fmtType, int ncomponents, int nfolds);

/**
 * Get Stride
 * in:t_eDmaFmt fmtType
 * in:bool isUBWC
 * in: dma_tPixAlignInfo roiDims
 * out: lumaStride
 * get luma and chroma stride from dma 
 */
int dma_get_stride(t_eDmaFmt, bool is_ubwc, t_dma_pix_align_info roi_dims);

/**
 * Get Memory Pool ID
 * out:qurt_mem_pool_t*
 * out:int ERR if not available
 * qurt call for alloxation of l2 cache
 */
int dma_get_mem_pool_id(qurt_mem_pool_t* pool_tcm);

/**
 * Allocate Cache for DMA
 * in:qurt_mem_pool_t pool_tcm
 * in:qurt_size_t cache_size
 * out:uintptr_t
 * allocate cache for tcm and descriptors
 */
uintptr_t dma_allocate_cache(qurt_mem_pool_t pool_tcm, qurt_size_t cache_size, uintptr_t* region_tcm);

/**
 * Lock Cache for DMA
 * in:qurt_addr_t
 * in:qurt_size_t
 * out: ERR if not available
 * lock cache
 */
int dma_lock_cache(uintptr_t cache_addr, qurt_size_t cache_size);

/**
 *  dma Prepare for Transfer
 * in:dma_tPrepareParams
 * out: Err if error occurs
 * prepare dma for tramsfer
 */
int dma_prepare_for_transfer(t_dma_prepare_params params);

/**
 *  Blocks new ops till other DMA operations are finished
 * in:void*
 * need to sync with the dma 
 */
int dma_wait(void* handle);

/**
 *  DMA Move Data
 * in:dma_tMoveParams moveParams
 * out:return ERR/OK
 * actual data transfer
 */
int dma_move_data(t_dma_move_params params);

/**
 * Unlock Cache for DMA
 * in:qurt_addr_t
 * in:qurt_size_t
 * out: ERR if not available
 * unlock cache 
 */
int dma_unlock_cache(uintptr_t cache_addr, qurt_size_t cache_size);

/**
 *  dma_free_dma_engine
 * Free DMA
 * in: handle
 * out: Error/Success
 * free the dma engine
 */
int dma_free_dma_engine(void* handle);

/**
 *  dma_finish_frame
 * in: handle
 * out: ERR
 * signal the end of frame
 */
int dma_finish_frame(void* handle);

/**
 *  dma_delete_mem_region
 * in: qurt_mem_region_t
 * do the deletion
 */
void dma_delete_mem_region(uintptr_t tcm_reg);

/**
 * dma_get_thread_id
 * out: unsigned int
 */
unsigned int dma_get_thread_id();

#ifdef __cplusplus
}
#endif

#endif
