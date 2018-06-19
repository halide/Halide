#ifndef HALIDE_HALIDERUNTIMEHEXAGONDMA_H
#define HALIDE_HALIDERUNTIMEHEXAGONDMA_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file
 *  Routines specific to the Halide Hexagon host-side runtime.
 */

extern const struct halide_device_interface_t *halide_hexagon_dma_device_interface();

/** The device handle for Hexagon is simply a pointer and size, stored
 * in the dev field of the buffer_t. If the buffer is allocated in a
 * particular way (ion_alloc), the buffer will be shared with Hexagon
 * (not copied). The device field of the buffer_t must be NULL when this
 * routine is called. This call can fail due to running out of memory
 * or being passed an invalid device handle. The device and host
 * dirty bits are left unmodified. */
extern int halide_hexagon_dma_device_wrap_native(void *user_context, struct halide_buffer_t *buf,
                                                 uint64_t mem);

/** Disconnect this halide_buffer_t from the device handle it was
 * previously wrapped around. Should only be called for a
 * halide_buffer_t that halide_hexagon_wrap_device_handle was
 * previously called on. Frees any storage associated with the binding
 * of the halide_buffer_t and the device handle, but does not free the
 * device handle. The device field of the halide_buffer_t will be NULL
 * on return. */
extern int halide_hexagon_dma_device_detach_native(void *user_context, struct halide_buffer_t *buf);

/** Before a buffer can be used in a copy operation (i.e. a DMA
 * operation), it must have a DMA engine allocated. */
///@{
extern int halide_hexagon_dma_allocate_engine(void *user_context, void ** dma_engine);
extern int halide_hexagon_dma_deallocate_engine(void *user_context, void *dma_engine);
extern int halide_hexagon_dma_prepare_for_copy_to_host(void *user_context, struct halide_buffer_t *buf,
                                                       void *dma_engine, bool is_ubwc, int fmt);
extern int halide_hexagon_dma_prepare_for_copy_to_device(void *user_context, struct halide_buffer_t *buf,
                                                       void *dma_engine, bool is_ubwc, int fmt);
extern int halide_hexagon_dma_unprepare(void *user_context, struct halide_buffer_t *buf);
extern int halide_hexagon_dma_allocate_l2_pool(void *user_context);
extern int halide_hexagon_dma_free_l2_pool(void *user_context);
///@}

#ifdef __cplusplus
} // End extern "C"
#endif

#endif // HALIDE_HALIDERUNTIMEHEXAGONDMA_H
