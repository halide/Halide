/**
 * This file contains the definitions of the hexagon dma device interface class
 * This file is an interface between Halide Runtime and the DMA driver
 * The functions in this file take care of the various functionalities of the
 * DMA driver through their respective halide device interface functions
 * The function in this file use the Hexagon DMA context structure for sharing
 * the DMA context across various APIs of the
 * DMA device interface */

#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "hexagon_dma_device_shim.h"
#include "HalideRuntimeHexagonDma.h"
#include "hexagon_dma_context.h"
#include "hexagon_dma_api.h"

using namespace Halide::Runtime::Internal::Qurt;
namespace Halide { namespace Runtime { namespace Internal { namespace Dma {

dma_context WEAK pdma_context = NULL;
}}}}

using namespace Halide::Runtime::Internal::Dma;

void halide_hexagon_set_dma_context(void* user_context, dma_context context) {
    halide_assert(user_context, context != NULL);
    pdma_context = context;
}

void halide_hexagon_get_dma_context (void* user_context, dma_context* context) {
    halide_assert(user_context, context != NULL);
    *context = pdma_context;
}

void halide_hexagon_acquire_dma_context(void* user_context, dma_context* ctx, int num_of_frames, bool create=true) {
    halide_assert(user_context, ctx != NULL);
    // If the context has not been initialized, initialize it now.
    if (!pdma_context && create) {
       halide_hexagon_dmaapp_create_context(user_context, num_of_frames);
    }
    *ctx = pdma_context;
}

namespace Halide { namespace Runtime { namespace Internal { namespace Dma {

//Helper class to pass DMA Context
class HexagonDmaContext {
    void* user_ctxt;

public:
    dma_context context;

    inline HexagonDmaContext(void* user_context): user_ctxt(user_context),
                             context(NULL) {

        halide_hexagon_acquire_dma_context(user_ctxt, &context, 1);
        halide_assert(user_context, context != NULL);
    }

    inline HexagonDmaContext(void* user_context, int num_of_frames): user_ctxt(user_context),
                            context(NULL) {

       halide_hexagon_acquire_dma_context(user_ctxt, &context, num_of_frames);
       halide_assert(user_context, context != NULL);
    }


    inline ~HexagonDmaContext() {
        user_ctxt = 0;
        context = 0;
    }

};
}}}}

extern "C" {

extern WEAK halide_device_interface_t hexagon_dma_device_interface;

int halide_hexagon_dmart_wrap_buffer(void *user_context, halide_buffer_t *buf, void *inframe)
{
    halide_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }

    dma_desc_image_t *handle = (dma_desc_image_t *)inframe;
    if (!handle)
        return HEX_ERROR;

    HexagonDmaContext hexagon_dma_context(user_context, handle->num_of_frames);
    halide_assert(user_context, hexagon_dma_context.context != NULL);
    dma_context dma_ctxt = hexagon_dma_context.context;

    dma_ctxt->set_host_frame(user_context, (uintptr_t)handle->buffer, handle->type, handle->read, handle->width, handle->height, handle->stride, handle->last_frame);
    dma_ctxt->set_padding(user_context, (uintptr_t)handle->buffer, handle->padding);

    buf->device_interface = &hexagon_dma_device_interface;
    buf->device = reinterpret_cast<uint64_t>(dma_ctxt);
    return 0;
}

