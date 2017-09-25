/**
 *  This file contain the Hexagon DMA transfer context structure
 * This Context will be passed a parameter to the
 * Halide DMA device interface so that necessary information is shared
 * across various APIs of the Halide DMA device interface
 * This Also maintains details of all the active DMA engines and the active
 * allocated Fold storage */

#ifndef _HALIDE_HEXAGON_DMA_CONTEXT_H_
#define _HALIDE_HEXAGON_DMA_CONTEXT_H_
#include "hexagon_dma_device_shim.h"

/**
 *  Currently hardcoding the number of DMA engines available
 * Should find a way to get this
 * Currently making the count to 4 as we want 1 RD Engine and 1 Write Engine per H/W Thread */
#define NUM_DMA_ENGINES 4

/**
 * Currently hardcoding the number of H/W threads available
 * Should find a way to get this */
#define NUM_HW_THREADS  2

/**
 * Error Codes */
typedef enum {
    HEX_SUCCESS = 0,
    HEX_ERROR = -1
} t_dma_error_codes;

/**
 * Memcpy control block (Per open session) */
typedef struct {
    void* dma_handle;
} t_halide_dma_session;

/**
 * DMA user context structure
 * This Below Structure capture
 * all the Details at Work Buffer */
typedef struct {
    bool in_use;                                // To Indicate if this Work unit is Free or associated to a Frame
    int work_buffer_index;                      // To Identify which Work Buffer we are using
    int roi_width;                              // Walk ROI width  
    int roi_height;                             // Walk ROI height 
    int roi_xoffset;                            // x-offset in the frame for ROI start
    int roi_yoffset;                            // Y-offset in the frame for ROI start
    int offset;                                 // Offset from pVAFoldBuffer to get the actual Fold Address 
    unsigned int thread_id;                     // The  Software thread ID which is using this set of WorkBuffer
    int l2_chroma_offset;                       // L2 Chroma Offset 
    int size_desc;                              // DMA Descriptor Size
    int size_tcm;                               // L2 cache Size Allocated for each DMA Transfer(Ping or Pong Buffer Size
    uintptr_t fold_virtual_addr;                // Virtual Address of Locked L2 cache for Ping, pong Buffers
    uintptr_t ping_phys_addr;                   // physical address ping buffer
    uintptr_t tcm_region;                       // TCM Region used for allocating the L2 cache
    uintptr_t desc_virtual_addr;                // DMA Descriptor virtual address 
    uintptr_t desc_region;                      // DMA Descritor Region used for Allocating descriptors 
} t_work_buffer;

/**
 *  This Below Structure is at Frame Granularity
 * Capture all the Details at the Frame Level */
typedef struct {
    int frame_index;                            // To Identify which frame we are handling 
    int frame_width;                            // Frame Width
    int frame_height;                           // Frame Height
    int frame_stride;                           // Frame Stride
    int type;                                   // Frame Format
    t_eDmaFmt chroma_type;                      // Frame Format
    t_eDmaFmt luma_type;                        // Frame Type
    int plane;                                  // Frame Plane Y or UV Plane
    int fold_width;                             // Fold Buffer Width 
    int fold_height;                            // Fold Buffer Height 
    int fold_stride;                            // Fold Buffer Stride 
    int num_folds;                              // Number of Folds(Circular Buffers)
    int fold_buff_size;                         // Fold Buffer Size 
    bool end_frame;                             // Default False
    bool is_ubwc;                               // Flag to to Indicate of the Frame is UBWC or not
    bool padding;                               // Flag To indicate if padding to 16-bit in L2 $ is needed or not 
    bool update;                                // Flag to undicate if dma transfer has started 
    uintptr_t frame_virtual_addr;               // Virtual Adress of the Frame(DMA Read/Write)
    t_work_buffer *pwork_buffer;                // We dont allocate this but just link the Free WorkBuffer for Frame
} t_resource_per_frame;

