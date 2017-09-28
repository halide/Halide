/**
 * This file contains the prototypes of the Halide hexagaon DMA transfer runtime functions
 * These functions will use the parameters passed by the user through Halide pipeline and populate
 * the Hexagon dma context structure, the details in this structure will ultimately be used by hexagon
 * dma device interface for invoking actual DMA transfer  */

#ifndef _DMA_HALIDE_RT_H_
#define _DMA_HALIDE_RT_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * halide_hexagon_dmart_set_dma_handle
 *in: handle DMA Handle allocated for the frame
 *out: Error/Success */
extern int halide_hexagon_dmart_set_dma_handle(void* user_context, dma_context pdma_context, void *handle, uintptr_t frame);

/**
 * halide_hexagon_dmart_set_chroma_stride
 * in: stride set the chroma stride for the frame
 * out: Error/Success */
extern int halide_hexagon_dmart_set_chroma_stride(void* user_context, dma_context pdma_context, uintptr_t frame, int stride);

/**
 * halide_hexagon_dmart_set_luma_stride
 * in: stride Set the luma stride for the frame
 * out: Error/Success */
extern int halide_hexagon_dmart_set_luma_stride(void* user_context, dma_context pdma_context, uintptr_t frame, int stride);

/**
 * halide_hexagon_dmart_set_fold_storage
 * in: addr_t Cache region
 * in: qurt_size_t Size of the fold
 * in: addr_t Descriptor virtual address of the fold
 * in: addr_t: Descriptor memory region
 * in: qurt_size_t Descriptor size
 * out: fold_id fold id
 * out: Error/Success */
extern int halide_hexagon_dmart_set_fold_storage(void* user_context, dma_context pdma_context, uintptr_t addr, uintptr_t tcm_region, \
                                                 qurt_size_t size, uintptr_t desc_va, uintptr_t desc_region, \
                                                 qurt_size_t desc_size, int *fold_id);

/**
 * halide_hexagon_dmart_get_update_params
 * in: addr_t fold address
 * out: dma_tMoveParams Parameters to update for DMA Transfer
 * out: Error/Success  */
extern int halide_hexagon_dmart_get_update_params(void* user_context, dma_context pdma_context, uintptr_t dev_buf, t_dma_move_params* update_param);

/**
 * halide_hexagon_dmart_get_prepare_params
 * in: addr_t frame address
 * out: dma_PrepareParams Parameters to update for DMA Transfer
 * out: Error/Success  */
extern int halide_hexagon_dmart_get_prepare_params(void* user_context, dma_context pdma_context, uintptr_t frame, t_dma_prepare_params* prepare_param);

/**
 * halide_hexagon_dmart_get_tcmDesc_params
 * in: addr_t dev_buf
 * out: addr_t cache address
 * out: qurt_size_t cache size
 * out: addr_t descriptor virtual address
 * out: addr_t
 * out: qurt_size_t descriptor size
 * out: addr_t descriptor
 * out: Error/Success */
extern int halide_hexagon_dmart_get_tcm_desc_params(void* user_context, dma_context pdma_context, uintptr_t dev_buf, uintptr_t *tcm_region,
                                                    qurt_size_t *tcm_size, uintptr_t *desc_va, uintptr_t *desc_region, \
                                                    qurt_size_t *desc_size);

/** 
 *halide_hexagon_dmart_get_last_frame
 * in: addr_t frame
 * out: bool lastFrame
 */
extern int halide_hexagon_dmart_get_last_frame(void* user_context, dma_context pdma_context, uintptr_t frame, bool *last_frame);

/*getter Functions*/

/**
 * halide_hexagon_dmart_is_buffer_read
 * in: addr_t frame
 * out: bool read_flag */
extern int halide_hexagon_dmart_is_buffer_read(void* user_context, dma_context pdma_context, uintptr_t frame, bool* is_read);

/**
 * halide_hexagon_dmart_get_fold_size
 * in: addr_t frame
 * out: unsigned int size */
extern int halide_hexagon_dmart_get_fold_size(void* user_context, dma_context pdma_context, uintptr_t frame);

