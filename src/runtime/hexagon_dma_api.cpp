/**This file contains the implementation
* of adaptor class */

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

//Internal Function
static inline int halide_hexagon_dma_cache_free(void* user_context, t_mem_desc& cache_mem) {
    // Release the TCM regions that were locked.
    // This can also be done through the firmware (using the nDmaWrapper_UnlockCache function).
    int nRet  = dma_unlock_cache(cache_mem.tcm_vaddr, cache_mem.tcm_size);
    if (nRet != QURT_EOK) {
        error(user_context) << "QURT TCM unlock failed due to QURT_EALIGN \
                          ERROR misaligned u32Size = " << cache_mem.tcm_size << "\n";
        return HEX_ERROR;
    }
    nRet  = dma_unlock_cache(cache_mem.desc_vaddr, cache_mem.desc_size);
    if (nRet != QURT_EOK) {
        error(user_context) << "QURT TCM descriptor unlock failed QURT_EALIGN ERROR misaligned u32Size = " << cache_mem.desc_size << "\n";
        return nRet;
    }
    // Delete all regions that were allocated.
    dma_delete_mem_region(cache_mem.tcm_region);
    dma_delete_mem_region(cache_mem.desc_region);

    return HEX_SUCCESS;
}

int halide_hexagon_dma_comp_get(void *user_context, halide_buffer_t *roi_buf, halide_hexagon_dma_user_component_t &comp) {

    if(roi_buf->dim[2].extent > 1) {
        comp = BOTH_LUMA_CHROMA;
    }
    else if (roi_buf->dim[2].extent == 1 && roi_buf->dim[2].min <= roi_buf->dim[2].stride) {
        comp = LUMA_COMPONENT;
    }
    else if (roi_buf->dim[2].extent == 1 && roi_buf->dim[2].min > roi_buf->dim[2].stride) {
        comp = CHROMA_COMPONENT;
    }
    else {
        return HEX_ERROR;
    }

    return HEX_SUCCESS;;
}

void* halide_hexagon_dma_memory_alloc(void* user_context, p_dma_context pdma_context, halide_hexagon_dma_user_component_t comp,
                                                 int w, int h, int s, int num_fold, bool padding, int type) {
    
    int nRet = HEX_SUCCESS;
    int ncomponents = 0;
    int nfolds = 0;
    ncomponents = ((comp==LUMA_COMPONENT)|| (comp==CHROMA_COMPONENT))?1:2;
    nfolds = num_fold;

    uintptr_t region_tcm;
    uintptr_t region_tcm_desc;
    uintptr_t tcm_buf_vaddr, tcm_desc_vaddr;
    qurt_mem_pool_t pool_tcm;

    t_eDmaFmt chroma_type,luma_type;
    chroma_type = type2_dma_chroma[type];
    luma_type = type2_dma_luma[type];

    int padd_factor = padding ? 2:1;
    float type_factor = type_size[type];
    int fold_buff_size = h*s*nfolds*padd_factor*type_factor;
    qurt_size_t tcm_buf_size = fold_buff_size;
    t_eDmaFmt ae_fmt_id[2] = {luma_type,chroma_type};

    qurt_size_t region_tcm_desc_size = dma_get_descriptor_size(ae_fmt_id, ncomponents, nfolds);

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
    if (nRet!=QURT_EOK) {
        error(user_context) << "Failed to attach the TCM memory region. The error code is:" << nRet << "\n" ;
        return NULL;
    }

    nRet = dma_allocate_cache(pool_tcm, region_tcm_size, &region_tcm, &tcm_buf_vaddr);
    if (nRet!=QURT_EOK) {
        error(user_context) << "QURT TCM lock failed due to QURT_EALIGN ERROR  \
               misaligned u32Size = " << region_tcm_size << "\n";
        return NULL;
    }

    nRet = dma_allocate_cache(pool_tcm, region_tcm_desc_size, &region_tcm_desc, &tcm_desc_vaddr);
    if (nRet!=QURT_EOK) {
        error(user_context) << "QURT TCM lock failed due to QURT_EALIGN ERROR  \
                        misaligned u32Size = " << region_tcm_desc_size << "\n";
        return NULL;
    }
    t_mem_desc cache_mem;
    cache_mem.desc_region = region_tcm_desc;
    cache_mem.desc_size = region_tcm_desc_size;
    cache_mem.desc_vaddr = tcm_desc_vaddr;
    cache_mem.tcm_region = region_tcm;
    cache_mem.tcm_size = region_tcm_size;
    cache_mem.tcm_vaddr = tcm_buf_vaddr;

    int fold_idx = 0;
    nRet = halide_hexagon_dmart_set_fold_storage(user_context, pdma_context, cache_mem.tcm_vaddr, cache_mem.tcm_region,
              cache_mem.tcm_size, cache_mem.desc_vaddr, cache_mem.desc_region, cache_mem.desc_size, &fold_idx);

    if(nRet != 0) {
        error(user_context) << "Failed to attach memory. The error code is: " << nRet <<"\n";
        nRet = halide_hexagon_dma_cache_free(user_context, cache_mem);     // free l2$ above
        if(nRet != 0) {
            error(user_context) << "Failed to free host memory. The error code is: " << nRet <<"\n";
        }
        return NULL;
    }
    return (void*)tcm_buf_vaddr;
}

