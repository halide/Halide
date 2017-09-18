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
#include "hexagon_dma_rt.h"
#include "hexagon_dma_context.h"

using namespace Halide::Runtime::Internal::Qurt;

namespace Halide { namespace Runtime { namespace Internal { namespace Dma {

WEAK t_dma_context* pdma_context = NULL;
}}}}

using namespace Halide::Runtime::Internal::Dma;

void halide_hexagon_set_dma_context(void* user_context, t_dma_context* context) {
    halide_assert(user_context, context != NULL);
    pdma_context = context;
}
/**
 *  halide_hexagon_dmart_get_context
 * get DMA context from hexagon context
 * dma_context = DMA context */
void halide_hexagon_get_dma_context (void* user_context, t_dma_context** context) {
    halide_assert(user_context, context != NULL);
    *context = pdma_context;
}

void halide_hexagon_acquire_dma_context(void* user_context, t_dma_context** ctx, bool create=true) {
    halide_assert(user_context, ctx != NULL);
    // If the context has not been initialized, initialize it now.
    if (!pdma_context && create) {
        halide_hexagon_get_dma_context(user_context, &pdma_context);
    }

    *ctx = pdma_context;
}

namespace Halide { namespace Runtime { namespace Internal { namespace Dma {

//Helper class to pass DMA Context
class HexagonDmaContext {
    void* user_ctxt;

public:
    t_dma_context* context;

    inline HexagonDmaContext(void* user_context): user_ctxt(user_context),
                             context(NULL) {

        halide_hexagon_acquire_dma_context(user_ctxt, &context);
        halide_assert(user_context, context != NULL);
    }

    inline ~HexagonDmaContext() {
        //halide_release_dma_context(user_context)
    }

};
}}}}

