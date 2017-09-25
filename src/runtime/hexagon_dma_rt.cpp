/**
 *  This file contains the definitions of the Halide hexagaon DMA transfer runtime functions
 * These functions will use the parameters passed by the user through Halide pipeline and populate
 * the Hexagon dma context structure, the details in this structure will ultimately be used by hexagon
 * dma device interface for invoking actual DMA transfer  */
//*******************************************************************************
// * Include files
// *******************************************************************************
#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "printer.h"
#include "hexagon_dma_device_shim.h"
#include "hexagon_dma_context.h"
#include "hexagon_dma_api.h"
#include "HalideRuntimeHexagonDma.h"
using namespace Halide::Runtime::Internal::Qurt;

/**
 *  Table to Get DMA Chroma format for the user type selected
 */
const t_eDmaFmt type2_dma_chroma[6]={eDmaFmt_NV12_UV, eDmaFmt_NV12_UV, eDmaFmt_P010_UV, eDmaFmt_TP10_UV, eDmaFmt_NV124R_UV, eDmaFmt_NV124R_UV};

/**
 *  Table to Get DMA Luma format for the user type selected
 */
const t_eDmaFmt type2_dma_luma[6]={eDmaFmt_NV12_Y, eDmaFmt_NV12_Y, eDmaFmt_P010_Y, eDmaFmt_TP10_Y, eDmaFmt_NV124R_Y, eDmaFmt_NV124R_Y};

/**
 * Table for accounting the dimension for various Formats
 */
const float type_size[6]={1, 1, 1, 0.6667, 1, 1};

/**
 * is_buffer_read
 * Find if data stream is input or output */