int halide_hexagon_dmart_release_wrapper(void *user_context, halide_buffer_t *buf)
{
    halide_assert(NULL, buf!=NULL);
    void* handle = NULL;
    bool read_flag = false;

    dma_context dma_handle = reinterpret_cast<dma_context>(buf->device);
    int frame_index = dma_handle->get_frame_index(user_context);
    uintptr_t frame = dma_handle->get_frame(user_context, frame_index);
    int fold_idx = dma_handle->pframe_table[frame_index].work_buffer_id;
    uintptr_t fold_addr = dma_handle->pfold_storage[fold_idx].fold_virtual_addr;

    if (dma_handle->is_buffer_read(user_context, frame, &read_flag) == -1) {
        error(user_context) << "Function failed to find the frame" ;
        return HEX_ERROR;
    } 

    if (read_flag) {
        handle = dma_handle->get_read_handle(user_context, frame);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Read Handle  \n";
            return HEX_ERROR;
        }
    } else {
        handle = dma_handle->get_write_handle(user_context, frame);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Write Handle  \n";
            return HEX_ERROR;
        }
    }
    if (handle != 0) {
        bool last_frame;
        if (dma_handle->get_last_frame(user_context, frame, &last_frame) == -1) {
            error(user_context) << "Function failed to get last frame \n";
            return HEX_ERROR;
        }

        dma_finish_frame(handle);
        dma_handle->clr_host_frame(user_context, frame);

        if (last_frame) {
            dma_free_dma_engine(handle);
            // Free the Allocated Qurt Memory Regions
            uintptr_t tcm_region,desc_region;
            uintptr_t desc_va;
            qurt_size_t desc_size, tcm_size;
            if (dma_handle->get_tcm_desc_params(user_context, fold_addr,&tcm_region,
                              &tcm_size, (uintptr_t *)&desc_va,&desc_region, &desc_size)) {
                error(user_context) << "Function failed to get TCM Desc Params  \n";
                return HEX_ERROR;
            }
            // Release the TCM regions that were locked.
            // This can also be done through the firmware (using the nDmaWrapper_UnlockCache function).
            int nRet  = dma_unlock_cache(fold_addr, tcm_size);
            if (nRet != QURT_EOK) {
                error(user_context) << "QURT TCM unlock failed due to QURT_EALIGN \
                                ERROR misaligned u32Size = " << tcm_size << "\n";
                return HEX_ERROR;
            }

            nRet  = dma_unlock_cache(desc_va, desc_size);
            if (nRet != QURT_EOK) {
                error(user_context) << "QURT TCM descriptor unlock failed QURT_EALIGN ERROR misaligned u32Size = " << desc_size << "\n";
                return nRet;
            }

            // Delete all regions that were allocated.
            dma_delete_mem_region(tcm_region);
            dma_delete_mem_region(desc_region);
        }
    }
    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = NULL;
    return HEX_SUCCESS;
}


WEAK int halide_hexagon_dma_device_release(void *user_context) {
    return HEX_SUCCESS;
}

WEAK int halide_hexagon_dma_device_malloc(void *user_context, halide_buffer_t *buf) {

    if (buf->device) {
        // This buffer already has a device allocation
        return 0;
    }

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);

    //TBD Allocation
    return 0;

}

WEAK int halide_hexagon_dma_device_free(void *user_context, halide_buffer_t *buf) {
    halide_assert(NULL, buf!=NULL);

    halide_hexagon_dmart_release_wrapper(user_context, buf);
    halide_hexagon_dmaapp_delete_context(user_context);

    return HEX_SUCCESS;

}

WEAK int halide_hexagon_dma_copy_to_device(void *user_context,  halide_buffer_t *buf) {
    void* handle = NULL;
    int nRet;
    halide_assert(user_context, buf!=NULL);
    bool read_flag;

    dma_context dma_handle = reinterpret_cast<dma_context>(buf->device);
    int frame_index = dma_handle->get_frame_index(user_context);
    uintptr_t frame = dma_handle->get_frame(user_context, frame_index);
    
    if (dma_handle->is_buffer_read(user_context, frame, &read_flag) == -1) {
        error(user_context) << "Funcation failed to find the frame" ;
        return HEX_ERROR;
    }

    if (!read_flag) {
        handle = dma_handle->get_write_handle(user_context, frame);
        if (dma_handle == NULL) {
            error(user_context) << "Function failed to get DMA Read Handle  \n";
            return HEX_ERROR;
        }
    } else {
        return HEX_ERROR;
    }

    int ncomponents = dma_handle->get_num_components(user_context, frame);
    if (ncomponents < 0) {
        error(user_context) << "Function failed to get number of Components from DMA Context \n";
        return HEX_ERROR;
    }

    t_dma_move_params move_param;
    int fold_idx = dma_handle->pframe_table[frame_index].work_buffer_id;
    uintptr_t fold_addr = dma_handle->pfold_storage[fold_idx].fold_virtual_addr; 
    dma_handle->get_update_params(user_context, fold_addr, &move_param);
    move_param.handle = handle;
    move_param.ncomponents = ncomponents;
    nRet = dma_move_data(move_param);

    if (nRet != QURT_EOK) {
        return HEX_ERROR;
    }

    return nRet;
}