/**
 *  This Below Structure is at DMA Resource Granularity
 *  Capture all the Detials at the DMA resource Level */
typedef struct {
    bool in_use;                                // To identify if the DMA Resource is in use or free 
    int resource_id;                            // ID for the DMA Resource to Distinguinsh from other resources
    t_halide_dma_session session;               // PSession Holds both the DMA Read/Write handles or Either of them
    bool dma_write;                             // flag to indicate DMA write is also allocated
    t_resource_per_frame *pframe;               // We donot allocate memory for this,just associate the current frame this Engine handles 
} t_dma_resource;

/**
 *  Capture Detials of available DMA  Resources */
typedef struct {
    bool in_use;                                // To Identify if the H/W thread is in use or Free
    int dma_set_id;                             // To Distinguinsh various sets of DMA engines
    int ndma_read_engines;                      // Number of DMA Enginers in use for Read in this SET
    int ndma_write_engines;                     // Number of DMA Enginers in use for Write in this SET
    t_dma_resource *pdma_read_resource;         // Allocate this based on the number of Read DMA Engines i.e nDmaReadEngines. Currently 1 
    t_dma_resource *pdma_write_resource;        // Allocate this based on the number of Write DMA Engines i.e nDmaWriteEngines. Curently 1 
} t_set_dma_engines;

/** Frame Table
 * For Fast Seraching of the Frame in the DMA structures */
typedef struct {
    uintptr_t frame_addr;                       // Virtual Address of the Frame
    int dma_set_id;                             // ID for the DMA Set used for this Frame 
    int dma_engine_id;                          // ID of DMA engine used for this Frame
    int frame_index;                            // The Frame ID used to disntinguish various Frames
    bool read;                                  // To distinguish if the Frame is For Read or write
    int work_buffer_id;                         // The Work Buffer attached to this Frame 
} t_frame_table;

/**
 * Hexagon DMA transfer user_context */
class DmaContext {
public:
    int nframes;                                       // Total Number of Frames, used to maintain frame Table 
    int current_frame_index;                           // Current Frame Index
    int frame_cnt;                                     // A Global count for indexing the Frames 
    int fold_cnt;                                      // A Global Count for indexing the Fold 
    int num_threads;                                   // Number of Software Threads = No of Tiles
    t_frame_table *pframe_table;                       // Allocate this based on the Number of Frames user want to DMA
    t_resource_per_frame *presource_frames;            // Number of elements of this struct is nFrames and allocate accordingly
    t_work_buffer *pfold_storage;                      // Fold Storage Buffer 
    t_set_dma_engines pset_dma_engines[NUM_HW_THREADS];// Can Run only N sets in parallel, where N is the number of H/W Threads


    /**
     * is_buffer_read
     * in: addr_t frame
     * out: bool read_flag */
    int is_buffer_read(void* user_context, uintptr_t frame, bool* is_read);
    
    /**
     * set_host_frame
     * desc: set the host frame input */
    int set_host_frame(void* user_context, uintptr_t  frame, int type, int d, int w, int h, int s, int last);

    /**
     * get frame index
     * desc: current frame index */
    int get_frame_index(void* user_context);

    /**
     * get frame
     * desc: get frame address of current frame index */  
    uintptr_t get_frame(void* user_context, int frame_index);

    /**
     * set_dma_handle
     *in: handle DMA Handle allocated for the frame 
     *out: Error/Success */
    int set_dma_handle(void* user_context, void *handle, uintptr_t frame);

    /**
     * set_chroma_stride
     * in: stride set the chroma stride for the frame
     * out: Error/Success */
     int set_chroma_stride(void* user_context, uintptr_t frame, int stride);

    /**
     * set_luma_stride
     * in: stride Set the luma stride for the frame
     * out: Error/Success */
     int set_luma_stride(void* user_context, uintptr_t frame, int stride);

