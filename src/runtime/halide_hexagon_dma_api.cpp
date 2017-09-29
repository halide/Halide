/**
 * This file contains the necessary API defination for Initiating
 * and Terminating the Hexagon DMA transfer from Halide user space
 * The Header is available to the user to pass the necessary details
 */
#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "printer.h"
#include "device_interface.h"
#include "hexagon_dma_device_shim.h"
#include "halide_hexagon_dma_api.h"
#include "hexagon_dma_api.h"
#include "hexagon_dma_rt.h"

using namespace Halide::Runtime::Internal::Qurt;
using namespace Halide::Runtime::Internal::Dma;

extern "C" {


// desc: Create Context
// Attach the frame to the context
// Wrap the device handle over the context
int halide_hexagon_dmaapp_wrap_buffer(void *user_context, halide_buffer_t *buf, unsigned char *inframe, bool read, halide_hexagon_dma_user_fmt_t fmt) {
    halide_assert(user_context, buf->device == 0);
    if(buf->device != 0) {
        return -2;
    }

    halide_assert(user_context, buf->dimensions > 1);
    halide_assert(user_context, buf->dimensions < 4);

    int num_of_frames = 1;  // ASSUMPTION: step 1 is one input frame only
    int last_frame = 1;     // ASSUMPTION: always new resources for each frame

    HexagonDmaContext hexagon_dma_context(user_context, num_of_frames);
    halide_assert(user_context, hexagon_dma_context.get_context() != NULL);
    p_dma_context dma_ctxt = hexagon_dma_context.get_context();

    int nret = halide_hexagon_dmart_attach_context(user_context, dma_ctxt, (uintptr_t)inframe, fmt, read, \
            buf->dim[0].extent, buf->dim[1].extent, buf->dim[1].stride, last_frame);
    if(nret) {
        error(user_context) << "Failure to attach the context \n";
        return nret;
    }
    buf->device_interface = halide_hexagon_dma_device_interface();
    buf->device = reinterpret_cast<uint64_t>(dma_ctxt);
    return HEX_SUCCESS;
}


//desc: Free DMA Engine
// Signal to DMA end of frame remove frame references
// Free L2 buffer allocations made for transfer
// Remove the device handle from the context
int halide_hexagon_dmaapp_release_wrapper(void *user_context, halide_buffer_t *buf) {
    halide_assert(NULL, buf!=NULL);
    void* handle = NULL;

    //////////////////////////////////////////////////////////////////////
    p_dma_context dma_handle = reinterpret_cast<p_dma_context>(buf->device);
    uintptr_t  frame = halide_hexagon_dmart_get_frame(user_context, dma_handle);
    //////////////////////////////////////////////////////////////////////////
    handle = halide_hexagon_dmart_get_dma_handle(user_context, dma_handle, frame);
    if(handle == 0) {
        error(user_context) << "Function failed to get DMA Write  Handle  \n";
        return HEX_ERROR;
    }

    if(handle != 0) {
        dma_finish_frame(handle);
        uintptr_t fold_addr = halide_hexagon_dmart_get_fold_addr(user_context, dma_handle, frame);
        halide_hexagon_dma_memory_free(user_context, dma_handle, (void*)fold_addr);
        halide_hexagon_dmart_detach_context(user_context, dma_handle, frame);
        dma_free_dma_engine(handle);
    }
    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = NULL;

    return HEX_SUCCESS;
}


//desc: Calculate width, height, stride and fold from roi buf
//Allocate Cache Memory from roi buf dimensions to make dma transfer
void* halide_hexagon_dmaapp_get_memory(void* user_context, halide_buffer_t *roi_buf, \
                                  bool padding, halide_hexagon_dma_user_fmt_t type) {
    static void *pcache = NULL;     // ASSUMPTION: single frame only, lifetime scope is unknown will inner loop call

    halide_assert(user_context, roi_buf != NULL);
    //Check if roi_buf is already allocated
    if(pcache != NULL) {
        return pcache;
    } else {
        void *vret = NULL;
        int nCircularFold;

        halide_hexagon_dma_user_component_t comp;
        if(halide_hexagon_dma_comp_get(user_context, roi_buf, comp)) {
            error(user_context) << "Failure to get the component\n" ;
            return NULL;
        }
        /////////////////////////////////////////////////////
        //Problem with not including the inframe buf
        // Have to take the global version of dma context
        /////////////////////////////////////////////////////
        HexagonDmaContext hexagon_dma_context(user_context);
        halide_assert(user_context, hexagon_dma_context.get_context() != NULL);
        p_dma_context dma_ctxt = hexagon_dma_context.get_context();

        // ASSUMPTION: no folding
        nCircularFold = 1;
        ////////////////////////////////////////////////////////////
        // Divide Frame to predefined tiles in Horizontal Direction
        //fold_width = roi_buf->dim[0].extent;
        // Divide Frame to predefined tiles in Vertical Direction
        //fold_height = roi_buf->dim[1].extent;
        // Each tile is again vertically split in to predefined DMA transfers
        // Stride is aligned to Predefined Value
        //fold_stride = roi_buf->dim[1].stride;
        ///////////////////////////////////////////////////////////////
     
        /////////////////////////////////////////////////////////
        //ASSUMPTION: Assuming paddng and fmt type to be default here since we really do  not have the inframe
        //int halide_hexagon_dmart_get_padding(void* user_context, uintptr_t frame);
        //int halide_hexagon_dmart_get_fmt_type(void* user_context, uintptr_t frame)
        ///////////////////////////////////////////////////
        // allocate L2$
        vret = halide_hexagon_dma_memory_alloc(user_context, dma_ctxt, comp, roi_buf->dim[0].extent, \
                         roi_buf->dim[1].extent, roi_buf->dim[1].stride, nCircularFold, padding, type);
        if(vret == NULL) {
            error(user_context) << "Failed to alloc host memeory." <<"\n";
            return NULL;
        }
        pcache = vret;
        return vret;
    }
}     


//Step 1: Updates/Prepare the DMA for transfer
//Step 2: Update ROI (to be transfered) information to the host
//Step 3: copy to host: Actual transfer of Data
//Step 4: Wait for DMA to be finished
int halide_buffer_copy(void *user_context, halide_buffer_t *frame_buf, void *ptr, halide_buffer_t *roi_buf) {
    int nRet;
    const halide_device_interface_t* dma_device_interface = halide_hexagon_dma_device_interface();

    int x = roi_buf->dim[0].min;
    int y = roi_buf->dim[1].min;
    int w = roi_buf->dim[0].extent;
    int h = roi_buf->dim[1].extent;
  
    if(frame_buf->device == 0) {
        return HEX_ERROR;
    }

    if(roi_buf->host == 0) {
        return HEX_ERROR;
    } 
 
    p_dma_context handle = reinterpret_cast<p_dma_context>(frame_buf->device);
    uintptr_t cache_addr = (uintptr_t)roi_buf->host;
 
    nRet = halide_hexagon_dma_update(user_context, frame_buf, roi_buf);
    if(nRet != 0) {
        error(user_context) << "Failed to update DMA. The error code is: " << nRet <<"\n";
        return nRet;
    }
    
    nRet = halide_hexagon_dmart_set_host_roi(user_context, handle, cache_addr, x, y, w, h, 0);
    if(nRet != 0) {
        error(user_context) << "Failed to Set Host ROI details. The error code is: " << nRet <<"\n";
        return nRet;
    }

    // Initiate the DMA Read -> Transfer from device (DDR) -> host (L2$) memory
    nRet = dma_device_interface->impl->copy_to_host(user_context, frame_buf);
    if(nRet != 0) {
        error(user_context) << "Failed to initiate DMA Read. The error code is: " << nRet <<"\n";
        return nRet;
    }

    // ASSUMPTION: synchronous DMA
    nRet = dma_device_interface->impl->device_sync(user_context, frame_buf);
    if(nRet != 0) {
        error(user_context) << "DMA Inititated but failed to complete. The error code is: " << nRet <<"\n";
        return nRet;
    }
    return nRet;    
}


//
// Halide UBWC/DMA test function, for step 1 of 5
// 
// Simple, synchronous, DMA tiling example, using cross buffer copy.
// Let's *not* attempt to share resources across pipeline invocations, even if it's slow for now.
// It will be easier to figure out how to share resources across pipeline invocations
// once we can see exactly what needs to be shared and how.
//
// Key changes
//      decoupled/separation of p_dma_context from user_context
//      introduced context class
//      swap host (locked L2$) and device (DDR) roles
//      single frame of NV12, processing luma only
//
//      
int nhalide_pipeline(void *user_context, unsigned char *inframe, unsigned char *outframe) {
    //hard coded for now
    const int width = 768;
    const int height = 384;

    halide_dimension_t inframe_dims[3];
    inframe_dims[0] = halide_dimension_t(0, width, 1);
    inframe_dims[1] = halide_dimension_t(0, height, width);
    inframe_dims[2] = halide_dimension_t(0, 2, width*height);

    halide_buffer_t inframe_buf;
    inframe_buf.dim = inframe_dims;
    inframe_buf.dimensions = 3;
    inframe_buf.host = NULL;
    inframe_buf.device = 0;
    inframe_buf.flags = 0;

    // ASSUMPTION: note defaulted arguments for dma direction and frame format
    halide_hexagon_dmaapp_wrap_buffer(user_context, &inframe_buf, inframe);

    const int tile_width = 256;
    const int tile_height = 32;

    for(int tx = 0; tx < width / tile_width; tx++) {
        for(int ty = 0; ty < height / tile_height; ty++) {
            halide_dimension_t roi_dims[3];
            roi_dims[0] = halide_dimension_t(tx*tile_width, tile_width, 1);
            roi_dims[1] = halide_dimension_t(ty*tile_height, tile_height, tile_width);
            roi_dims[2] = halide_dimension_t(0, 1, tile_width*tile_height);

            halide_buffer_t roi_buf;
            roi_buf.dim = roi_dims;
            roi_buf.dimensions = 3;
            roi_buf.flags = 0;
            // ASSUMPTION: note defaulted arguments for dma padding and frame format
            roi_buf.host = (unsigned char*)halide_hexagon_dmaapp_get_memory(user_context, &roi_buf);

            halide_buffer_copy(user_context, &inframe_buf, NULL, &roi_buf);

            for(int x = 0; x < tile_width; x++) {
                for(int y = 0; y < tile_height; y++) {
                    outframe[(ty * tile_height + y) * width + tx * tile_width + x] =
                        roi_buf.host[y * tile_width + x];
                }
            }
        }
    }
    halide_hexagon_dmaapp_release_wrapper(user_context, &inframe_buf);
    return 0;
}

}// exterm C enfs
