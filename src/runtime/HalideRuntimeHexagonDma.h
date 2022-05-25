#ifndef HALIDE_HALIDERUNTIMEHEXAGONDMA_H
#define HALIDE_HALIDERUNTIMEHEXAGONDMA_H

/** \file
 *  Routines specific to the Halide Hexagon DMA host-side runtime.
 */

// Don't include HalideRuntime.h if the contents of it were already pasted into a generated header above this one
#ifndef HALIDE_HALIDERUNTIME_H

#include "HalideRuntime.h"

#endif

// Don't include HalideRuntimeHexagonHost.h if the contents of it were already pasted into a generated header above this one
#ifndef HALIDE_HALIDERUNTIMEHEXAGONHOST_H

#include "HalideRuntimeHexagonHost.h"

#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup rt_hexagon_dma Halide Hexagon DMA runtime
 * @{
 */

/**
 * Image Formats to prepare the application for DMA Transfer
 */
typedef enum {
    halide_hexagon_fmt_RawData,
    halide_hexagon_fmt_NV12,
    halide_hexagon_fmt_NV12_Y,
    halide_hexagon_fmt_NV12_UV,
    halide_hexagon_fmt_P010,
    halide_hexagon_fmt_P010_Y,
    halide_hexagon_fmt_P010_UV,
    halide_hexagon_fmt_TP10,
    halide_hexagon_fmt_TP10_Y,
    halide_hexagon_fmt_TP10_UV,
    halide_hexagon_fmt_NV124R,
    halide_hexagon_fmt_NV124R_Y,
    halide_hexagon_fmt_NV124R_UV
} halide_hexagon_image_fmt_t;

extern const struct halide_device_interface_t *halide_hexagon_dma_device_interface();

/** This API is used to set up the DMA device interface to be used for DMA transfer. This also internally
 * creates the DMA device handle and populates all the Buffer related parameters (width, height, stride)
 * to be used for DMA configuration.
 */
extern int halide_hexagon_dma_device_wrap_native(void *user_context, struct halide_buffer_t *buf,
                                                 uint64_t mem);

/** Detach the Input/Output Buffer from DMA device handle and deallocate the DMA device handle buffer allocation
 * This API also frees up the DMA device and makes it available for another usage.
 */
extern int halide_hexagon_dma_device_detach_native(void *user_context, struct halide_buffer_t *buf);

/** This API will allocate a DMA Engine needed for DMA read/write. This is the first step Before
 * a buffer can be used in a copy operation (i.e. a DMA read/write operation).
 */
extern int halide_hexagon_dma_allocate_engine(void *user_context, void **dma_engine);

/** This API free up the allocated DMA engine. This need to be called after a user program ends
 * all the DMA Operations and make it available for subsequent DMA transfers */
extern int halide_hexagon_dma_deallocate_engine(void *user_context, void *dma_engine);

/** This API Prepares a buffer for DMA Read Operation. This will setup the DMA format, direction (read).
 * Will also make necessary adjustments to the DMA frame parameters based on Image format provided.
 */
extern int halide_hexagon_dma_prepare_for_copy_to_host(void *user_context, struct halide_buffer_t *buf,
                                                       void *dma_engine, bool is_ubwc, halide_hexagon_image_fmt_t fmt);

/** This API Prepares a buffer for DMA Write Operation. This will setup the DMA format, direction (write).
 * Will also make necessary adjustments to the DMA frame parameters based on Image format provided.
 */
extern int halide_hexagon_dma_prepare_for_copy_to_device(void *user_context, struct halide_buffer_t *buf,
                                                         void *dma_engine, bool is_ubwc,
                                                         halide_hexagon_image_fmt_t fmt);

/** This API is used to frees up the DMA Resources associated with the buffer.
 * TODO: Currently this API is a dummy as all the necessary freeing is done in an another API.
 * This will be used in future.
 */
extern int halide_hexagon_dma_unprepare(void *user_context, struct halide_buffer_t *buf);

/** This API is used to setup the hexagon Operation modes. We will setup the necessary Operating frequency
 * based on the power mode chosen. Check the structure halide_hexagon_power_mode_t defined in Halide HalideRuntimeHexagonHost.h
 * for the supported power modes.
 */
extern int halide_hexagon_dma_power_mode_voting(void *user_context, halide_hexagon_power_mode_t cornercase);

///@}

#ifdef __cplusplus
}  // End extern "C"
#endif

#endif  // HALIDE_HALIDERUNTIMEHEXAGONDMA_H