    /**
     * set_fold_storage
     * in: addr_t Cache region
     * in: qurt_size_t Size of the fold
     * in: addr_t Descriptor virtual address of the fold
     * in: addr_t: Descriptor memory region
     * in: qurt_size_t Descriptor size
     * out: fold_id fold id
     * out: Error/Success */
     int set_fold_storage(void* user_context, uintptr_t addr, uintptr_t tcm_region, \
                                                     qurt_size_t size, uintptr_t desc_va, uintptr_t desc_region, \
                                                     qurt_size_t desc_size, int *fold_id);

    /**
     * get_update_params
     * in: addr_t fold address
     * out: dma_tMoveParams Parameters to update for DMA Transfer
     * out: Error/Success  */
     int get_update_params(void* user_context, uintptr_t dev_buf, t_dma_move_params* update_param);

    /**
     * get_tcmDesc_params
     * in: addr_t dev_buf
     * out: addr_t cache address
     * out: qurt_size_t cache size
     * out: addr_t descriptor virtual address
     * out: addr_t
     * out: qurt_size_t descriptor size
     * out: addr_t descriptor
     * out: Error/Success */
     int get_tcm_desc_params(void* user_context, uintptr_t dev_buf, uintptr_t *tcm_region,
                                                        qurt_size_t *tcm_size, uintptr_t *desc_va, uintptr_t *desc_region, \
                                                        qurt_size_t *desc_size);

    /**
     * get_last_frame
     * in: addr_t frame
     * out: bool lastFrame
     */
     int get_last_frame(void* user_context, uintptr_t frame, bool *last_frame);

    /**
     * get_fold_size
     * in: addr_t frame
     * out: unsigned int size */
     int get_fold_size(void* user_context, uintptr_t frame);

    /**
     * allocate_dma
     * in: addr_t frame
     * out: bool dma_allocate
     * out: Error/Success */
     int allocate_dma(void* user_context, uintptr_t frame, bool* dma_allocate);

    /**
     * get_read_handle
     * in: addr_t frame
     * out: void* handle */
     void* get_read_handle(void* user_context, uintptr_t frame);

    /**
     * get_write_handle
     * in: addr_t frame
     * out: void* handle */
     void* get_write_handle(void* user_context, uintptr_t frame);

    /**
     * get_free_fold
     * out: bool free fold
     * out: fold_id
     * out: int */
     int get_free_fold (void* user_context, bool *free_fold, int* store_id);

    /**
     * get num components 
     * Function will return the Number of Components (Planes ) in the Frame
     * will retun 1 - for Y plane
     * will retun 1 - for UV plane
     * will retun 2 - for both Y and UV planes */
     int get_num_components (void* user_context, uintptr_t frame);

    /**
     *  set_storage_linkage
     * associate host frame to device storage - one call per frame
     * frame = VA of frame buffer
     * fold = VA of device storage
     * store_id = Fold storage id  */
    int set_storage_linkage(void* user_context, uintptr_t frame, uintptr_t fold, int store_id);

    /**
     * set_max_fold_storage
     * desc: set roi information */ 
    int set_max_fold_storage (void* user_context, uintptr_t  frame, int w, int h, int s, int n);

    /**
     * set_padding
     * set the padding of frame */
    int set_padding(void* user_context, uintptr_t frame, int flag);

    /**
     * set component
     * set component chroma, luma, both */
    int set_component (void* user_context, uintptr_t frame, int plane);

    /**
     * set host roi
     * desc: roi dimensions and number
     */ 
    int set_host_roi (void* user_context, uintptr_t buf_addr, int x, int y, int w, int h, int rsc_id);

    /**
     * set device storage offset
     * ping pong address offset
     */
    int set_device_storage_offset (void* user_context, uintptr_t buf_addr, int offset, int rsc_id);

   /**
    * clr_host_frame
    * frame to be cleared 
    */
    int clr_host_frame (void* user_context, uintptr_t frame);

};



#endif  /*_HALIDE_HEXAGON_DMA_CONTEXT_H_*/
