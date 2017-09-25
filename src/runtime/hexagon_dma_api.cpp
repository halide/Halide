#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "printer.h"
#include "device_interface.h"
#include "hexagon_dma_device_shim.h"
#include "HalideRuntimeHexagonDma.h"
#include "hexagon_dma_context.h"
#include "hexagon_dma_api.h"

using namespace Halide::Runtime::Internal::Qurt;

void* halide_hexagon_dma_memory_alloc(void* user_context) {
    
    int nRet = HEX_SUCCESS;

    //get the Global DMA Context   
    dma_context pdma_context;
    halide_hexagon_get_dma_context(user_context, &pdma_context);
    halide_assert(user_context, pdma_context != NULL);

    //Get the Frame Index
    int frame_idx = pdma_context->get_frame_index(user_context);
    if (frame_idx == -1) {
        error(user_context) << "Function failed to get frame index  \n";
        return NULL;
    }
    uintptr_t frame = pdma_context->get_frame(user_context, frame_idx);

    // Get the necessary Parameters from DMA Context
    t_eDmaFmt chroma_type,luma_type;
    int ncomponents = 0;
    int nfolds = 0;
    int plane = 0;
    chroma_type = pdma_context->presource_frames[frame_idx].chroma_type;
    luma_type  = pdma_context->presource_frames[frame_idx].luma_type;
    plane = pdma_context->presource_frames[frame_idx].plane;
    ncomponents = ((plane==LUMA_COMPONENT)|| (plane==CHROMA_COMPONENT))?1:2;
    nfolds = pdma_context->presource_frames[frame_idx].num_folds;
    t_eDmaFmt ae_fmt_id[2] = {luma_type,chroma_type};

    //Check if there are any Free and Allocated Fold Storage Available
    bool fold_exists = false;
    int fold_idx = 0;
    pdma_context->get_free_fold(user_context, &fold_exists, &fold_idx);

    uintptr_t region_tcm;
    uintptr_t region_tcm_desc;
    uintptr_t tcm_buf_vaddr, tcm_desc_vaddr;
    qurt_mem_pool_t pool_tcm;

    if (fold_exists) {
        tcm_buf_vaddr = pdma_context->pfold_storage[fold_idx].fold_virtual_addr;
    } else {
        qurt_size_t region_tcm_desc_size = dma_get_descriptor_size(ae_fmt_id, ncomponents, nfolds);
        qurt_size_t tcm_buf_size = pdma_context->get_fold_size(user_context, frame);
        if (tcm_buf_size == 0) {
            error(user_context) << "Function failed to get Fold Buffer Size \n";
            return NULL;
        }
        qurt_size_t region_tcm_size = align(tcm_buf_size, 0x1000);
        qurt_size_t region_tcm_limit = 0x40000; // Limit is set to 256k
        if (region_tcm_size > region_tcm_limit) {
            error(user_context) << "The required TCM region for this ROI" << region_tcm_size << \
                          "exceeds the set limit of " << region_tcm_limit;
            error(user_context) << "The ROI must be lowered or the allowed region made larger.\n" ;
            return NULL;
        }

        nRet = dma_get_mem_pool_id(&pool_tcm);
        if (nRet != QURT_EOK) {
            error(user_context) << "Failed to attach the TCM memory region. The error code is:" << nRet << "\n" ;
            return NULL;
        }

        tcm_buf_vaddr = dma_allocate_cache(pool_tcm, region_tcm_size, &region_tcm);
        tcm_desc_vaddr = dma_allocate_cache(pool_tcm, region_tcm_desc_size, &region_tcm_desc);
        
        // Lock the TCM region. This maps the region mapped as TCM to the actual TCM.
        nRet = dma_lock_cache(tcm_buf_vaddr, region_tcm_size);
        if (nRet != QURT_EOK) {
            error(user_context) << "QURT TCM lock failed due to QURT_EALIGN ERROR  \
                           misaligned u32Size = " << region_tcm_size << "\n";
            return NULL;
        }

        // Lock the descriptor region as well.
        nRet  = dma_lock_cache(tcm_desc_vaddr, region_tcm_desc_size);
        if (nRet != QURT_EOK) {
            error(user_context) << "QURT TCM lock failed due to QURT_EALIGN ERROR  \
                           misaligned u32Size = " << region_tcm_desc_size << "\n" ;
            return NULL;
        }

        if (pdma_context->set_fold_storage(user_context, tcm_buf_vaddr, region_tcm, tcm_buf_size,
                      tcm_desc_vaddr, region_tcm_desc, region_tcm_desc_size, &fold_idx)) {
            error(user_context) << "Function failed to Set Fold Storage to DMA context \n";
            return NULL;
        }
    }
      
    //Added to ensure the roi_bug gets the right address passed
    cache_mem_t* cache_addr = (cache_mem_t *) malloc(sizeof(cache_mem_t));
    cache_addr->fold_vaddr = tcm_buf_vaddr; 
    cache_addr->fold_idx = fold_idx; 
 
    return (void*) cache_addr;
}