int  halide_hexagon_dma_memory_free(void* user_context, p_dma_context pdma_context, void* vret) {
    if (vret == NULL) {
        return HEX_ERROR;
    }

    halide_assert(user_context, pdma_context != NULL);
    uintptr_t fold_addr = (uintptr_t)vret;
    // Free the Allocated Qurt Memory Regions
    uintptr_t tcm_region,desc_region;
    uintptr_t desc_va;
    qurt_size_t desc_size, tcm_size;
    if (halide_hexagon_dmart_get_tcm_desc_params(user_context, pdma_context, fold_addr, &tcm_region,
                   &tcm_size, (uintptr_t *)&desc_va,&desc_region, &desc_size)) {
        error(user_context) << "Function failed to get TCM Desc Params  \n";
        return HEX_ERROR;
    }
    t_mem_desc cache_mem;
    cache_mem.desc_region = desc_region;
    cache_mem.desc_size = desc_size;
    cache_mem.desc_vaddr = desc_va;
    cache_mem.tcm_region = tcm_region;
    cache_mem.tcm_size = tcm_size;
    cache_mem.tcm_vaddr = fold_addr;

    int nRet = halide_hexagon_dma_cache_free(user_context, cache_mem);
    if (nRet != 0) {
        error(user_context) << "Failed to free host memory. The error code is: " << nRet <<"\n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

static inline int hexagon_dma_prepare(void* user_context, halide_buffer_t* inframe_buf, halide_buffer_t* roi_buf) {
    int nRet;
    p_dma_context handle = reinterpret_cast<p_dma_context>(inframe_buf->device);
    ///////////////////////////////////////////////////////////////////////////
    //Assumption: We only have one frame currently in the dma context handle
    /////////////////////////////////////////////////////////////////////////
    uintptr_t  frame = halide_hexagon_dmart_get_frame(user_context, handle);

    //Get Frame details for populating work descriptors
    //#################################################################
    t_dma_prepare_params params;
    halide_hexagon_dmart_get_prepare_params(user_context, handle, frame, &params);

    //Get Fold Storage Details
    uintptr_t cache_addr = (uintptr_t) roi_buf->host;
    //Assumption No Folding: so fold id to be 0
    if (halide_hexagon_dmart_set_storage_linkage(user_context, handle, frame, cache_addr, 0)) {
        error(user_context) << "Function failed to Link Frame and Fold Storage \n";
        return HEX_ERROR;
    }

    uintptr_t region_tcm;
    uintptr_t region_tcm_desc;
    uintptr_t tcm_desc_vaddr;
    qurt_size_t region_tcm_desc_size;
    qurt_size_t tcm_size;
    if (halide_hexagon_dmart_get_tcm_desc_params(user_context, handle, cache_addr,\
            &region_tcm, &tcm_size, &tcm_desc_vaddr, &region_tcm_desc, &region_tcm_desc_size)) {
        error(user_context) << "Function failed to Link Frame and Fold Storage \n";
        return HEX_ERROR;
    }
    //Prepare for transfer
    //#################################################################
    params.desc_address = tcm_desc_vaddr;
    params.desc_size = region_tcm_desc_size;
    nRet = dma_prepare_for_transfer(params);
    if(nRet != QURT_EOK) {
        error(user_context) << "Error in Preparing for DMA Transfer\n";
        return nRet;
    }
    halide_hexagon_dmart_set_update(user_context, handle, frame);
    return nRet;
}

int halide_hexagon_dma_update(void *user_context, halide_buffer_t *inframe_buf, halide_buffer_t *roi_buf) {

    int nret;
    int nCircularFold;
    int w,h,s;
    bool update = false;
    halide_assert(user_context, roi_buf->dimensions == 3);
    halide_assert(user_context, inframe_buf->device != NULL);
    halide_assert(user_context, roi_buf->host != NULL);

    halide_hexagon_dma_user_component_t comp;
    halide_hexagon_dma_comp_get(user_context, roi_buf, comp);
    p_dma_context handle = reinterpret_cast<p_dma_context>(inframe_buf->device);

    ///////////////////////////////////////////////////////////////////////////
    //Assumption: We only have one frame currently in the dma context handle
    /////////////////////////////////////////////////////////////////////////
    uintptr_t  frame = halide_hexagon_dmart_get_frame(user_context, handle);
    if(halide_hexagon_dmart_get_update(user_context, handle, frame, update)) {
        return HEX_ERROR;
    }
    if(update) {
        return HEX_SUCCESS;
    }
    ////////////////////////////////////////////////////////////////////////////
    //Set the component for transfer
    nret = halide_hexagon_dmart_set_component(user_context, handle, frame, comp);
    if(nret != 0) {
        error(user_context) << "Failed to Set component. The error code is: " << nret <<"\n";
        return nret;
    }
    ////////////////////////////////////////////////////////////////////////////////

    /////////////////////////////////////////////////////////////////////////////////
    // Set ROI details for the transfer
    nCircularFold =1;
    // Divide Frame to predefined tiles in the Horizontal Direction
    w = roi_buf->dim[0].extent;
    // Divide Frame in to predefined tiles in vertical Direction
    h = roi_buf->dim[1].extent;
    // Each tile is again vertically split in to predefined DMA transfers
    // Stride is aligned to Predefined Value
    s = roi_buf->dim[1].stride;

    nret = halide_hexagon_dmart_set_max_fold_storage(user_context, handle, frame, w, h, s, nCircularFold);
    if(nret != 0) {
        error(user_context) << "Failed to Set Fold Storage. The error code is: " << nret <<"\n";
        return nret;
    }
    ////////////////////////////////////////////////
    //Allocate DMA Engine through device malloc
    ////////////////////////////////////////////////
    const halide_device_interface_t* dma_device_interface = halide_hexagon_dma_device_interface();

    int nRet = dma_device_interface->impl->device_malloc(user_context, inframe_buf);
    if(nRet != 0) {
        error(user_context) << "Failed to allocate engines for dma The error code is: " << nRet <<"\n";
        return nRet;
    }
    /////////////////////////////////////////////////////////////////////////////////////
    //Prepare the DMA For transfer
    nret = hexagon_dma_prepare(user_context, inframe_buf, roi_buf);
    if(nret != 0) {
        error(user_context) << "Failed to attach a device memory for the Tile. The error code is: " << nret <<"\n";
        return nret;
    }
    ///////////////////////////////////////////////////////////////////////////////////////////
    return nret;
}