int DmaContext::is_buffer_read(void* user_context, uintptr_t frame, bool* is_read) {

    int index = -1;

    for (int i=0; i < nframes; i++) {
        if ((index==-1) && (pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }

    if (index != -1) {
        *is_read = pframe_table[index].read;
    } else {
        error(user_context) << "The frame doesn't exist \n";
        return HEX_ERROR;
    }

    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_get_fold_size
 * Get Fold Size */
int DmaContext::get_fold_size(void* user_context, uintptr_t frame) {

    int index = -1;

    //Search the frame in the frame Table
    for (int i=0; i<nframes; i++) {
        if ((index==-1)&&(pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }

    if (index != -1) {
        return (presource_frames[index].fold_buff_size);
    } else {
        //The frame Doesnt Exist
        error(user_context) << "The frame doesnt exist \n";
        return 0;
    }
}

/**
 *  halide_hexagon_dmart_get_num_components
 * The Function will return the Number of Components (Planes ) in the frame
 * will retun 1 - for Y plane
 * will retun 1 - for UV plane
 * will retun 2 - for both Y and UV planes */
int DmaContext::get_num_components (void* user_context, uintptr_t frame) {
    int index;
    int i;

    // Serch if the frame Already Exists
    index = -1;
    for (i=0 ;i < nframes; i++) {
        if ((index==-1)&& (pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }
    
    int ncomponents = -1;
    if (index==-1) {
        //Error the frame index Doesnt Exist
        error(user_context) << "The frame with the given VA doesnt exist \n";
    } else {
        // frame Exists get the number of components
        int plane = presource_frames[index].plane;
        //Return 1 for both Chroma and Luma Components
        //Return 2 in case of Other i.e having both Planes
        ncomponents = ((plane ==LUMA_COMPONENT)||(plane==CHROMA_COMPONENT))?1:2;
    }
    return ncomponents;
}

/**
 * halide_hexagon_dmart_allocate_dma
 * Check if DMA is already allocated */
int DmaContext::allocate_dma(void* user_context, uintptr_t frame, bool *dma_allocate) {


    int index = -1;
    //Search the frame in the frame Table
    for (int i=0;i<nframes;i++) {
        if ((index==-1) && (pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index!=-1) {
        //frame Exist
        error(user_context) << "pdma_context->frame_cnt " << frame_cnt << "\n";
        int set_id = pframe_table[index].dma_set_id;
        int engine_id =  pframe_table[index].dma_engine_id;
        error(user_context) << "set_id " << set_id <<  "engine_id " << engine_id << "\n";
        if (pframe_table[index].read) {
            // Read Engine
            if (pset_dma_engines[set_id].pdma_read_resource[engine_id].session.dma_handle == NULL) {
                *dma_allocate = true;
            }
        } else {
            // Write Engine
            if (pset_dma_engines[set_id].pdma_write_resource[engine_id].session.dma_handle == NULL){
                *dma_allocate = true;
            }
        }
    } else {
        // The frame Doesnt Exist
        error(user_context) << "The frame doesnt exist \n";
        *dma_allocate = false;
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_set_dma_handle
 * Set DMA Handle called by device interface */
int DmaContext::set_dma_handle(void* user_context, void* handle, uintptr_t frame) {
    int index = -1;
    //Search the frame in the frame Table
    for (int i=0;i<nframes;i++) {
        if ((index==-1) && (pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index!=-1) {
        //frame Exist
        int set_id = pframe_table[index].dma_set_id;
        int engine_id =  pframe_table[index].dma_engine_id;

        if (pframe_table[index].read) {
           pset_dma_engines[set_id].pdma_read_resource[engine_id].session.dma_handle = handle;
        } else {
            pset_dma_engines[set_id].pdma_write_resource[engine_id].session.dma_handle = handle;
        }
    } else {
        //The frame Doesnt Exist
        error(user_context) << "The frame doesnt exist \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_get_read_handle
 * Get Handle for output buffers  */
void* DmaContext::get_read_handle(void* user_context, uintptr_t frame) {

    int index = -1;
    //Search the frame in the frame Table
    for (int i=0;i<nframes;i++) {
        if ((index==-1) && (pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index!=-1) {
        //frame Exist
        int set_id = pframe_table[index].dma_set_id;
        int engine_id =  pframe_table[index].dma_engine_id;
        return pset_dma_engines[set_id].pdma_read_resource[engine_id].session.dma_handle;
    } else {
        //The frame Doesnt Exist
        error(user_context) << "The frame doesnt exist \n";
        return NULL;
    }
}

/**
 * halide_hexagon_dmart_get_write_handle
 * Get Handle for output buffers  */
void* DmaContext::get_write_handle(void* user_context, uintptr_t frame) {
    int index = -1;
    //Search the frame in the frame Table
    for (int i=0;i<nframes;i++) {
        if ((index==-1) && (pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index!=-1) {
        //frame Exist
        int set_id = pframe_table[index].dma_set_id;
        int engine_id =  pframe_table[index].dma_engine_id;
        return pset_dma_engines[set_id].pdma_write_resource[engine_id].session.dma_handle;
    } else {
        // The frame Doesnt Exist
        error(user_context) << "The frame doesnt exist \n";
        return NULL;
    }
}

/**
 * halide_hexagon_dmart_set_fold_storage
 * Link the fold with the frame */
int DmaContext::set_fold_storage(void* user_context, uintptr_t addr, uintptr_t tcm_region, qurt_size_t size,
                                          uintptr_t desc_va, uintptr_t desc_region, qurt_size_t desc_size, int *fold_id) {

    pfold_storage[fold_cnt].fold_virtual_addr = addr;
    pfold_storage[fold_cnt].in_use = 0;
    pfold_storage[fold_cnt].ping_phys_addr = dma_lookup_physical_address(addr);
    pfold_storage[fold_cnt].tcm_region = tcm_region;
    pfold_storage[fold_cnt].desc_virtual_addr    = desc_va;
    pfold_storage[fold_cnt].desc_region = desc_region;
    pfold_storage[fold_cnt].size_desc = desc_size;
    pfold_storage[fold_cnt].size_tcm = size;
    *fold_id = fold_cnt;
    fold_cnt++;
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_get_update_params
 * The params needs for updating descriptors */
int DmaContext::get_update_params(void* user_context, uintptr_t buf_addr, t_dma_move_params* param) {

    int store_id = -1;
    for (int i=0; i<fold_cnt; i++) {
        if ((store_id==-1) && (pfold_storage[i].fold_virtual_addr==buf_addr)) {
            store_id = i;
        }
    }
    if (store_id!=-1) {
        param->yoffset = pfold_storage[store_id].roi_yoffset;
        param->roi_height = pfold_storage[store_id].roi_height ;
        param->xoffset = pfold_storage[store_id].roi_xoffset;
        param->roi_width = pfold_storage[store_id].roi_width;
        // the tcm address is updated given the index of the buffer to use (ping/pong) which alternates on every dma transfer.
        param->ping_buffer = pfold_storage[store_id].ping_phys_addr;
        param->offset =  pfold_storage[store_id].offset;
        param->l2_chroma_offset = pfold_storage[store_id].l2_chroma_offset;
        return HEX_SUCCESS;
    } else {
        return HEX_ERROR;
    }
}

/**
 * halide_hexagon_dmart_get_tcm_desc_params
 * the descriptors need to be saved for
 * unlocking cache and free afterwards  */
int  DmaContext::get_tcm_desc_params(void* user_context, uintptr_t dev_buf, uintptr_t *tcm_region, qurt_size_t *size_tcm, \
                                             uintptr_t *desc_va, uintptr_t *desc_region, qurt_size_t *desc_size) {


    int store_id = -1;
    for (int i=0;i<fold_cnt;i++) {
        if ((store_id==-1) && (pfold_storage[i].fold_virtual_addr==dev_buf)) {
            store_id = i;
        }
    }
    if (store_id != -1) {
        *tcm_region  = pfold_storage[store_id].tcm_region;
        *desc_va     = pfold_storage[store_id].desc_virtual_addr;
        *desc_region = pfold_storage[store_id].desc_region;
        *desc_size   = pfold_storage[store_id].size_desc;
        *size_tcm    = pfold_storage[store_id].size_tcm;
    } else {
        // The fold Buffer doesnt exist
        error(user_context) << " Device Buffer Doesnt exist \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_get_last_frame
 * We keep count of frames to avoid
 * costly alloc/free of DMA Resources */
int DmaContext::get_last_frame(void* user_context, uintptr_t frame, bool* last_frame) {

    int index = -1;
    // Search the frame in the frame Table
    for (int i=0; i<nframes; i++) {
        if ((index==-1) && (pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index != -1) {
        // frame Exist
        int set_id = pframe_table[index].dma_set_id;
        int engine_id =  pframe_table[index].dma_engine_id;
        if (pframe_table[index].read) {
            *last_frame = pset_dma_engines[set_id].pdma_read_resource[engine_id].pframe->end_frame;
        } else {
            *last_frame = pset_dma_engines[set_id].pdma_write_resource[engine_id].pframe->end_frame;
        }
    } else {
        // The fold Buffer doesnt exist
        error(user_context) << " The frame doesnt exist \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}



/**
 *  halide_hexagon_dmart_get_frame_index
 * Get Frame Index of the Frame in the frame context  */
int DmaContext::get_frame_index(void* user_context) {

    return current_frame_index;
}

uintptr_t DmaContext::get_frame(void* user_context, int frame_index) {

   if (frame_index != -1) {
      return pframe_table[frame_index].frame_addr;
   }
   else {
        return NULL;
   }
}

/**
 *  halide_hexagon_dmart_set_host_frame
 * set frame type
 * frame = VA of frame buffer
 * type = NV12/NV124R/P010/UBWC_NV12/TP10/UBWC_NV124R
 * frameID = frameID from the Video to distinguish various frames
 * d = frame direction 0:read, 1:write
 * w = frame width in pixels
 * h = frame height in pixels
 * s = frame stride in pixels
 * last = last frame 0:no 1:yes *optional*
 * inform dma it is last frame of the session, and can start freeing resource when requested
 * at appropriate time (on last SW thread of that resource set) */
int DmaContext::set_host_frame (void* user_context, uintptr_t  frame, int type, int d, int w, int h, int s, int last) {
    int free_dma;
    int free_set;
    int i;
    int nengines;
    t_dma_resource *pdma_resource;
    bool flag;
    t_resource_per_frame *pframe_context;
    
    int nRet;
    bool is_ubwc_dst;
    t_eDmaFmt efmt_chroma;
    t_dma_pix_align_info pix_align_info;

    //Check for valid type and alignment requirements
    //Type should be one of the entries in Union t_eUserFmt
    if ((type > 5) || (type < 0)) {
        //Error Not a Valid type
        error(user_context) << "the frame type is invalid in dmaapp_attach_context \n";
        return HEX_ERROR;
    }

    is_ubwc_dst = ((type==1)||(type==5))?1:0;
    //Convert the User provided frame type to UBWC DMA chroma type for alignment check
    efmt_chroma = type2_dma_chroma[type];
    nRet = dma_get_format_alignment(efmt_chroma, is_ubwc_dst, pix_align_info);
    if (nRet != QURT_EOK) {
        if (h%(pix_align_info.u16H) != 0 || w%(pix_align_info.u16W) != 0) {
            error(user_context) << "frame width and height must be aligned to W=" << pix_align_info.u16W << "H=" <<  pix_align_info.u16H << "\n";
            return HEX_ERROR;
        }
        return HEX_ERROR;
    }

    // Check if there are any Free H/W Threads available
    free_set =-1;
    for (i=0; i < NUM_HW_THREADS; i++) {
        if ((!pset_dma_engines[i].in_use) && (free_set ==-1)) {
        // A Hardware Thread with ID = i is Free
        free_set = i;
        }
    }
    if (-1==free_set) {
        // None of the Hardware Threads are free
        error(user_context) << "None of the Hardware threads are Free \n";
        // Return a suitable Error ID, currently returning -1 
        return -1;
    } else {
        // If H/W threads are free we can use them for DMA transfer 
        // Using the DMAset_id same as the H/W Thread index`
        pset_dma_engines[free_set].dma_set_id = free_set;
        uintptr_t pframe;
        // Check if the frame Entry Exist by searching in the frame Table
        pframe = 0;
        for (i=0;i<nframes;i++) {
            if (frame==pframe_table[i].frame_addr) {
                // frame already Exist
                pframe = frame;
            }
        }
        if (pframe==0) {
            // The frame Doesnt Exist and we need to add it
            if (d==0) {
            // Current frame is to be read by DMA
            pdma_resource = pset_dma_engines[free_set].pdma_read_resource;
            nengines = pset_dma_engines[free_set].ndma_read_engines;
            //Set the Flag that the Engine is used for Read
            flag = false;
            } else {
                // Current frame is to be Written by DMA
                pdma_resource = pset_dma_engines[free_set].pdma_write_resource;
                nengines = pset_dma_engines[free_set].ndma_write_engines;
                //Set the Flag that the Engine is used for Read
                flag = true;
            }
            // using a value to ease search
            free_dma =-1;
            // Search if any of the Engine is Free
            for (i =0;i< nengines;i++) {
                if (!pdma_resource[i].in_use && (free_dma==-1)) {
                    free_dma = i;
                }
            }
            if (free_dma!=-1) {
                pframe_context = &(presource_frames[frame_cnt]);
                // Populate the frame Details in the frame Structure
                pframe_context->frame_virtual_addr = frame;
                pframe_context->frame_width = w;
                pframe_context->frame_height = h;
                pframe_context->frame_stride = s;
                pframe_context->end_frame = last;
                pframe_context->type = type;
                pframe_context->chroma_type = type2_dma_chroma[type];
                pframe_context->luma_type = type2_dma_luma[type];
                pframe_context->frame_index = frame_cnt;
                pframe_context->update = false;
                pdma_resource[free_dma].pframe = pframe_context;
                pdma_resource[free_dma].dma_write = flag;
                pdma_resource[free_dma].in_use = true;
                pset_dma_engines[free_set].in_use = true;
                pframe_table[frame_cnt].frame_addr = frame;
                pframe_table[frame_cnt].dma_set_id = free_set ;
                pframe_table[frame_cnt].dma_engine_id = free_dma;
                pframe_table[frame_cnt].frame_index = frame_cnt;
                pframe_table[frame_cnt].read = (d==0)? 1:0;
                current_frame_index = frame_cnt;
                frame_cnt++;
            } else {
                //Error No Free DMA Engines
                error(user_context) << "Error No free DMA Engines for Read operation \n";
                // Return a suitable Error Code currently returning -1
                return HEX_ERROR;
            }
        } else {
            // frame Already Exists an Error State
            error(user_context) << "The frame with the given VA is already registered for DMA Transfer \n";
            // Set up Error Code and return respective value, Currently REturning -1
            return HEX_ERROR;
        }
    }
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_set_padding
 * set for dma padding in L2$ (8bit in DDR, 16bit in L2$) - padding '0' to LSb
 * frame = VA of frame buffer
 * flag = 0:no padding, 1:padding  */
int DmaContext::set_padding (void* user_context, uintptr_t  frame, int flag) {
    int index,i,frame_idx;
    
    // Serch if the frame Already Exists
    index = -1;
    for (i=0 ;i< nframes; i++) {
        if ((-1==index)&& (pframe_table[i].frame_addr==frame)) {
            index = i;
         }
    }
    if (index==-1) {
        //Error the frame index Doesnt Exist 
        error(user_context) << "The frame with the given VA doesnt exist \n";
        //Currently Returning -1, we have to setup Error Codes
        return HEX_ERROR;
    } else {
        //frame Exists populate the padding Flag to the frame Context
        frame_idx = pframe_table[index].frame_index;
        presource_frames[frame_idx].padding = (flag)? 1:0;
    }
    return HEX_SUCCESS;
}

/**
 *  halide_hexagon_dmart_set_component
 * set which component to dma
 * frame = VA of frame buffer
 * plane = Y/UV  */
int DmaContext::set_component (void* user_context, uintptr_t frame, int plane) {
    int index = 0;
    int i = 0;
    int frame_idx = 0;


    index = -1;
    for (i=0 ;i< nframes; i++) {
        if ((index==-1)&& (pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }
    if (index==-1) {
        // Error the frame index Doesnt Exist 
        error(user_context) << "The frame with the given VA doesnt exist \n";
        //Currently Returning -1, we have to setup Error Codes
        return HEX_ERROR;
    } else {
        // frame Exists populate the padding Flag to the frame Context 
        frame_idx = pframe_table[index].frame_index;
        presource_frames[frame_idx].plane = plane;
    }
    return HEX_SUCCESS;
}

/**
 *  _DmaRT_SetStorageTile
 * specify the largest folding storage size to dma tile and folding instances; to be used when malloc of device memory (L2$)
 * frame = VA of frame buffer
 * w = fold width in pixels
 * h = fold height in pixels
 * s = fold stride in pixels
 * n = number of folds (circular buffers)
 * example ping/pong fold_storage for NV12; for both Y and UV, Y only, UV only  */
int DmaContext::set_max_fold_storage (void* user_context, uintptr_t  frame, int w, int h, int s, int n) {
    int frame_idx;
    int i,index;
    int padd_factor;
    float type_factor;
    
    // Search if the frame Already Exists
    index = -1;
    for (i=0; i < nframes; i++) {
        if ((-1==index)&& (pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }
    if (index==-1) {
        // Error the frame index doesnt exist
        error(user_context) << "The frame with the given VA doesnt exist \n";
        // Currently Returning -1, we have to setup Error Codes
        return HEX_ERROR;
    } else {
        // frame Exists populate the padding Flag to the frame Context 
        frame_idx = pframe_table[index].frame_index;
        presource_frames[frame_idx].fold_width = w;
        presource_frames[frame_idx].fold_height = h;
        presource_frames[frame_idx].fold_stride =s;
        presource_frames[frame_idx].num_folds = n;
        padd_factor = presource_frames[frame_idx].padding ? 2:1;
        type_factor = type_size[presource_frames[frame_idx].type];
        presource_frames[frame_idx].fold_buff_size = h*s*n*padd_factor*type_factor;
    }
    return HEX_SUCCESS;
}

/**
 *  halide_hexagon_dmart_get_free_fold
 * get fold which is disassociated from frame but
 * not yet deallocated  */
int DmaContext::get_free_fold (void* user_context, bool *free_fold, int* store_id) {
    int i,fold_idx;

// Search if the fold Already Exists and not in use
    fold_idx =-1;
    for (i=0;i < fold_cnt;i++) {
        if ((-1==fold_idx)&& (pfold_storage[i].in_use==false) && (pfold_storage[i].fold_virtual_addr)) {
            //Fold is Free and Already Allocated 
            fold_idx = i;
        }
    }
    if (fold_idx != -1) {
        //A Free Fold Exists and Return the Index
        *free_fold = true;
        *store_id = fold_idx;
    } else {
        //An Already Allocated Free Fold Doesnt Exist
        *free_fold = false;
        *store_id = fold_idx;
        return HEX_ERROR; 
    }
    return HEX_SUCCESS;
}



/**
 *  halide_hexagon_dmart_set_storage_linkage
 * associate host frame to device storage - one call per frame
 * frame = VA of frame buffer
 * fold = VA of device storage
 * store_id = output of storage id  */
int DmaContext::set_storage_linkage (void* user_context, uintptr_t frame, uintptr_t fold, int store_id) {
    int i,index;
    
    halide_assert(user_context, frame!=0);
    halide_assert(user_context, fold!=0);
    // Serch if the frame Already Exists
    index = -1;
    for (i=0 ;i < nframes; i++) {
        if ((index==-1) && (pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }
    // See if the store_id is a valid one or not
    if (store_id > -1) {
        //Set that the the Identified fold is in use
        pfold_storage[store_id].in_use = true;
        // QURT API to get thread ID */
        pfold_storage[store_id].thread_id =  dma_get_thread_id();
        //Associate the fold to frame in frame table
        pframe_table[index].work_buffer_id = store_id;
        //Associate the fold to the frame in frame Context
        presource_frames[index].pwork_buffer = &pfold_storage[store_id];
    } else {
        error(user_context) << "Error from dmart_set_storage_linkage, Invalid fold Index \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_set_device_storage_offset
 * set the offset into folding device storage to dma - one call for each frame, per transaction
 * store_id = storage id, pin point the correct folding storage
 * offset = offset from start of L2$ (local folding storage) to dma - to id the fold (circular buffer)
 * rcs_id = locked resource ID */
int DmaContext::set_device_storage_offset (void* user_context, uintptr_t buf_addr, int offset, int rsc_id) {
    
    int index = -1;
    for (int i=0;i<fold_cnt;i++) {
        if ((index==-1) && (pfold_storage[i].fold_virtual_addr==buf_addr)) {
            index = i;
        }
    }
    if (index!=-1) {
        // The fold Storage Exist 
        pfold_storage[index].offset = offset;
    } else {
        error(user_context) << "The Device Storage Doesnt Exist \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_set_host_roi
 * set host ROI to dma
 * store_id = storage id, pin point the correct folding storage
 * x = ROI start horizontal position in pixels
 * y = ROI start vertical position in pixels
 * w = ROI width in pixels
 * h = ROI height in pixels
 * rsc_id = locked resource ID  */
int DmaContext::set_host_roi (void* user_context, uintptr_t buf_addr, int x, int y, int w, int h, int rsc_id) {
    int type;
    int is_ubwc_dst;
    t_eDmaFmt efmt_chroma;
    int index;
    int i=0;
    t_dma_pix_align_info pix_align_info;
    int store_id = -1;
    

    for (int i=0;i<fold_cnt;i++) {
        if ((-1==store_id) && (pfold_storage[i].fold_virtual_addr==buf_addr)) {
            store_id = i;
        }
    }
    if (store_id != -1) {
        // The fold Storage Exist 
        // Need to Get frame type for checking Alignment requirements
        // Search if the frame Already Exists
        index = -1;
        for (i=0 ;i < nframes; i++) {
            if ((index==-1)&& (pframe_table[i].work_buffer_id==store_id)) {
                index = i;
            }
        }
        type = presource_frames[index].type;
        is_ubwc_dst = ((type==1)||(type==5))?1:0;
        efmt_chroma = type2_dma_chroma[type];
        dma_get_min_roi_size(efmt_chroma, is_ubwc_dst, pix_align_info);

        if (h%(pix_align_info.u16H) != 0 || w%(pix_align_info.u16W) != 0) {
            error(user_context) << "ROI width and height must be aligned to W = " << pix_align_info.u16W \
                        << "and  H = " << pix_align_info.u16H << "\n";
            return HEX_ERROR;
        }

        if (y%(pix_align_info.u16H) != 0 || x%(pix_align_info.u16W) != 0) {
            error(user_context) << "ROI X-position and y-position must be aligned to  W = " << pix_align_info.u16W \
                        << "and  H = " << pix_align_info.u16H << "\n";
            return HEX_ERROR;
        }
        //Store the ROI Details
        pfold_storage[store_id].roi_xoffset=x;
        pfold_storage[store_id].roi_yoffset=y;
        pfold_storage[store_id].roi_width=w;
        pfold_storage[store_id].roi_height=h;
    } else {
        error(user_context) << "The Device Storage Doesnt Exist \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}


/**
 *  halide_hexagon_dmart_clr_host_frame
 * clear frame
 * frame = VA of frame buffer  */
int DmaContext::clr_host_frame (void* user_context, uintptr_t frame) {
    int fold_idx;
    // Serch for the frame Entry
    int index = -1;
    
    for (int i=0; i < nframes; i++) {
    
        if ((index==-1)&& (pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }

    if (index != -1) {
        //Dis-associate frame From fold 
        fold_idx = pframe_table[index].work_buffer_id;
        //Free the fold for recycling 
        pfold_storage[fold_idx].in_use = false;
        //set id 
        int dma_id = pframe_table[index].dma_engine_id;
        int set_id = pframe_table[index].dma_set_id;
        //Free The DMA Engine
        if (pframe_table[index].read) {
            //Clear the Read Resource
            pset_dma_engines[set_id].pdma_read_resource[dma_id].in_use = false;
            memset((void*) pset_dma_engines[set_id].pdma_read_resource[dma_id].pframe, 0, sizeof(t_resource_per_frame));
         } else {
            //Clear the Write Resource
            pset_dma_engines[set_id].pdma_write_resource[dma_id].in_use = false;
            pset_dma_engines[set_id].pdma_write_resource[dma_id].dma_write = false;
            memset((void*) pset_dma_engines[set_id].pdma_write_resource[dma_id].pframe, 0, sizeof(t_resource_per_frame));
        }
        //Free the H/W Thread
        pset_dma_engines[set_id].in_use = false;
        //Clear the frame Context
        memset((void*)&presource_frames[index], 0, sizeof(t_resource_per_frame));
        //Clear the frame Table
        memset((void*)&pframe_table[index], 0, sizeof(t_frame_table));
        //Decrement the frame_cnt (index) as a frame is deleted
        frame_cnt--;
        current_frame_index = 0;
    } else {
        // Error the frame doesnt Exist
        error(user_context) << "frame to be Freed doesnt exist\n";
        //Currently returning -1, but setup the Error codes
        return HEX_ERROR;
    }    
    return HEX_SUCCESS;
}


/**
 *  halide_hexagon_dmaapp_dma_init
 * dma_context  - pointer to Allocated DMA Context
 * nFrmames - Total Number of frames */
int halide_hexagon_dmaapp_dma_init(void *user_context, void *dma_ctxt, int nframes) {
    int i;
    int engine_per_thread = 0;
    int rem_engine = 0;
    dma_context pdma_context;
    halide_assert(user_context, dma_ctxt!=NULL);
    halide_assert(user_context, nframes!=0);
    // Zero Initialize the Buffer
    memset(dma_ctxt,0,sizeof(DmaContext));
    pdma_context = (dma_context)dma_ctxt;
    //Initialize the Number of frames 
    pdma_context->nframes = nframes;
    //Allocate for frameTable 
    pdma_context->pframe_table = (t_frame_table *) malloc(nframes*sizeof(t_frame_table));
    if (pdma_context->pframe_table == 0) {
        //Malloc Failed to Alloc the Required Buff
        // Setup Error Codes currrently returning -1
        error(user_context) << "Malloc Failed to allocate in DMA Init function \n";
        return HEX_ERROR;
    }
    //Init to Zero
    memset(pdma_context->pframe_table, 0, nframes*sizeof(t_frame_table));
    // Allocate the frame Resource Struture
    pdma_context->presource_frames = (t_resource_per_frame *) malloc(nframes*sizeof(t_resource_per_frame));
    if (pdma_context->presource_frames == 0) {
        //Malloc Failed to Alloc the Required Buff
        //Setup Error Codes currrently returning -1
        error(user_context) << "Malloc Failed to allocate in DMA Init function \n";
        return HEX_ERROR;
    }
    //Init to Zero 
    memset(pdma_context->presource_frames, 0, nframes*sizeof(t_resource_per_frame));
    // Number of Folds =  NUM_DMA_ENGINES     
    pdma_context->pfold_storage = (t_work_buffer *) malloc(NUM_DMA_ENGINES * sizeof(t_work_buffer));
    if (pdma_context->pfold_storage==0) {
        //Malloc Failed to Alloc for USer Structure
        //currently returning -1 but need to setup Error Codes
        error(user_context) << "Malloc Failed to allocate in DMA Init function \n";
        return HEX_ERROR;
    }
    //Init to Zero 
    memset(pdma_context->pfold_storage, 0, (NUM_DMA_ENGINES *sizeof(t_work_buffer)));
    // Allocate reamining structures
    // Allocate DMA Read Resources 
    // Equally share across HW threads
    engine_per_thread = NUM_DMA_ENGINES/NUM_HW_THREADS;
    for (i=0; i<NUM_HW_THREADS ; i++) {
        // Equally Share the DMA resources between Read and Write Engine 
        pdma_context->pset_dma_engines[i].pdma_read_resource = (t_dma_resource *) malloc((engine_per_thread/2)*sizeof(t_dma_resource));
        if (pdma_context->pset_dma_engines[i].pdma_read_resource == NULL) {
            //Malloc Failed to Alloc the Required Buff
            // Setup Error Codes currrently returning -1
            error(user_context) << "Malloc Failed to allocate in DMA Init function \n";
            return HEX_ERROR;
        }
        pdma_context->pset_dma_engines[i].ndma_read_engines = (engine_per_thread/2);
        //Init to Zero
        memset(pdma_context->pset_dma_engines[i].pdma_read_resource, 0, (engine_per_thread/2)*sizeof(t_dma_resource));
        //Allocate the Remaining for Write
        rem_engine = engine_per_thread - (engine_per_thread/2);
        pdma_context->pset_dma_engines[i].pdma_write_resource =(t_dma_resource *) malloc(rem_engine*sizeof(t_dma_resource));
        if (pdma_context->pset_dma_engines[i].pdma_write_resource == NULL) {
            //Malloc Failed to Alloc the Required Buff
            // Setup Error Codes currrently returning -1
            error(user_context) << "Malloc Failed to allocate in DMA Init function \n";
            return HEX_ERROR;
        }
        pdma_context->pset_dma_engines[i].ndma_write_engines = rem_engine;
        //Init to Zero 
        memset(pdma_context->pset_dma_engines[i].pdma_write_resource, 0, rem_engine*sizeof(t_dma_resource));
    }
    return HEX_SUCCESS;
}



/**
 * CreateContext check if DMA Engines are available
 * Allocates Memory for a DMA Context
 * Returns Error if DMA Context is not available */
int halide_hexagon_dmaapp_create_context(void* user_context, int nframes) {
    dma_context pdma_context;

    //Check if DMA Driver is Ready and Allocate the DMA Structure
    if (dma_is_dma_driver_ready() == QURT_EOK) {
        //Alloc DMA Context
        pdma_context = (dma_context) malloc(sizeof(DmaContext));
        if (pdma_context != NULL) {
            //initialize dma_context
            halide_hexagon_dmaapp_dma_init(user_context, (void *)pdma_context, nframes);
            halide_hexagon_set_dma_context(user_context, pdma_context);
        } else {
            error(user_context) << "DMA structure Allocation have some issues \n";
            // Setup Error Codes, Currently Returning -1
            return HEX_ERROR;
        }
    } else {
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

/**
 * Delete Context frees up the dma handle if not yet freed
 * and deallocates memory for the userContext */
int halide_hexagon_dmaapp_delete_context(void* user_context) {
    dma_context pdma_context;
    halide_hexagon_get_dma_context(user_context, &pdma_context);
    halide_assert(user_context, pdma_context != NULL);

    //De-associate all the allocated Buffers
    pdma_context->pfold_storage = NULL;
    pdma_context->pframe_table = NULL;
    pdma_context->presource_frames = NULL;
    for (int i=0; i<NUM_HW_THREADS; i++) {
        pdma_context->pset_dma_engines[i].pdma_read_resource = NULL;
        pdma_context->pset_dma_engines[i].pdma_write_resource =NULL;
    }
    //Finally Free the DMA Context     
    pdma_context = NULL;
    return HEX_SUCCESS;
}