extern "C" {

extern WEAK halide_device_interface_t hexagon_dma_device_interface;

WEAK int halide_hexagon_dma_device_release(void *user_context) {
    return HEX_SUCCESS;
}

WEAK int halide_hexagon_dma_device_malloc(void *user_context, halide_buffer_t *buf) {
    int nRet = HEX_SUCCESS;
    t_eDmaFmt chroma_type,luma_type;
    int frame_idx;
    t_dma_context* dma_context;
    uintptr_t region_tcm;
    uintptr_t region_tcm_desc;
    uintptr_t tcm_buf_vaddr, tcm_desc_vaddr;
    qurt_size_t region_tcm_desc_size;
    qurt_mem_pool_t pool_tcm;

    HexagonDmaContext hexagon_dma_context(user_context);
    halide_assert(user_context, hexagon_dma_context.context != NULL);
    dma_context = hexagon_dma_context.context;

    //Get the Frame Index
    frame_idx = halide_hexagon_dmart_get_frame_index(user_context, (uintptr_t)buf->host);
    if (frame_idx == -1)  {
        error(user_context) << "Function failed to get Frame Index  \n";
        return HEX_ERROR;
    }
    
    //Get the Chroma and Luma Type
    chroma_type = dma_context->presource_frames[frame_idx].chroma_type;
    luma_type = dma_context->presource_frames[frame_idx].luma_type;

    t_eDmaFmt ae_fmt_id[2] = {luma_type,chroma_type};
    int roi_width = 0;
    int roi_height = 0;
    bool is_ubwc = 0;
    bool padding = 0;
    int frame_width = 0;
    int frame_height = 0;
    int frame_stride = 0;
    int luma_stride = 0;
    int chroma_stride = 0;
    void* dma_handle = NULL;
    int ncomponents = 0;
    int nfolds = 0;
    int plane = 0;

    // Get the necessary Parameters from DMA Context
    // Get ROI and Padding Details 
    roi_width = dma_context->presource_frames[frame_idx].fold_width;
    roi_height = dma_context->presource_frames[frame_idx].fold_height;
    padding = dma_context->presource_frames[frame_idx].padding;
    is_ubwc = dma_context->presource_frames[frame_idx].is_ubwc;
    frame_width = dma_context->presource_frames[frame_idx].frame_width;
    frame_height = dma_context->presource_frames[frame_idx].frame_height;
    frame_stride = dma_context->presource_frames[frame_idx].frame_stride;
    plane = dma_context->presource_frames[frame_idx].plane;
    ncomponents = ((plane==LUMA_COMPONENT)|| (plane==CHROMA_COMPONENT))?1:2;
    nfolds = dma_context->presource_frames[frame_idx].num_folds;

    t_dma_pix_align_info pst_roi_size;
    pst_roi_size.u16W = roi_width;
    pst_roi_size.u16H = roi_height;
    luma_stride = dma_get_stride(luma_type, is_ubwc, pst_roi_size);
    chroma_stride = dma_get_stride(chroma_type, is_ubwc, pst_roi_size);

    //#########################
    //Allocate DMA if required
    //#########################
    bool dma_allocate;
    if (halide_hexagon_dmart_allocate_dma(user_context, (uintptr_t)buf->host, &dma_allocate) == -1) {
        error(user_context) << "Undefined Error"; 
        return HEX_ERROR;
    }   

    if (dma_allocate) {
        dma_handle = dma_allocate_dma_engine();
        if (dma_handle == 0) {
            error(user_context) << "halide_hexagon_dma_device_malloc:Failed to allocate the read DMA engine.\n";
            return HEX_ERROR;
        }
        if (halide_hexagon_dmart_set_dma_handle(user_context, dma_handle, (uintptr_t)buf->host)) {
            error(user_context) << "Function failed to set DMA Handle to DMA context \n";
            return HEX_ERROR;
        }
    } else {
        //An Allocated DMA Already Exists Re-use it 
        bool read = dma_context->pframe_table[frame_idx].read;
        if (read) {
            dma_handle = halide_hexagon_dmart_get_read_handle(user_context, (uintptr_t)buf->host);
        } else {
            dma_handle = halide_hexagon_dmart_get_write_handle(user_context, (uintptr_t)buf->host);
        }
    }

    //Check if there are any Free and Allocated Fold Storage Available 
    bool fold_exists = false;
    int fold_idx;
    halide_hexagon_dmart_get_free_fold(user_context, &fold_exists, &fold_idx);

    if (fold_exists) {
        //An Allocated and Free Fold Exists
        // re-use it
        buf->device = dma_context->pfold_storage[fold_idx].fold_virtual_addr;
        // Not able to use the Existing Descriptors
        region_tcm_desc_size = dma_context->pfold_storage[fold_idx].size_desc;
        tcm_desc_vaddr = dma_context->pfold_storage[fold_idx].desc_virtual_addr;
    } else {
        //Free Fold Doesnt Exists We will try allocating one

        //#################################################################################
        //Now allocate the descriptors
        // 2 ping pong buffers for each frame ( read and write so two)
        //################################################################################
        region_tcm_desc_size = dma_get_descriptor_size(ae_fmt_id, ncomponents, nfolds);

        //#########################################################
        //Now allocate the L2 Cache
        //########################################################
        //The maximum TCM Buf Size
        qurt_size_t tcm_buf_size;
        tcm_buf_size = halide_hexagon_dmart_get_fold_size(user_context, (uintptr_t)buf->host);
        if (tcm_buf_size == 0) {
            error(user_context) << "Function failed to get Fold Buffer Size \n";
            return HEX_ERROR;
        }
        qurt_size_t region_tcm_size = align(tcm_buf_size, 0x1000);

        // It is a good idea to check that this size is not too large, as while we are still in DDR, when locked to the TCM
        // large sizes will become problematic.
        qurt_size_t region_tcm_limit = 0x40000; // Limit is set to 256k
        if (region_tcm_size > region_tcm_limit) {
            error(user_context) << "The required TCM region for this ROI" << region_tcm_size << \
                           "exceeds the set limit of " << region_tcm_limit;
            error(user_context) << "The ROI must be lowered or the allowed region made larger.\n" ;
            return HEX_ERROR;
        }

        nRet = dma_get_mem_pool_id(&pool_tcm);
        if (nRet != QURT_EOK) {
            error(user_context) << "Failed to attach the TCM memory region. The error code is:" << nRet << "\n" ;
            return HEX_ERROR;
        }

        tcm_buf_vaddr = dma_allocate_cache(pool_tcm, region_tcm_size, &region_tcm);
        tcm_desc_vaddr = dma_allocate_cache(pool_tcm, region_tcm_desc_size, &region_tcm_desc);

        // Lock the TCM region. This maps the region mapped as TCM to the actual TCM.
        // This is done to ensure the region is not invalidated during DMA processing.
        // This can also be done through the firmware (using the nDmaWrapper_LockCache function).
        nRet = dma_lock_cache(tcm_buf_vaddr, region_tcm_size);
        if (nRet != QURT_EOK) {
            error(user_context) << "QURT TCM lock failed due to QURT_EALIGN ERROR  \
                           misaligned u32Size = " << region_tcm_size << "\n";
            return HEX_ERROR;
        }

        // Lock the descriptor region as well.
        nRet  = dma_lock_cache(tcm_desc_vaddr, region_tcm_desc_size);
        if (nRet != QURT_EOK) {
            error(user_context) << "QURT TCM lock failed due to QURT_EALIGN ERROR  \
                           misaligned u32Size = " << region_tcm_desc_size << "\n" ;
            return HEX_ERROR;
        }

        if (halide_hexagon_dmart_set_fold_storage(user_context, tcm_buf_vaddr, region_tcm, tcm_buf_size, \
                         tcm_desc_vaddr, region_tcm_desc, region_tcm_desc_size, &fold_idx)) {
            error(user_context) << "Function failed to Set Fold Storage to DMA context \n";
            return HEX_ERROR;
        }

        //Assign the Allocated Fold Storage to Device Memory
        buf->device = tcm_buf_vaddr;
    }
    //Now We have Allocated the Fold and will link it to the Frame
    if (halide_hexagon_dmart_set_storage_linkage(user_context, (uintptr_t)buf->host, (uintptr_t)buf->device, fold_idx)) {
        error(user_context) << "Function failed to Link Frame and Fold Storage \n";
        return HEX_ERROR;
    }

    //Populate Work Descriptors and Prepare DMA
    //#################################################################
    t_dma_prepare_params params;
    params.handle = dma_handle;
    params.host_address =  (uintptr_t )buf->host;
    params.frame_width = frame_width;
    params.frame_height = frame_height;
    params.frame_stride = frame_stride;
    params.roi_width = roi_width;
    params.roi_height = roi_height;
    params.luma_stride = luma_stride;
    params.chroma_stride = chroma_stride;
    params.luma_type = luma_type;
    params.chroma_type = chroma_type;
    params.ncomponents = ncomponents;
    params.padding = padding;
    params.is_ubwc = is_ubwc;
    params.desc_address = tcm_desc_vaddr;
    params.desc_size = region_tcm_desc_size;

    nRet = dma_prepare_for_transfer(params);
    if(nRet != QURT_EOK) {
        error(user_context) << "Error in Preparing for DMA Transfer\n";
        return nRet;
    }
    return nRet;
}

WEAK int halide_hexagon_dma_device_free(void *user_context, halide_buffer_t *buf) {
    void* handle = NULL;
    halide_assert(NULL, buf!=NULL);
    bool read_flag = false;
    t_dma_context *dma_context = NULL;

    HexagonDmaContext hexagon_dma_context(user_context);
    halide_assert(user_context, hexagon_dma_context.context != NULL);
    dma_context = hexagon_dma_context.context;

    if (halide_hexagon_dmart_is_buffer_read(user_context, (uintptr_t)buf->host, &read_flag) == -1) {
        error(user_context) << "Funcation failed to find the frame" ;
        return HEX_ERROR;
    }
    
    if (read_flag) {
        handle = halide_hexagon_dmart_get_read_handle(user_context, (uintptr_t)buf->host);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Read Handle  \n";
            return HEX_ERROR;
        }
    } else {
        handle = halide_hexagon_dmart_get_write_handle(user_context, (uintptr_t)buf->host);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Write Handle  \n";
            return HEX_ERROR;
        }
    }
    if (handle != 0) {

        bool last_frame;

        if (halide_hexagon_dmart_get_last_frame(user_context, (uintptr_t)buf->host, &last_frame) == -1) {
            error(user_context) << "Function failed to get last frame \n";
            return HEX_ERROR;
        }
      
        dma_finish_frame(handle);
        if (last_frame) {
            dma_free_dma_engine(handle);
            // Free the Allocated Qurt Memory Regions
            uintptr_t tcm_region,desc_region;
            uintptr_t desc_va;
            qurt_size_t desc_size, tcm_size;
            if (halide_hexagon_dmart_get_tcm_desc_params(user_context, (uintptr_t)buf->device,&tcm_region,
                                                        &tcm_size, (uintptr_t *)&desc_va,&desc_region, &desc_size)) {
                error(user_context) << "Function failed to get TCM Desc Params  \n";
                return HEX_ERROR;
            }
            // Release the TCM regions that were locked.
            // This can also be done through the firmware (using the nDmaWrapper_UnlockCache function).
            int nRet  = dma_unlock_cache(buf->device, tcm_size);
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
    buf->device = 0;
    return HEX_SUCCESS;
}

WEAK int halide_hexagon_dma_copy_to_device(void *user_context,  halide_buffer_t *buf) {
    void* handle = NULL;
    int nRet;
    halide_assert(user_context, buf!=NULL);
    bool read_flag;
    t_dma_context *dma_context = NULL;
    HexagonDmaContext hexagon_dma_context(user_context);
    halide_assert(user_context, hexagon_dma_context.context != NULL);
    dma_context = hexagon_dma_context.context;

    if (halide_hexagon_dmart_is_buffer_read(user_context, (uintptr_t)buf->host, &read_flag) == -1) {
        error(user_context) << "Funcation failed to find the frame" ;
        return HEX_ERROR;
    }

    if (read_flag) {
        handle = halide_hexagon_dmart_get_read_handle(user_context, (uintptr_t)buf->host);
        if (handle == NULL) {
            error(user_context) << "Function failed to get DMA Read Handle  \n";
            return HEX_ERROR;
        }
    } else {
        return HEX_ERROR;
    }
    
    int ncomponents = halide_hexagon_dmart_get_num_components(user_context, (uintptr_t)buf->host);
    if (ncomponents < 0) {
        error(user_context) << "Function failed to get number of Components from DMA Context \n";
        return HEX_ERROR;
    }
    
    t_dma_move_params move_param;
    halide_hexagon_dmart_get_update_params(user_context, (uintptr_t)buf->device, &move_param);
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
    t_dma_context *dma_context = NULL;
    HexagonDmaContext hexagon_dma_context(user_context);
    halide_assert(user_context, hexagon_dma_context.context != NULL);
    dma_context = hexagon_dma_context.context;

    if (halide_hexagon_dmart_is_buffer_read(user_context, (uintptr_t)buf->host, &read_flag) == -1) {
        error(user_context) << "Funcation failed to find the frame" ;
        return HEX_ERROR;
    }

    if (!read_flag) {
        handle = halide_hexagon_dmart_get_write_handle(user_context, (uintptr_t)buf->host);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Read Handle  \n";
            return HEX_ERROR;
        }
    } else {
        return HEX_ERROR;
    }
    int ncomponents;
    ncomponents = halide_hexagon_dmart_get_num_components(user_context, (uintptr_t)buf->host);
    t_dma_move_params move_param;
    halide_hexagon_dmart_get_update_params(user_context, (uintptr_t)buf->device, &move_param);
    move_param.handle = handle;
    move_param.ncomponents = ncomponents;
    int nRet = HEX_SUCCESS;
    nRet = dma_move_data(move_param);
    if(nRet != QURT_EOK){
        return HEX_ERROR;
    }
    return nRet;
}


WEAK int halide_hexagon_dma_device_sync(void* user_context, halide_buffer_t *buf) {
    int nRet = HEX_SUCCESS;
    void* handle=NULL;
    bool read_flag;
    t_dma_context *dma_context = NULL;
    HexagonDmaContext hexagon_dma_context(user_context);
    halide_assert(user_context, hexagon_dma_context.context != NULL);
    dma_context = hexagon_dma_context.context;

    if (halide_hexagon_dmart_is_buffer_read(user_context, (uintptr_t)buf->host, &read_flag) == -1) {
        error(user_context) << "Funcation failed to find the frame" ;
        return HEX_ERROR;
    }
 
    if (read_flag){
        handle = halide_hexagon_dmart_get_read_handle(user_context, (uintptr_t)buf->host);
        if (handle == 0) {
            error(user_context) << "Function failed to get DMA Read Handle  \n";
            return HEX_ERROR;
        }
        nRet = dma_wait(handle);
    } else {
        handle = halide_hexagon_dmart_get_write_handle(user_context, (uintptr_t)buf->host);
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

