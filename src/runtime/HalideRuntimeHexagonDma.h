#ifndef HALIDE_HALIDERUNTIMEHEXAGONDMA_H
#define HALIDE_HALIDERUNTIMEHEXAGONDMA_H


#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum halide_hexagon_dma_user_fmt{
	NV12,
	UBWC_NV12,
	P010,
	TP10,
	NV124R,
	UBWC_NV124R,
}halide_hexagon_dma_user_fmt_t;

typedef enum halide_hexagon_user_component{
	LUMA_COMPONENT,                /*frame has only Luma component*/
	CHROMA_COMPONENT,              /*frame has only Chroma Component*/
	BOTH_LUMA_CHROMA               /*frame has both Luma nad chroma components */

}halide_hexagon_dma_user_component_t;

/** \file
 *  Routines specific to the Halide Hexagon dma runtime.
 */

typedef int halide_hexagon_dma_handle_t;

extern const struct halide_device_interface_t *halide_hexagon_dma_device_interface();

/* CreateContext check if DMA Engines are available
 * Allocates Memory for a DMA Context
 * Returns Error if DMA Context is not available
 */
extern int halide_hexagon_dmaapp_create_context(void** user_context, int nFrames);

/* Attach Context checks if the frame width, height and type is aligned with DMA Transfer
 * Returns Error if the frame is not aligned.
 * AttachContext needs to be called for each frame
 */
extern int halide_hexagon_dmaapp_attach_context(void* user_context, addr_t  frame, int type, int d, int w, int h, int s, int last);

/* Detach Context signals the end of frame
* This call is used when user has more frames to process
* and does not want to free the DMA Engines
* Return an Error if there is an error in DMA Transfer
*/
extern int halide_hexagon_dmaapp_detach_context(void* user_context, addr_t  frame);

/* Delete Context frees up the dma handle if not yet freed
 * and deallocates memory for the userContext
 */
extern int halide_hexagon_dmaapp_delete_context(void* user_context);

/**************************************************************************
 * Runtime Functions
 * ************************************************************************
 */
/* _DmaRT_SetComponent
*
* set which component to dma
* *frame = VA of frame buffer
* plane = Y/UV*/
extern int halide_hexagon_dmart_set_component (void* user_context, addr_t  frame, int plane);

/* _DmaRT_SetPadFlag
*
* set for dma padding in L2$ (8bit in DDR, 16bit in L2$) - padding '0' to LSb
* *frame = VA of frame buffer
* flag = 0:no padding, 1:padding*/
extern int halide_hexagon_dmart_set_padding (void* user_context, addr_t  frame, int flag);

/* _DmaRT_SetParallel **optional depending on thread scope**
*
* is parallel processing (parallization of inner most loop only; must avoid nested parallelism)
* threads = number of SW threads (one thread per Halide tile)
* This may not be needed*/
extern int halide_hexagon_dmart_set_parallel (void* user_context, int threads);


/* _DmaRT_SetStorageTile
*
* specify the largest folding storage size to dma tile and folding instances; to be used when malloc of device memory (L2$)
* *frame = VA of frame buffer
* w = fold width in pixels
* h = fold height in pixels
* s = fold stride in pixels
* n = number of folds (circular buffers)*/
extern int halide_hexagon_dmart_set_max_fold_storage (void* user_context, addr_t  frame, int h, int w, int s, int n);


/* _DmaRT_SetStorageLinkage
*
* associate host frame to device storage - one call per frame
* *frame = VA of frame buffer
* *fold = VA of device storage
* *store_id = Fold storage id*/
extern int halide_hexagon_dmart_set_storage_linkage (void* user_context, addr_t  frame, addr_t  fold, int store_id);


/* _DmaRT_SetLockResource and _DmaRT_SetUnlockResource **optional depending on thread scope**
*
* lock dma resource set to thread, max number of resource set is based on available HW threads
* lock = 0:unlock, 1:lock
* *rsc_id = lock outputs id value, unlock inputs id value
* This may not be needed*/
extern int halide_hexagon_dmart_set_resource (void* user_context, int lock, int* rsc_id);


/* _DmaRT_SetDeviceFold
*
* set the offset into folding device storage to dma - one call for each frame, per transaction
* store_id = storage id, pin point the correct folding storage
* offset = offset from start of L2$ (local folding storage) to dma - to id the fold (circular buffer)
* rcs_id = locked resource ID **optional** */
extern int halide_hexagon_dmart_set_device_storage_offset (void* user_context, addr_t dev_buf, int offset, int rsc_id);


/* _DmaRT_SetHostROI
*
* set host ROI to dma
* store_id = storage id, pin point the correct folding storage
* x = ROI start horizontal position in pixels
* y = ROI start vertical position in pixels
* w = ROI width in pixels
* h = ROI height in pixels
* rsc_id = locked resource ID **optional**  */
extern int halide_hexagon_dmart_set_host_roi (void* user_context, addr_t dev_buf, int x, int y, int w, int h, int rsc_id);

/* _DmaRT_ClrHostFrame
*
* clear frame
 *frame = VA of frame buffer*/
extern int halide_hexagon_dmart_clr_host_frame (void* user_context, addr_t  frame);


#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEHEXAGONHOST_H
