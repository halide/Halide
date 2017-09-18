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
    int num_ping_pong_buffers;                  // Number of Ping Pong Buffers, come from Halide Pipeline
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
typedef struct {
    int nframes;                                       // Total Number of Frames, used to maintain frame Table 
    int frame_cnt;                                     // A Global count for indexing the Frames 
    int fold_cnt;                                      // A Global Count for indexing the Fold 
    int num_threads;                                   // Number of Software Threads = No of Tiles 
    t_frame_table *pframe_table;                       // Allocate this based on the Number of Frames user want to DMA
    t_resource_per_frame *presource_frames;            // Number of elements of this struct is nFrames and allocate accordingly
    t_work_buffer *pfold_storage;                      // Fold Storage Buffer 
    t_set_dma_engines pset_dma_engines[NUM_HW_THREADS];// Can Run only N sets in parallel, where N is the number of H/W Threads
} t_dma_context;

extern void halide_hexagon_set_dma_context(void* user_context, t_dma_context* context);

/**
 *  halide_hexagon_dmart_get_context
 * get DMA context from hexagon context
 * dma_context = DMA context */
extern void halide_hexagon_get_dma_context (void* user_context, t_dma_context** context);

#endif  /*_HALIDE_HEXAGON_DMA_CONTEXT_H_*/