/**
 * halide_hexagon_dmart_allocate_dma
 * in: addr_t frame
 * out: bool dma_allocate
 * out: Error/Success */
extern int halide_hexagon_dmart_allocate_dma(void* user_context, dma_context pdma_context, uintptr_t frame, bool* dma_allocate);

/**
 * halide_hexagon_dmart_get_dma_handle
 * in: addr_t frame
 * out: void* handle */
extern void* halide_hexagon_dmart_get_dma_handle(void* user_context, dma_context pdma_context, uintptr_t frame);

/**
 * halide_hexagon_dmart_get_free_fold
 *out: bool free fold
 * out: fold_id
 * out: int */
extern int halide_hexagon_dmart_get_free_fold (void* user_context, dma_context pdma_context, bool *free_fold, int* store_id);

/**
 * halide_hexagon_dmart_get_frame_index
 * in: addr_t frame
 * out: frameIDx */
extern int halide_hexagon_dmart_get_frame_index(void* user_context, dma_context pdma_context, uintptr_t frame);

/**
 *  halide_hexagon_dmart_set_host_frame
 * set frame type
 * *frame = VA of frame buffer
 * type = NV12/NV124R/P010/UBWC_NV12/TP10/UBWC_NV124R
 * FrameID = FrameID from the Video to distinguish various Frames
 * d = frame direction 0:read, 1:write
 * w = frame width in pixels
 * h = frame height in pixels
 * s = frame stride in pixels
 * last = last frame 0:no 1:yes *optional*
 * inform dma it is last frame of the session */
extern int halide_hexagon_dmart_set_host_frame (void* user_context, dma_context pdma_context, uintptr_t frame,int type, int d,
                                               int w, int h, int s, int last);

/**
 * The Function will return the Number of Components (Planes ) in the Frame
 * will retun 1 - for Y plane
 * will retun 1 - for UV plane
 * will retun 2 - for both Y and UV planes */
extern int halide_hexagon_dmart_get_num_components (void* user_context, dma_context pdma_context, uintptr_t frame);

/**
 *  halide_hexagon_dmart_clr_host_frame
 * clear frame
 * frame = VA of frame buffer  */
extern int halide_hexagon_dmart_clr_host_frame(void* user_context, dma_context pdma_context, uintptr_t frame);

/**
 *  halide_hexagon_dmart_set_storage_linkage
 * associate host frame to device storage - one call per frame
 * frame = VA of frame buffer
 * fold = VA of device storage
 * store_id = Fold storage id  */
extern int halide_hexagon_dmart_set_storage_linkage(void* user_context, dma_context pdma_context, uintptr_t frame, uintptr_t fold, int store_id);

/**
 * halide_hexagon_dmaapp_create_context
 * CreateContext check if DMA Engines are available
 * Allocates Memory for a DMA Context
 * Returns Error if DMA Context is not available  */
extern int halide_hexagon_dmart_create_context(void* user_context, dma_context* pdma_context, int nFrames);

/**
 * halide_hexagon_dmaapp_delete_context
 * Delete Context frees up the dma handle if not yet freed
 * and deallocates memory for the userContext  */
extern int halide_hexagon_dmart_delete_context(void* user_context, dma_context pdma_context);


/**
 * halide_hexagon_dmaapp_attach_context
 * Attach Context checks if the frame width, height and type is aligned with DMA Transfer
 * Returns Error if the frame is not aligned.
 * AttachContext needs to be called for each frame  */
extern int halide_hexagon_dmart_attach_context(void* user_context, dma_context pdma_context, uintptr_t frame, int type,
	                                        int d, int w, int h, int s, int last);

/**
* halide_hexagon_dmaapp_detach_context
* Detach Context signals the end of frame
* This call is used when user has more frames to process
* and does not want to free the DMA Engines
* Return an Error if there is an error in DMA Transfe */
extern int halide_hexagon_dmart_detach_context(void* user_context, dma_context pdma_context, uintptr_t frame);

/**
 *  halide_hexagon_dmart_set_component
 * set which component to dma
 * frame = VA of frame buffer
 * plane = Y/UV  */
