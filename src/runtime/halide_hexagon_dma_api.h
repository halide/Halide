/**
 *  This file contains the necessary APIs and Structures for Initiating
 * and Terminating the Hexagon DMA transfer from Halide user space
 * The Header is available to the user to pass the necessary details
 * of the Frame and its type so that Halide issue a matching call to DMA driver
 * This File also have APIs to create the DMA context for the First frame and Delete
 * the DMA context for the Last frame
 */
#ifndef HALIDE_HEXAGON_DMA_API_H
#define HALIDE_HEXAGON_DMA_API_H

#include "HalideRuntime.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * User Format currently supported
 */
typedef enum halide_hexagon_dma_user_fmt {
    NV12,
    UBWC_NV12,
    P010,
    TP10,
    NV124R,
    UBWC_NV124R,
} halide_hexagon_dma_user_fmt_t;

/**
 * Component of the frame
 */
typedef enum halide_hexagon_user_component {
    LUMA_COMPONENT,                //frame has only Luma component
    CHROMA_COMPONENT,              //frame has only Chroma Component
    BOTH_LUMA_CHROMA               //frame has both Luma nad chroma components 
} halide_hexagon_dma_user_component_t;

/** file
 *  Routines specific to the Halide Hexagon dma runtime.
 */
typedef int halide_hexagon_dma_handle_t;

/**
 * halide_hexagon_dma_device_interface
 * Returns hexagon dma interface  */
extern const struct halide_device_interface_t *halide_hexagon_dma_device_interface();

/**
 * nhalide_pipeline
 * Put in for test purpose
 */
int nhalide_pipeline(void *user_context, unsigned char *inframe, unsigned char *outframe);

/**
 * halide_hexagon_dmaapp_wrap_buffer
 * in: halide_buffer_t*
 * in: unsigned char
 * out: int
 * desc: Wraps inframe buf into the device handle
 */
extern int halide_hexagon_dmaapp_wrap_buffer(void *user_context, halide_buffer_t* buf, unsigned char *inframe,
		                                    bool read=true, halide_hexagon_dma_user_fmt_t fmt=NV12);

/**
 * halide_hexagon_dmaapp_release_wrapper
 * in: halide_buffer_t*
 * out: int
 * desc: Release the buf from the device handle
 */
extern int halide_hexagon_dmaapp_release_wrapper(void *user_context, halide_buffer_t* buf);

/**
 * halide_hexagon_dmart_get_memory
 * in: halide_buffer
 * desc: Allocates L2 buf
 */
extern void* halide_hexagon_dmaapp_get_memory(void* user_context, halide_buffer_t *roi_buf);

/**
 * halide_buffer_copy
 * in:void* user_Context
 * out:halide_buffer_t src
 * out:halide_buffer_t dest
 * desc: copes from src to dest
 * void* ptr */
extern int halide_buffer_copy(void *user_context, halide_buffer_t *src_buf, void *ptr, halide_buffer_t *dst_buf);

#ifdef __cplusplus
} // End extern "C"
#endif
#endif // HALIDE_HEXAGON_DMA_API_H
