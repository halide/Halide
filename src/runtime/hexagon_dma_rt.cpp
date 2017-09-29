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
#include "halide_hexagon_dma_api.h"
#include "hexagon_dma_api.h"
#include "hexagon_dma_rt.h"


using namespace Halide::Runtime::Internal::Qurt; 

/**
 *  halide_hexagon_dmart_get_frame_index
 * Get Frame Index of the Frame in the frame context  */
int halide_hexagon_dmart_get_frame_index(void* user_context, p_dma_context pdma_context, uintptr_t frame) {
    int index,i;
    halide_assert(user_context, pdma_context != NULL);// Search if the frame Already Exists
    // Serch if the frame Already Exists
    index = -1;
    for (i=0; i<pdma_context->nframes; i++) {
        if ((index==-1)&& (pdma_context->pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }
    return index;
}

/**
 *  halide_hexagon_dmart_set_host_frame
 * set frame type
 * frameID = frameID from the Video to distinguish various frames
 * d = frame direction 0:read, 1:write
 * w = frame width in pixels
 * h = frame height in pixels
 * s = frame stride in pixels
 * last = last frame 0:no 1:yes *optional*
 * inform dma it is last frame of the session, and can start freeing resource when requested
 * at appropriate time (on last SW thread of that resource set) */
int halide_hexagon_dmart_set_host_frame (void* user_context, p_dma_context pdma_context, uintptr_t  frame, int type, int d, int w, int h, int s, int last) {
    int free_dma;
    int free_set;
    int i;
    int nengines;
    t_dma_resource *pdma_resource;
    bool flag;
    t_resource_per_frame *pframe_context;
    
    halide_assert(user_context, pdma_context != NULL);
    // Check if there are any Free H/W Threads available
    free_set =-1;
    for (i=0; i < NUM_HW_THREADS; i++) {
        if ((!pdma_context->pset_dma_engines[i].in_use) && (free_set ==-1)) {
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
        pdma_context->pset_dma_engines[free_set].dma_set_id = free_set;
        uintptr_t pframe;
        // Check if the frame Entry Exist by searching in the frame Table
        pframe = 0;
        for (i=0;i<pdma_context->nframes;i++) {
            if (frame==pdma_context->pframe_table[i].frame_addr) {
                // frame already Exist
                pframe = frame;
            }
        }
        if (pframe==0) {
            // The frame Doesnt Exist and we need to add it
            if (d==0) {
            // Current frame is to be read by DMA
            pdma_resource = pdma_context->pset_dma_engines[free_set].pdma_read_resource;
            nengines = pdma_context->pset_dma_engines[free_set].ndma_read_engines;
            //Set the Flag that the Engine is used for Read
            flag = false;
            } else {
                // Current frame is to be Written by DMA
                pdma_resource = pdma_context->pset_dma_engines[free_set].pdma_write_resource;
                nengines = pdma_context->pset_dma_engines[free_set].ndma_write_engines;
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
                pframe_context = &(pdma_context->presource_frames[pdma_context->frame_cnt]);
                // Populate the frame Details in the frame Structure
                pframe_context->frame_virtual_addr = frame;
                pframe_context->frame_width = w;
                pframe_context->frame_height = h;
                pframe_context->frame_stride = s;
                pframe_context->end_frame = last;
                pframe_context->type = type;
                pframe_context->chroma_type = type2_dma_chroma[type];
                pframe_context->luma_type = type2_dma_luma[type];
                pframe_context->frame_index = pdma_context->frame_cnt;
                pframe_context->update = false;
                pdma_resource[free_dma].pframe = pframe_context;
                pdma_resource[free_dma].dma_write = flag;
                pdma_resource[free_dma].in_use = true;
                pdma_context->pset_dma_engines[free_set].in_use = true;
                pdma_context->pframe_table[pdma_context->frame_cnt].frame_addr = frame;
                pdma_context->pframe_table[pdma_context->frame_cnt].dma_set_id = free_set ;
                pdma_context->pframe_table[pdma_context->frame_cnt].dma_engine_id = free_dma;
                pdma_context->pframe_table[pdma_context->frame_cnt].frame_index = pdma_context->frame_cnt;
                pdma_context->pframe_table[pdma_context->frame_cnt].read = (d==0)? 1:0;
                pdma_context->frame_cnt++;
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
 * flag = 0:no padding, 1:padding  */
int halide_hexagon_dmart_set_padding (void* user_context, p_dma_context pdma_context, uintptr_t  frame, int flag) {
    int index,i,frame_idx;
    halide_assert(user_context, pdma_context != NULL);
    // Serch if the frame Already Exists
    index = -1;
    for (i=0 ;i< pdma_context->nframes; i++) {
        if ((-1==index)&& (pdma_context->pframe_table[i].frame_addr==frame)) {
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
        frame_idx = pdma_context->pframe_table[index].frame_index;
        pdma_context->presource_frames[frame_idx].padding = (flag)? 1:0;
    }
    return HEX_SUCCESS;
}

/**
 *  halide_hexagon_dmart_set_component
 * set which component to dma
 * plane = Y/UV  */
int halide_hexagon_dmart_set_component (void* user_context, p_dma_context pdma_context, uintptr_t frame, int plane) {
    int index = 0;
    int i = 0;
    int frame_idx = 0;

    halide_assert(user_context, pdma_context != NULL);    
    //search if the frame Already Exists
    index = -1;
    for (i=0 ;i< pdma_context->nframes; i++) {
        if ((index==-1)&& (pdma_context->pframe_table[i].frame_addr==frame)) {
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
        frame_idx = pdma_context->pframe_table[index].frame_index;
        pdma_context->presource_frames[frame_idx].plane = plane;
    }
    return HEX_SUCCESS;
}

/**
 *  halide_hexagon_dmart_set_parallel
 * is parallel processing (parallization of inner most loop only; must avoid nested parallelism)
 * threads = number of SW threads (one thread per Halide tile)  */
int halide_hexagon_dmart_set_parallel (void* user_context, p_dma_context pdma_context, int threads) {

    halide_assert(user_context, pdma_context != NULL);
    pdma_context->num_threads = threads;
    return HEX_SUCCESS;
}


/**
 *  _DmaRT_SetStorageTile
 * specify the largest folding storage size to dma tile and folding instances; to be used when malloc of device memory (L2$)
 * n = number of folds (circular buffers)
 * example ping/pong fold_storage for NV12; for both Y and UV, Y only, UV only  */
int halide_hexagon_dmart_set_max_fold_storage (void* user_context, p_dma_context pdma_context, uintptr_t  frame, int w, int h, int s, int n) {
    int frame_idx;
    int i,index;
    int padd_factor;
    float type_factor;
    
    halide_assert(user_context, pdma_context != NULL);
    // Search if the frame Already Exists
    index = -1;
    for (i=0; i < pdma_context->nframes; i++) {
        if ((-1==index)&& (pdma_context->pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }
    if (index==-1) {
        // Error the frame index Doesnt Exist 
        error(user_context) << "The frame with the given VA doesnt exist \n";
        // Currently Returning -1, we have to setup Error Codes
        return HEX_ERROR;
    } else {
        // frame Exists populate the padding Flag to the frame Context 
        frame_idx = pdma_context->pframe_table[index].frame_index;
        pdma_context->presource_frames[frame_idx].fold_width = w;
        pdma_context->presource_frames[frame_idx].fold_height = h;
        pdma_context->presource_frames[frame_idx].fold_stride =s;
        pdma_context->presource_frames[frame_idx].num_folds = n;
        padd_factor = pdma_context->presource_frames[frame_idx].padding ? 2:1;
        type_factor = type_size[pdma_context->presource_frames[frame_idx].type];
        pdma_context->presource_frames[frame_idx].fold_buff_size = h*s*n*padd_factor*type_factor;
    }
    return HEX_SUCCESS;
}

/**
 *  halide_hexagon_dmart_get_free_fold
 * get fold which is disassociated from frame but
 * not yet deallocated  */
int halide_hexagon_dmart_get_free_fold (void* user_context, p_dma_context pdma_context, bool *free_fold, int* store_id) {
    int i,fold_idx;
    halide_assert(user_context, pdma_context != NULL);    
    //Search if the fold Already Exists and not in use
    fold_idx =-1;
    for (i=0;i < pdma_context->fold_cnt;i++) {
        if ((-1==fold_idx)&& (pdma_context->pfold_storage[i].in_use==false) && (pdma_context->pfold_storage[i].fold_virtual_addr)) {
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
 * store_id = output of storage id  */
int halide_hexagon_dmart_set_storage_linkage (void* user_context, p_dma_context pdma_context, uintptr_t frame, uintptr_t fold, int store_id) {
    int i,index;
    halide_assert(user_context, pdma_context != NULL);
    halide_assert(user_context, frame!=0);
    halide_assert(user_context, fold!=0);
    // Serch if the frame Already Exists
    index = -1;
    for (i=0 ;i < pdma_context->nframes; i++) {
        if ((index==-1) && (pdma_context->pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }
    // See if the store_id is a valid one or not
    if (store_id > -1) {
        //Set that the the Identified fold is in use
        pdma_context->pfold_storage[store_id].in_use = true;
        // QURT API to get thread ID */
        pdma_context->pfold_storage[store_id].thread_id =  dma_get_thread_id();
        //Associate the fold to frame in frame table
        pdma_context->pframe_table[index].work_buffer_id = store_id;
        //Associate the fold to the frame in frame Context
        pdma_context->presource_frames[index].pwork_buffer = &pdma_context->pfold_storage[store_id];
    } else {
        error(user_context) << "Error from dmart_set_storage_linkage, Invalid fold Index \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}


/**
 *  halide_hexagon_dmart_set_resource
 * lock dma resource set to thread, max number of resource set is based on available HW threads
 * rsc_id = lock outputs id value, unlock inputs id value */
int halide_hexagon_dmart_set_resource (void* user_context, p_dma_context pdma_context, int lock, int* rsc_id) {
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_set_device_storage_offset
 * offset = offset from start of L2$ (local folding storage) to dma - to id the fold (circular buffer)
 * rcs_id = locked resource ID */
int halide_hexagon_dmart_set_device_storage_offset (void* user_context, p_dma_context pdma_context, uintptr_t buf_addr, int offset, int rsc_id) {
    
    halide_assert(user_context, pdma_context != NULL);
    
    int index = -1;
    for (int i=0;i<pdma_context->fold_cnt;i++) {
        if ((index==-1) && (pdma_context->pfold_storage[i].fold_virtual_addr==buf_addr)) {
            index = i;
        }
    }
    if (index!=-1) {
        // The fold Storage Exist 
        pdma_context->pfold_storage[index].offset = offset;
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
 * rsc_id = locked resource ID  */
int halide_hexagon_dmart_set_host_roi (void* user_context, p_dma_context pdma_context, uintptr_t buf_addr, int x, int y, int w, int h, int rsc_id) {
    int type;
    int is_ubwc_dst;
    t_eDmaFmt efmt_chroma;
    int index;
    int i=0;
    t_dma_pix_align_info pix_align_info;
    int store_id = -1;
    halide_assert(user_context, pdma_context != NULL);

    for (int i=0;i<pdma_context->fold_cnt;i++) {
        if ((-1==store_id) && (pdma_context->pfold_storage[i].fold_virtual_addr==buf_addr)) {
            store_id = i;
        }
    }
    if (store_id != -1) {
        // The fold Storage Exist 
        // Need to Get frame type for checking Alignment requirements
        // Search if the frame Already Exists
        index = -1;
        for (i=0 ;i < pdma_context->nframes; i++) {
            if ((index==-1)&& (pdma_context->pframe_table[i].work_buffer_id==store_id)) {
                index = i;
            }
        }
        type = pdma_context->presource_frames[index].type;
        is_ubwc_dst = ((type==1)||(type==5))?1:0;
        //Convert the User provided frame type to UBWC DMA chroma type for alignment check
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
        pdma_context->pfold_storage[store_id].roi_xoffset=x;
        pdma_context->pfold_storage[store_id].roi_yoffset=y;
        pdma_context->pfold_storage[store_id].roi_width=w;
        pdma_context->pfold_storage[store_id].roi_height=h;
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
int halide_hexagon_dmart_clr_host_frame (void* user_context, p_dma_context pdma_context, uintptr_t frame) {
    int fold_idx;
    halide_assert(user_context, pdma_context != NULL);
    // Serch for the frame Entry
    int index = -1;
    
    for (int i=0; i < pdma_context->nframes; i++) {
    
        if ((index==-1)&& (pdma_context->pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }

    if (index != -1) {
        //Dis-associate frame From fold 
        fold_idx = pdma_context->pframe_table[index].work_buffer_id;
        //Free the fold for recycling 
        pdma_context->pfold_storage[fold_idx].in_use = false;
        //set id 
        int dma_id = pdma_context->pframe_table[index].dma_engine_id;
        int set_id = pdma_context->pframe_table[index].dma_set_id;
        //Free The DMA Engine
        if (pdma_context->pframe_table[index].read) {
            //Clear the Read Resource
            pdma_context->pset_dma_engines[set_id].pdma_read_resource[dma_id].in_use = false;
            memset((void*) pdma_context->pset_dma_engines[set_id].pdma_read_resource[dma_id].pframe, 0, sizeof(t_resource_per_frame));
         } else {
            //Clear the Write Resource
            pdma_context->pset_dma_engines[set_id].pdma_write_resource[dma_id].in_use = false;
            pdma_context->pset_dma_engines[set_id].pdma_write_resource[dma_id].dma_write = false;
            memset((void*) pdma_context->pset_dma_engines[set_id].pdma_write_resource[dma_id].pframe, 0, sizeof(t_resource_per_frame));
        }
        //Free the H/W Thread
        pdma_context->pset_dma_engines[set_id].in_use = false;
        //Clear the frame Context
        memset((void*)&pdma_context->presource_frames[index], 0, sizeof(t_resource_per_frame));
        //Clear the frame Table
        memset((void*)&pdma_context->pframe_table[index], 0, sizeof(t_frame_table));
        //Decrement the frame_cnt (index) as a frame is deleted
        pdma_context->frame_cnt--;
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
 * p_dma_context  - pointer to Allocated DMA Context
 * nFrmames - Total Number of frames */
int halide_hexagon_dmart_dma_init(void *user_context, void *dma_context, int nframes) {
    int i;
    int engine_per_thread = 0;
    int rem_engine = 0;
    t_dma_context *pdma_context;
    halide_assert(user_context, dma_context!=NULL);
    halide_assert(user_context, nframes!=0);
    // Zero Initialize the Buffer
    memset(dma_context,0,sizeof(t_dma_context));
    pdma_context = (t_dma_context *)dma_context;
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
int halide_hexagon_dmart_create_context(void* user_context, p_dma_context* pdma_handle, int nframes) {
    t_dma_context *pdma_context;

    //Check if DMA Driver is Ready and Allocate the DMA Structure
    if (dma_is_dma_driver_ready() == QURT_EOK) {
        //Alloc DMA Context
        pdma_context = (t_dma_context *) malloc(sizeof(t_dma_context));
        if (pdma_context != NULL) {
            //initialize p_dma_context
            halide_hexagon_dmart_dma_init(user_context, (void *)pdma_context, nframes);
            *pdma_handle = pdma_context;
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
 * Attach Context checks if the frame width, height and type is aligned with DMA Transfer
 * Returns Error if the frame is not aligned.
 * AttachContext needs to be called for each frame */
int halide_hexagon_dmart_attach_context(void* user_context, p_dma_context pdma_context, uintptr_t  frame,int type,
                                         int d, int w, int h, int s, int last) {
    int nRet;
    bool is_ubwc_dst;
    t_eDmaFmt efmt_chroma;
    t_dma_pix_align_info pix_align_info;
    halide_assert(user_context, pdma_context != NULL);

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
    if (frame!=0) {
        nRet = halide_hexagon_dmart_set_host_frame(user_context, pdma_context, frame, type, d, w, h, s, last);
        if (nRet) {
            //Error in setting the Host frame
            error(user_context) << "hexagon_dmart_set_host_frame function failed \n";
            return HEX_ERROR;
        }
    }
    return HEX_SUCCESS;
}

/*!
 *  Detach Context signals the end of frame
 * This call is used when user has more frames to process
 * and does not want to free the DMA Engines
 * Return an Error if there is an error in DMA Transfer  */
int halide_hexagon_dmart_detach_context(void* user_context, p_dma_context pdma_context, uintptr_t frame) {
    int index,i;
    halide_assert(user_context, pdma_context != NULL);

    if (frame != 0) {
        // Serch for the frame Entry
        index = -1;
        for (i=0; i < pdma_context->nframes; i++) {
            if ((index==-1) && (pdma_context->pframe_table[i].frame_addr==frame)) {
                index = i;
            }        
        }
        if (index!=-1) {
            halide_hexagon_dmart_clr_host_frame (user_context, pdma_context, frame);
        } else {
            error(user_context) << "Error, the frame Deoesnt exist to detach \n";
            //Currently Returning -1, need to setup Error Codes
            return HEX_ERROR;
        }
    } else {
        //Error frame is null
        error(user_context) << "The frame provided to function dmaapp_detach_context is NULL \n";
        //Currently Returning -1, setup Return Codes
        return HEX_ERROR;
    } 
    return HEX_SUCCESS;
}

/**
 * Delete Context frees up the dma handle if not yet freed
 * and deallocates memory for the userContext */
int halide_hexagon_dmart_delete_context(void* user_context, p_dma_context pdma_context) {
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

/**
 * halide_hexagon_dmart_is_buffer_read
 * Find if data stream is input or output */
int halide_hexagon_dmart_is_buffer_read(void* user_context, p_dma_context pdma_context, uintptr_t frame, bool *is_read) {
    halide_assert(user_context, pdma_context != NULL);

    int index = -1;
    //Search the frame in the frame Table
    for (int i=0; i<pdma_context->nframes; i++) {
        if ((index==-1)&&(pdma_context->pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }

    if(index!=-1) {
        //frame Exist 
        *is_read = pdma_context->pframe_table[index].read;
     } else {
        //The frame Doesnt Exist 
        error(user_context) << "The frame Doesnt Exist \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_get_fold_size
 * Get Fold Size */
int halide_hexagon_dmart_get_fold_size(void* user_context, p_dma_context pdma_context, uintptr_t frame) {
    halide_assert(user_context, pdma_context != NULL);

    int index = -1;
    //Search the frame in the frame Table
    for (int i=0; i<pdma_context->nframes; i++) {
        if ((index==-1) && (pdma_context->pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index!=-1) {
        return (pdma_context->presource_frames[index].fold_buff_size);
    } else {
        //The frame Doesnt Exist 
        error(user_context) << "The frame doesnt exist \n";
        return 0;
    }
}

/**
 *  halide_hexagon_dmart_get_num_components
 * The Function will return the Number of Components (Planes ) in the frame
 * will retun 2 - for both Y and UV planes */
int halide_hexagon_dmart_get_num_components (void* user_context, p_dma_context pdma_context, uintptr_t frame) {
    int index;
    int i;
    halide_assert(user_context, pdma_context != NULL);

    // Serch if the frame Already Exists
    index = -1;
    for (i=0 ;i < pdma_context->nframes; i++) {
        if ((index==-1)&& (pdma_context->pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }
    int ncomponents = -1;
    if (index==-1) {
        //Error the frame index Doesnt Exist 
        error(user_context) << "The frame with the given VA doesnt exist \n";
    } else {
        // frame Exists get the number of components 
        int plane = pdma_context->presource_frames[index].plane;
        //Return 1 for both Chroma and Luma Components 
        //Return 2 in case of Other i.e having both Planes
        ncomponents = ((plane ==LUMA_COMPONENT)||(plane==CHROMA_COMPONENT))?1:2;
    }
    return ncomponents;
}

/**
 * halide_hexagon_dmart_allocate_dma
 * Check if DMA is already allocated */
int halide_hexagon_dmart_allocate_dma(void* user_context, p_dma_context pdma_context, uintptr_t frame, bool *dma_allocate) {
    halide_assert(user_context, pdma_context != NULL);

    int index = -1;
    //Search the frame in the frame Table
    for (int i=0;i<pdma_context->nframes;i++) {
        if ((index==-1) && (pdma_context->pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index!=-1) {
        //frame Exist 
        error(user_context) << "pdma_context->frame_cnt " << pdma_context->frame_cnt << "\n";
        int set_id = pdma_context->pframe_table[index].dma_set_id;
        int engine_id =  pdma_context->pframe_table[index].dma_engine_id;
        error(user_context) << "set_id " << set_id <<  "engine_id " << engine_id << "\n";
        if (pdma_context->pframe_table[index].read) {
            // Read Engine 
            if (pdma_context->pset_dma_engines[set_id].pdma_read_resource[engine_id].session.dma_handle == NULL) {
                *dma_allocate = true;
            }
        } else {
            // Write Engine
            if (pdma_context->pset_dma_engines[set_id].pdma_write_resource[engine_id].session.dma_handle == NULL){
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
int halide_hexagon_dmart_set_dma_handle(void* user_context, p_dma_context pdma_context, void* handle, uintptr_t frame) {
    halide_assert(user_context, pdma_context != NULL);
 
    int index = -1;
    //Search the frame in the frame Table
    for (int i=0;i<pdma_context->nframes;i++) {
        if ((index==-1) && (pdma_context->pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index!=-1) {
        //frame Exist
        int set_id = pdma_context->pframe_table[index].dma_set_id;
        int engine_id =  pdma_context->pframe_table[index].dma_engine_id;

        if (pdma_context->pframe_table[index].read) {
            pdma_context->pset_dma_engines[set_id].pdma_read_resource[engine_id].session.dma_handle = handle;
        } else {
            pdma_context->pset_dma_engines[set_id].pdma_write_resource[engine_id].session.dma_handle = handle;
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
void* halide_hexagon_dmart_get_dma_handle(void* user_context, p_dma_context pdma_context, uintptr_t frame) {
    halide_assert(user_context, pdma_context != NULL);

    int index = -1;
    //Search the frame in the frame Table
    for (int i=0;i<pdma_context->nframes;i++) {
        if ((index==-1) && (pdma_context->pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index != -1) {
        //frame Exist
        if(pdma_context->pframe_table[index].read) {
            int set_id = pdma_context->pframe_table[index].dma_set_id;
            int engine_id = pdma_context->pframe_table[index].dma_engine_id;
            return pdma_context->pset_dma_engines[set_id].pdma_read_resource[engine_id].session.dma_handle;
        }
        else {
            int set_id = pdma_context->pframe_table[index].dma_set_id;
            int engine_id = pdma_context->pframe_table[index].dma_engine_id;
            return pdma_context->pset_dma_engines[set_id].pdma_write_resource[engine_id].session.dma_handle;
        }
    } else {
        //The frame Doesnt Exist 
        error(user_context) << "The frame doesnt exist \n";
        return NULL;
    }
}



/**
 * halide_hexagon_dmart_set_fold_storage
 * Link the fold with the frame */
int halide_hexagon_dmart_set_fold_storage(void* user_context, p_dma_context pdma_context, uintptr_t addr, uintptr_t tcm_region, qurt_size_t size,
                                          uintptr_t desc_va, uintptr_t desc_region, qurt_size_t desc_size, int *fold_id) {
    halide_assert(user_context, pdma_context != NULL);

    pdma_context->pfold_storage[pdma_context->fold_cnt].work_buffer_index = pdma_context->fold_cnt;
    pdma_context->pfold_storage[pdma_context->fold_cnt].fold_virtual_addr = addr;
    pdma_context->pfold_storage[pdma_context->fold_cnt].in_use = 0;
    pdma_context->pfold_storage[pdma_context->fold_cnt].ping_phys_addr = dma_lookup_physical_address(addr);
    pdma_context->pfold_storage[pdma_context->fold_cnt].tcm_region = tcm_region;
    pdma_context->pfold_storage[pdma_context->fold_cnt].desc_virtual_addr    = desc_va;
    pdma_context->pfold_storage[pdma_context->fold_cnt].desc_region = desc_region;
    pdma_context->pfold_storage[pdma_context->fold_cnt].size_desc = desc_size;
    pdma_context->pfold_storage[pdma_context->fold_cnt].size_tcm = size;
    *fold_id = pdma_context->fold_cnt;
    pdma_context->fold_cnt++;
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_get_update_params
 * The params needs for updating descriptors */
int halide_hexagon_dmart_get_update_params(void* user_context, p_dma_context pdma_context, uintptr_t buf_addr, t_dma_move_params* param) {
    halide_assert(user_context, pdma_context != NULL);

    int store_id = -1;
    for (int i=0; i<pdma_context->fold_cnt; i++) {
        if ((store_id==-1) && (pdma_context->pfold_storage[i].fold_virtual_addr==buf_addr)) {
            store_id = i;
        }
    }
    if (store_id!=-1) {
        param->yoffset = pdma_context->pfold_storage[store_id].roi_yoffset;
        param->roi_height = pdma_context->pfold_storage[store_id].roi_height ;
        param->xoffset = pdma_context->pfold_storage[store_id].roi_xoffset;
        param->roi_width = pdma_context->pfold_storage[store_id].roi_width;
        // the tcm address is updated given the index of the buffer to use (ping/pong) which alternates on every dma transfer.
        param->ping_buffer = pdma_context->pfold_storage[store_id].ping_phys_addr;
        param->offset =  pdma_context->pfold_storage[store_id].offset;
        param->l2_chroma_offset = pdma_context->pfold_storage[store_id].l2_chroma_offset;
        return HEX_SUCCESS;
    } else {
        return HEX_ERROR;
    }
}

/**
 * halide_hexagon_dmart_get_tcm_desc_params
 * the descriptors need to be saved for
 * unlocking cache and free afterwards  */
int  halide_hexagon_dmart_get_tcm_desc_params(void* user_context, p_dma_context pdma_context, uintptr_t dev_buf, uintptr_t *tcm_region, qurt_size_t *size_tcm, \
                                             uintptr_t *desc_va, uintptr_t *desc_region, qurt_size_t *desc_size) {
    
    halide_assert(user_context, pdma_context != NULL);

    int store_id = -1;
    for (int i=0;i<pdma_context->fold_cnt;i++) {
        if ((store_id==-1) && (pdma_context->pfold_storage[i].fold_virtual_addr==dev_buf)) {
            store_id = i;
        }
    }

    if (store_id != -1) {
        *tcm_region  = pdma_context->pfold_storage[store_id].tcm_region;
        *desc_va     = pdma_context->pfold_storage[store_id].desc_virtual_addr;
        *desc_region = pdma_context->pfold_storage[store_id].desc_region;
        *desc_size   = pdma_context->pfold_storage[store_id].size_desc;
        *size_tcm    = pdma_context->pfold_storage[store_id].size_tcm;
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
int halide_hexagon_dmart_get_last_frame(void* user_context, p_dma_context pdma_context, uintptr_t frame, bool* last_frame) {
    halide_assert(user_context, pdma_context != NULL);

    int index = -1;
    // Search the frame in the frame Table
    for (int i=0; i<pdma_context->nframes; i++) {
        if ((index==-1) && (pdma_context->pframe_table[i].frame_addr == frame)) {
            index = i;
        }
    }
    if (index != -1) {
        // frame Exist 
        int set_id = pdma_context->pframe_table[index].dma_set_id;
        int engine_id =  pdma_context->pframe_table[index].dma_engine_id;
        if (pdma_context->pframe_table[index].read) {
            *last_frame = pdma_context->pset_dma_engines[set_id].pdma_read_resource[engine_id].pframe->end_frame;
        } else {
            *last_frame = pdma_context->pset_dma_engines[set_id].pdma_write_resource[engine_id].pframe->end_frame;
        }
    } else {
        // The fold Buffer doesnt exist 
        error(user_context) << " The frame doesnt exist \n";
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

/**
 * halide_hexagon_dmart_get_prepare_params
 * Prepare params for dma transfer */
int halide_hexagon_dmart_get_prepare_params(void* user_context, p_dma_context pdma_context, uintptr_t frame, t_dma_prepare_params* prepare_param) {

    halide_assert(user_context, pdma_context != NULL);

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
    t_eDmaFmt chroma_type;
    t_eDmaFmt luma_type;

    frame_idx = halide_hexagon_dmart_get_frame_index(user_context, pdma_context, frame);

    roi_width = pdma_context->presource_frames[frame_idx].fold_width;
    roi_height = pdma_context->presource_frames[frame_idx].fold_height;
    padding = pdma_context->presource_frames[frame_idx].padding;
    is_ubwc = pdma_context->presource_frames[frame_idx].is_ubwc;
    frame_width = pdma_context->presource_frames[frame_idx].frame_width;
    frame_height = pdma_context->presource_frames[frame_idx].frame_height;
    frame_stride = pdma_context->presource_frames[frame_idx].frame_stride;
    plane = pdma_context->presource_frames[frame_idx].plane;
    ncomponents = ((plane==LUMA_COMPONENT)|| (plane==CHROMA_COMPONENT))?1:2;
    nfolds = pdma_context->presource_frames[frame_idx].num_folds;
    chroma_type = pdma_context->presource_frames[frame_idx].chroma_type;
    luma_type  = pdma_context->presource_frames[frame_idx].luma_type;

    t_dma_pix_align_info pst_roi_size;
    pst_roi_size.u16W = roi_width;
    pst_roi_size.u16H = roi_height;
    luma_stride = dma_get_stride(luma_type, is_ubwc, pst_roi_size);
    chroma_stride = dma_get_stride(chroma_type, is_ubwc, pst_roi_size);

    //An Allocated DMA Already Exists Re-use it
    dma_handle = halide_hexagon_dmart_get_dma_handle(user_context, pdma_context, frame);


    //Populate Work Descriptors and Prepare DMA
    //#################################################################
    prepare_param->handle = dma_handle;
    prepare_param->host_address =  frame;
    prepare_param->frame_width = frame_width;
    prepare_param->frame_height = frame_height;
    prepare_param->frame_stride = frame_stride;
    prepare_param->roi_width = roi_width;
    prepare_param->roi_height = roi_height;
    prepare_param->luma_stride = luma_stride;
    prepare_param->chroma_stride = chroma_stride;
    prepare_param->luma_type = luma_type;
    prepare_param->chroma_type = chroma_type;
    prepare_param->ncomponents = ncomponents;
    prepare_param->padding = padding;
    prepare_param->is_ubwc = is_ubwc;
    prepare_param->num_folds = nfolds;

    return HEX_SUCCESS;
}

/**
 *halide_hexagon_dmart_get_frame
 *get the current frame address */
extern uintptr_t halide_hexagon_dmart_get_frame(void* user_context, p_dma_context pdma_context) {
    halide_assert(user_context, pdma_context != NULL);

    int i=0;
    return pdma_context->pframe_table[i].frame_addr;
}

/**
 * halide_hexagon_dmart_get_fold_addr
 * get the fold addr from frame */
extern uintptr_t halide_hexagon_dmart_get_fold_addr(void* user_context, p_dma_context pdma_context, uintptr_t frame) {
    halide_assert(user_context, pdma_context != NULL);
    uintptr_t fold_addr = NULL;

    int index = -1;
    // Search the frame in the frame Table
    for (int i=0; i<pdma_context->nframes; i++) {
        if ((index==-1) && (pdma_context->pframe_table[i].frame_addr == frame)) {
            index = i;
        }        
    }
    if (index != -1) {
        // frame Exist
        int fold_idx = pdma_context->pframe_table[index].work_buffer_id;
        fold_addr = (uintptr_t) pdma_context->pfold_storage[fold_idx].fold_virtual_addr;
    } else {
        // The fold Buffer doesnt exist
        error(user_context) << " The frame doesnt exist \n";
        return NULL;
    }

    return fold_addr;

}
/**
 *halide_hexagon_dmart_get_update
 *get update parameters from frame */ 
int halide_hexagon_dmart_get_update(void* user_context, p_dma_context pdma_context, uintptr_t frame, bool& update) {
    halide_assert(user_context, pdma_context != NULL);

    // Serch for the frame Entry
    int index = -1;
    for (int i=0; i < pdma_context->nframes; i++) {

        if ((index==-1)&& (pdma_context->pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }

    if (index != -1) {
        //Dis-associate frame From fold
        update = pdma_context->pframe_table[index].update;
    }
    else {
        return HEX_ERROR;
    }

    return HEX_SUCCESS;
}

/**
 *halide_hexagon_dmart_set_update
 * set update parameters */
int halide_hexagon_dmart_set_update(void* user_context, p_dma_context pdma_context, uintptr_t frame) {
    halide_assert(user_context, pdma_context != NULL);

    // Serch for the frame Entry
    int index = -1;
    for (int i=0; i < pdma_context->nframes; i++) {
        if ((index==-1)&& (pdma_context->pframe_table[i].frame_addr==frame)) {
            index = i;
        }
    }

    if (index != -1) {
        ///Dis-associate frame From fold
        pdma_context->pframe_table[index].update = true;
    }
    else {
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}