void* halide_hexagon_dmart_get_memory(void* user_context, halide_buffer_t *roi_buf) {

    if(roi_buf->host != 0)
    {
        return (void*) roi_buf->host; 
    }

    int nCircularFold;
    int w,h,s;
    int nRet = 0;
    void* vret = NULL;

    //Since we do not pass inframe here we have no way of knowing for which frame we are assigning roi. Big issue
    halide_hexagon_dma_user_component_t comp;
    dma_context pdma_context;
    halide_hexagon_get_dma_context(user_context, &pdma_context);
    halide_assert(user_context, pdma_context != NULL);

    if(roi_buf->dim[2].extent > 1) {
        comp = BOTH_LUMA_CHROMA;
    }
    else if (roi_buf->dim[2].extent == 1 && roi_buf->dim[2].min <= roi_buf->dim[2].stride) {
        comp = LUMA_COMPONENT;
    }
    else if (roi_buf->dim[2].extent == 1 && roi_buf->dim[2].min > roi_buf->dim[2].stride) {
        comp = CHROMA_COMPONENT;
    } else {
        nRet = HEX_ERROR;
    }
    
    if(nRet != 0) {
        error(user_context) << "Failed to Set component. The error code is: " << nRet <<"\n";
        return NULL;
    }
    
    // ASSUMPTION: no folding
    nCircularFold = 1;
    // Divide Frame to predefined tiles in Horizontal Direction
    w = roi_buf->dim[0].extent;
    // Divide Frame to predefined tiles in Vertical Direction
    h = roi_buf->dim[1].extent;
    // Each tile is again vertically split in to predefined DMA transfers    
    // Stride is aligned to Predefined Value
    s = roi_buf->dim[1].stride;
     
    int current_frame_index = pdma_context->get_frame_index(user_context);
    uintptr_t frame = pdma_context->get_frame(user_context, current_frame_index);
    pdma_context->set_max_fold_storage (user_context, frame, w, h, s, nCircularFold);
    pdma_context->set_component(user_context, frame, comp);

    vret = halide_hexagon_dma_memory_alloc(user_context);
    
    if(vret == NULL) {
        error(user_context) << "Failed to alloc host memeory." <<"\n";
        return NULL;
    }
    
    return vret;
}     


