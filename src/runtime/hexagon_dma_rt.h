/*!
 * This file contains the prototypes of the Halide hexagaon DMA transfer runtime functions
 * These functions will use the parameters passed by the user through Halide pipeline and populate
 * the Hexagon dma context structure, the details in this structure will ultimately be used by hexagon
 * dma device interface for invoking actual DMA transfer
 */

#ifndef _DMA_HALIDE_RT_H_
#define _DMA_HALIDE_RT_H_


#ifdef __cplusplus
extern "C" {
#endif

/*!
 * halide_hexagon_dmart_set_dma_handle
 *in: handle DMA Handle allocated for the frame
 *out: Error/Success
 */
extern int halide_hexagon_dmart_set_dma_handle(void* user_context, void *handle, uintptr_t frame);

/*!
 * halide_hexagon_dmart_set_chroma_stride
 * in: stride set the chroma stride for the frame
 * out: Error/Success
 */
extern int halide_hexagon_dmart_set_chroma_stride(void* user_context, uintptr_t frame, int stride);

/*!
 * halide_hexagon_dmart_set_luma_stride
 * in: stride Set the luma stride for the frame
 * out: Error/Success
 */
extern int halide_hexagon_dmart_set_luma_stride(void* user_context, uintptr_t frame, int stride);

/*!
 * halide_hexagon_dmart_set_fold_storage
 * in: addr_t Cache region
 * in: qurt_size_t Size of the fold
 * in: addr_t Descriptor virtual address of the fold
 * in: addr_t: Descriptor memory region
 * in: qurt_size_t Descriptor size
 * out: fold_id fold id
 * out: Error/Success
 */
extern int halide_hexagon_dmart_set_fold_storage(void* user_context, uintptr_t addr, uintptr_t tcm_region,
             qurt_size_t size, uintptr_t desc_va, uintptr_t desc_region, qurt_size_t desc_size, int *fold_id);

/*!
 * halide_hexagon_dmart_get_update_params
 * in: addr_t fold address
 * out: dma_tMoveParams Parameters to update for DMA Transfer
 * out: Error/Success
 */
extern int halide_hexagon_dmart_get_update_params(void* user_context, uintptr_t dev_buf , t_dma_move_params* update_param);

/*!
 * halide_hexagon_dmart_get_tcmDesc_params
 * in: addr_t dev_buf
 * out: addr_t cache address
 * out: qurt_size_t cache size
 * out: addr_t descriptor virtual address
 * out: addr_t
 * out: qurt_size_t descriptor size
 * out: addr_t descriptor
 * out: Error/Success
 */
extern int halide_hexagon_dmart_get_tcm_desc_params(void* user_context, uintptr_t dev_buf, uintptr_t *tcm_region,
                         qurt_size_t *tcm_size, uintptr_t *desc_va, uintptr_t *desc_region, qurt_size_t *desc_size);

/*halide_hexagon_dmart_get_last_frame
* in: addr_t frame
* out: bool lastFrame
* out: Error/Success
*/
extern int halide_hexagon_dmart_get_last_frame(void* user_context, uintptr_t frame, bool *last_frame);
/*getter Functions*/

/*!
 * halide_hexagon_dmart_is_buffer_read
 * in: addr_t frame
 * out: bool read_flag
 * out: Error/Success
 */
extern int halide_hexagon_dmart_is_buffer_read(void* user_context, uintptr_t frame, bool *read_flag);

/*halide_hexagon_dmart_get_fold_size
* in: addr_t frame
* out: unsigned int * size
* out: Error/Success
*/
extern int halide_hexagon_dmart_get_fold_size(void* user_context, uintptr_t frame, unsigned int *size);

/*!
 * halide_hexagon_dmart_allocate_dma
 * in: addr_t frame
 * out: bool dma_allocate
 * out: Error/Success
 */
extern int halide_hexagon_dmart_allocate_dma(void* user_context, uintptr_t frame, bool *dma_allocate);

/*!
 * halide_hexagon_dmart_get_read_handle
 * in: addr_t frame
 * out: void* handle
 */
extern void* halide_hexagon_dmart_get_read_handle(void* user_context, uintptr_t frame);

/*!
 * halide_hexagon_dmart_get_write_handle
 * in: addr_t frame
 * out: void* handle
 */
extern void* halide_hexagon_dmart_get_write_handle(void* user_context, uintptr_t frame);

/*!
 * halide_hexagon_dmart_get_free_fold
 *out: bool free fold
 * out: fold_id
 * out: int
 */
extern int halide_hexagon_dmart_get_free_fold (void* user_context, bool *free_fold, int* store_id);

/*!
 * halide_hexagon_dmart_get_frame_index
 * in: addr_t frame
 * out: frameIDx
 */
extern int halide_hexagon_dmart_get_frame_index(void *user_context, uintptr_t frame, int *frame_idx);

/*!
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
 * inform dma it is last frame of the session
 */
extern int halide_hexagon_dmart_set_host_frame (void* user_context, uintptr_t  frame,int type, int d,
                                               int w, int h, int s, int last);



/*!
 * The Function will return the Number of Components (Planes ) in the Frame
 * will retun 1 - for Y plane
 * will retun 1 - for UV plane
 * will retun 2 - for both Y and UV planes
 */
extern int halide_hexagon_dmart_get_num_components (void* user_context, uintptr_t  frame, int *ncomponents);


/*!
 *  _DmaRT_SetContext
 *
 * set DMA context from hexagon context
 * dma_context = DMA context
 */
extern int halide_hexagon_dmart_set_context (void* user_context, void* dma_context);

/*!
 *  _DmaRT_GetContext
 *
 * get DMA context from hexagon context
 * dma_context = DMA context
 */
extern int halide_hexagon_dmart_get_context (void* user_context, void** dma_context);

#ifdef __cplusplus
}
#endif

#endif /* _DMA_HALIDE_RT_H_ */
