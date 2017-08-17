#ifndef _DMA_HALIDE_RT_H_
#define _DMA_HALIDE_RT_H_

using namespace Halide::Runtime::Internal::Qurt;

#ifdef __cplusplus
extern "C" {
#endif

/*set Functions*/
extern int halide_hexagon_dmart_set_dma_handle(void* user_context, void *handle, addr_t frame);
extern int halide_hexagon_dmart_set_chroma_stride(void* user_context,addr_t frame, int stride);
extern int halide_hexagon_dmart_set_luma_stride(void* user_context,addr_t frame, int stride);
extern int halide_hexagon_dmart_set_fold_storage(void* user_context, addr_t addr,qurt_mem_region_t tcm_region, qurt_size_t size,addr_t desc_va,qurt_mem_region_t desc_region,qurt_size_t desc_size, int *fold_id);
extern int halide_hexagon_dmart_get_update_params(void* user_context, addr_t dev_buf , dma_tMoveParams* updateParam);
extern int halide_hexagon_dmart_get_tcmDesc_params(void* user_context, addr_t dev_buf,qurt_mem_region_t *tcm_region,qurt_size_t *tcm_size, addr_t *desc_va,qurt_mem_region_t *desc_region,qurt_size_t *desc_size);
extern int halide_hexagon_dmart_get_last_frame(void* user_context, addr_t  frame, bool *last_frame);
/*getter Functions*/

extern int halide_hexagon_dmart_isBufferRead(void* user_context,addr_t frame, bool *read_flag);
extern int halide_hexagon_dmart_get_fold_size(void* user_context,addr_t frame, unsigned int *Size);
extern int halide_hexagon_dmart_allocateDMA(void* user_context,addr_t frame, bool *dma_allocate);
extern void* halide_hexagon_dmart_get_readHandle(void* user_context,addr_t frame);
extern void* halide_hexagon_dmart_get_writeHandle(void* user_context,addr_t frame);

/*Get an Index of already Allocated free Fold Storage if exists*/
extern int halide_hexagon_dmart_get_free_fold (void* user_context, bool *free_fold, int* store_id);

/* Get Frame Index of the Frame in the frame context*/
extern int halide_hexagon_dmart_get_frame_index(void *user_context, addr_t frame, int *FrameIdx);

/* _DmaRT_SetHostFrame
*
* set frame type
* *frame = VA of frame buffer
* type = NV12/NV124R/P010/UBWC_NV12/TP10/UBWC_NV124R
* FrameID = FrameID from the Video to distinguish various Frames
* d = frame direction 0:read, 1:write
* w = frame width in pixels
* h = frame height in pixels
* s = frame stride in pixels
* last = last frame 0:no 1:yes *optional*
* inform dma it is last frame of the session, and can start freeing resource when requested at appropriate time (on last SW thread of that resource set)*/
extern int halide_hexagon_dmart_set_host_frame (void* user_context, addr_t  frame,int type, int d, int w, int h, int s, int last);



/*The Function will return the Number of Components (Planes ) in the Frame
* will retun 1 - for Y plane 
* will retun 1 - for UV plane 
* will retun 2 - for both Y and UV planes */
extern int halide_hexagon_dmart_get_num_components (void* user_context, addr_t  frame, int *nComponents);


/* _DmaRT_SetContext
*
* set DMA context from hexagon context
* *dma_context = DMA context*/
extern int halide_hexagon_dmart_set_context (void* user_context, void* dma_context);

/* _DmaRT_GetContext
*
* get DMA context from hexagon context
* **dma_context = DMA context*/
extern int halide_hexagon_dmart_get_context (void* user_context, void** dma_context);

#ifdef __cplusplus
}
#endif

#endif /* _DMA_HALIDE_RT_H_ */