static int halide_hexagon_dmart_update(void *user_context, halide_buffer_t *inframe_buf, halide_buffer_t *roi_buf)
{
    int nRet;
    int w,h,s;
    uintptr_t region_tcm;
    uintptr_t region_tcm_desc;
    uintptr_t tcm_desc_vaddr;
    qurt_size_t region_tcm_desc_size;
    qurt_size_t tcm_size;
    t_eDmaFmt chroma_type,luma_type;
    
    // Divide Frame to predefined tiles in the Horizontal Direction
    w = roi_buf->dim[0].extent;
    // Divide Frame in to predefined tiles in Vertical Direction
    h = roi_buf->dim[1].extent;
    // Each tile is again vertically split in to predefined DMA transfers    
    // Stride is aligned to Predefined Value
    s = roi_buf->dim[1].stride;
    
    //shalide_assert(s == ALIGN(w,ALIGN_SIZE));
    dma_context handle = reinterpret_cast<dma_context>(inframe_buf->device);

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
    int frame_idx = 0;
    //Get the Frame Index
    frame_idx = handle->get_frame_index(user_context);
    if (frame_idx == -1)  {
        error(user_context) << "Function failed to get Frame Index  \n";
        return HEX_ERROR;
    }

    uintptr_t frame = handle->get_frame(user_context, frame_idx);
    if(handle->presource_frames[frame_idx].update)
       return HEX_SUCCESS;

    // Get the necessary Parameters from DMA Context
    roi_width = handle->presource_frames[frame_idx].fold_width;
    roi_height = handle->presource_frames[frame_idx].fold_height;
    padding = handle->presource_frames[frame_idx].padding;
    is_ubwc = handle->presource_frames[frame_idx].is_ubwc;
    frame_width = handle->presource_frames[frame_idx].frame_width;
    frame_height = handle->presource_frames[frame_idx].frame_height;
    frame_stride = handle->presource_frames[frame_idx].frame_stride;
    plane = handle->presource_frames[frame_idx].plane;
    ncomponents = ((plane==LUMA_COMPONENT)|| (plane==CHROMA_COMPONENT))?1:2;
    nfolds = handle->presource_frames[frame_idx].num_folds;
    chroma_type = handle->presource_frames[frame_idx].chroma_type;
    luma_type  = handle->presource_frames[frame_idx].luma_type;
    
    t_dma_pix_align_info pst_roi_size;
    pst_roi_size.u16W = roi_width;
    pst_roi_size.u16H = roi_height;
    luma_stride = dma_get_stride(luma_type, is_ubwc, pst_roi_size);
    chroma_stride = dma_get_stride(chroma_type, is_ubwc, pst_roi_size);
   
    //Check for dma_allocation 
    bool dma_allocate;
    if (handle->allocate_dma(user_context, frame, &dma_allocate) == -1) {
        error(user_context) << "Undefined Error";
        return HEX_ERROR;
    }

    if (dma_allocate) {
        
        dma_handle = dma_allocate_dma_engine();
        
        if (dma_handle == 0) {
            error(user_context) << "halide_hexagon_dma_device_malloc:Failed to allocate the read DMA engine.\n";
            return HEX_ERROR;
        }
        
        if (handle->set_dma_handle(user_context, dma_handle, frame)) {
            error(user_context) << "Function failed to set DMA Handle to DMA context \n";
            return HEX_ERROR;
        }
    } else {
        //An Allocated DMA Already Exists Re-use it
        bool read = handle->pframe_table[frame_idx].read;
        if (read) {
            dma_handle = handle->get_read_handle(user_context, frame);
        } else {
            dma_handle = handle->get_write_handle(user_context, frame);
        }
    }
  
    cache_mem_t* cache_addr = (cache_mem_t *) roi_buf->host;
    //Now We have Allocated the Fold and will link it to the Frame
    if (handle->set_storage_linkage(user_context, frame, (uintptr_t)cache_addr->fold_vaddr, cache_addr->fold_idx)) {
        error(user_context) << "Function failed to Link Frame and Fold Storage \n";
        return HEX_ERROR;
    }

    if (handle->get_tcm_desc_params(user_context, (uintptr_t)cache_addr->fold_vaddr, &region_tcm, &tcm_size, &tcm_desc_vaddr, &region_tcm_desc, &region_tcm_desc_size)) {
        error(user_context) << "Function failed to Link Frame and Fold Storage \n";
        return HEX_ERROR;
    } 

    //Populate Work Descriptors and Prepare DMA
    //#################################################################
    t_dma_prepare_params params;
    params.handle = dma_handle;
    params.host_address =  frame;
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
    params.num_folds = nfolds;

    nRet = dma_prepare_for_transfer(params);
    if(nRet != QURT_EOK) {
        error(user_context) << "Error in Preparing for DMA Transfer\n";
        return nRet;
    }
    handle->presource_frames[frame_idx].update = true; 
    return nRet;
}


int halide_buffer_copy(void *user_context, halide_buffer_t *frame_buf, void *ptr, halide_buffer_t *roi_buf)
{
    int nRet;
    const halide_device_interface_t* dma_device_interface = halide_hexagon_dma_device_interface();

    int x = roi_buf->dim[0].min;
    int y = roi_buf->dim[1].min;
    int w = roi_buf->dim[0].extent;
    int h = roi_buf->dim[1].extent;
  
    if (frame_buf->device == 0) {
        return HEX_ERROR;
    }

    if (roi_buf->host == 0) {
         return HEX_ERROR;
    } 
 
    dma_context handle = reinterpret_cast<dma_context>(frame_buf->device);
    cache_mem_t* cache_addr = (cache_mem_t *)roi_buf->host;
 
    nRet = halide_hexagon_dmart_update(user_context, frame_buf, roi_buf);
    if (nRet != 0) {
        error(user_context) << "Failed to update DMA. The error code is: " << nRet <<"\n";
        return nRet;
    }
    
    nRet = handle->set_host_roi(user_context, cache_addr->fold_vaddr, x, y, w, h, 0);
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