WEAK int halide_hexagon_dma_copy_to_host(void *user_context, halide_buffer_t *buf) {
    void* handle;
    bool read_flag;

    dma_context dma_handle = reinterpret_cast<dma_context>(buf->device);
    int frame_index = dma_handle->get_frame_index(user_context);
    uintptr_t frame = dma_handle->get_frame(user_context, frame_index);

    if (dma_handle->is_buffer_read(user_context, frame, &read_flag) == -1) {
        error(user_context) << "Function failed to find the frame" ;
        return HEX_ERROR;
    }

    if (read_flag) {
        handle = dma_handle->get_read_handle(user_context, frame);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Read Handle  \n";
            return HEX_ERROR;
        }
    } else {
        return HEX_ERROR;
    }

    int ncomponents;
    ncomponents = dma_handle->get_num_components(user_context, frame);

    t_dma_move_params move_param;
    int fold_idx = dma_handle->pframe_table[frame_index].work_buffer_id;
    uintptr_t fold_addr = dma_handle->pfold_storage[fold_idx].fold_virtual_addr; 
    dma_handle->get_update_params(user_context, fold_addr, &move_param);
    move_param.handle = handle;
    move_param.ncomponents = ncomponents;

    int nRet = HEX_SUCCESS;
    nRet = dma_move_data(move_param);

    if (nRet != QURT_EOK) {
       return HEX_ERROR;
    }
    return HEX_SUCCESS;
}


WEAK int halide_hexagon_dma_device_sync(void* user_context, halide_buffer_t *buf) {
    int nRet = HEX_SUCCESS;
    void* handle=NULL;
    bool read_flag;
    
    dma_context dma_handle = reinterpret_cast<dma_context>(buf->device);
    int current_frame_index = dma_handle->get_frame_index(user_context);
    uintptr_t frame = dma_handle->get_frame(user_context, current_frame_index);
  
    if (dma_handle->is_buffer_read(user_context, frame, &read_flag) == -1) {
        error(user_context) << "Funcation failed to find the frame" ;
        return HEX_ERROR;
    }
 
    if (read_flag) {
        handle = dma_handle->get_read_handle(user_context, frame);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Read Handle  \n";
            return HEX_ERROR;
        }
        nRet = dma_wait(handle);
    } else {
        handle = dma_handle->get_write_handle(user_context, frame);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Write  Handle  \n";
            return HEX_ERROR;
        }
        nRet = dma_wait(handle);
    }
    return nRet;
}


WEAK int halide_hexagon_dma_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    int result = halide_hexagon_dma_device_malloc(user_context, buf);
    return result;
}

WEAK int halide_hexagon_dma_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    int result = halide_hexagon_dma_device_free(user_context, buf);
    buf->host = NULL;
    return result;
}


WEAK const halide_device_interface_t *halide_hexagon_dma_device_interface() {
    return &hexagon_dma_device_interface;
}

}// exterm C enfs

WEAK halide_device_interface_impl_t hexagon_dma_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_hexagon_dma_device_malloc,
    halide_hexagon_dma_device_free,
    halide_hexagon_dma_device_sync,
    halide_hexagon_dma_device_release,
    halide_hexagon_dma_copy_to_host,
    halide_hexagon_dma_copy_to_device,
    halide_hexagon_dma_device_and_host_malloc,
    halide_hexagon_dma_device_and_host_free,
    halide_default_device_wrap_native,
    halide_default_device_detach_native,
};

WEAK struct halide_device_interface_t hexagon_dma_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_device_wrap_native,
    halide_device_detach_native,
    &hexagon_dma_device_interface_impl
};