extern int halide_hexagon_dmart_set_component(void* user_context, dma_context pdma_context, uintptr_t frame, int plane);

/**
 *  halide_hexagon_dmart_set_padding
 * set for dma padding in L2$ (8bit in DDR, 16bit in L2$) - padding '0' to LSb
 * frame = VA of frame buffer
 * flag = 0:no padding, 1:padding  */
extern int halide_hexagon_dmart_set_padding(void* user_context, dma_context pdma_context, uintptr_t frame, int flag);

/**
 *  halide_hexagon_dmart_set_parallel
 * is parallel processing (parallization of inner most loop only; must avoid nested parallelism)
 * threads = number of SW threads (one thread per Halide tile)
 * This may not be needed */
extern int halide_hexagon_dmart_set_parallel(void* user_context, dma_context pdma_context, int threads);

/**
 *  halide_hexagon_dmart_set_max_fold_storage
 * specify the largest folding storage size to dma tile and
 * folding instances; to be used when malloc of device memory (L2$)
 * frame = VA of frame buffer
 * w = fold width in pixels
 * h = fold height in pixels
 * s = fold stride in pixels
 * n = number of folds (circular buffers) */
extern int halide_hexagon_dmart_set_max_fold_storage(void* user_context, dma_context pdma_context, uintptr_t frame,
                                                     int h, int w, int s, int n);

/**
 *  halide_hexagon_dmart_set_resource
 * lock dma resource set to thread, max number of resource set is based on available HW threads
 * lock = 0:unlock, 1:lock
 * rsc_id = lock outputs id value, unlock inputs id value
 * This may not be needed  */
extern int halide_hexagon_dmart_set_resource(void* user_context, dma_context pdma_context, int lock, int* rsc_id);

/**
 *  halide_hexagon_dmart_set_device_storage_offset
 * set the offset into folding device storage to dma - one call for each frame, per transaction
 * store_id = storage id, pin point the correct folding storage
 * offset = offset from start of L2$ (local folding storage) to dma - to id the fold (circular buffer)
 * rcs_id = locked resource ID **optional**  */
extern int halide_hexagon_dmart_set_device_storage_offset(void* user_context, dma_context pdma_context, uintptr_t dev_buf,
                                                          int offset, int rsc_id);
/**
 *  halide_hexagon_dmart_set_host_roi
 * set host ROI to dma
 * store_id = storage id, pin point the correct folding storage
 * x = ROI start horizontal position in pixels
 * y = ROI start vertical position in pixels
 * w = ROI width in pixels
 * h = ROI height in pixels
 * rsc_id = locked resource ID **optional  */
extern int halide_hexagon_dmart_set_host_roi(void* user_context, dma_context pdma_context, uintptr_t dev_buf, int x, int y,
                                             int w, int h, int rsc_id);

/**
 * halide_hexagon_dmart_get_frame
 * get frame address
 * rsc_id = locked resource ID **optional  */
extern uintptr_t halide_hexagon_dmart_get_frame(void* user_context, dma_context pdma_context);

/**
 * halide_hexagon_dmart_get_fold_addr
 * get fold addr */
extern uintptr_t halide_hexagon_dmart_get_fold_addr(void* user_context, dma_context pdma_context, uintptr_t frame);

/**
 *  halide_hexagon_dmart_get_prepare_params
 *  prepare for dma transfer */
extern int halide_hexagon_dmart_get_prepare_params(void* user_context, dma_context pdma_context, uintptr_t frame, t_dma_prepare_params* prepare_param);

/**
 * halide_hexagon_dmart_get_update
 * if update is already begin  */
extern int halide_hexagon_dmart_get_update(void* user_context, dma_context pdma_context, uintptr_t frame, bool &update);

/**
 * halide_hexagon_dmart_set_update
 * start update  */
extern int halide_hexagon_dmart_set_update(void* user_context, dma_context pdma_context, uintptr_t frame);


#ifdef __cplusplus
}
#endif

#endif /* _DMA_HALIDE_RT_H_ */
