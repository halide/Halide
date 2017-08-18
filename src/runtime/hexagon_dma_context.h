#ifndef _HALIDE_HEXAGON_DMA_CONTEXT_H_
#define _HALIDE_HEXAGON_DMA_CONTEXT_H_

#include "hexagon_dma_device_shim.h"

using namespace Halide::Runtime::Internal::Qurt;

/* Currently hardcoding the number of DMA engines available*/
/* Should find a way to get this */
/* Currently making the count to 4 as we want 1 RD Engine and 1 Write Engine per H/W Thread*/
#define NUM_DMA_ENGINES 4
/* Currently hardcoding the number of H/W threads available */
/* Should find a way to get this */
#define NUM_HW_THREADS  2

#define OK 0
#define ERR -1


/*!
 * Memcpy control block (Per open session)
 */
typedef struct stDmaHalide_SessionCb {
    //! Read DMA wrapper handle
    void* dmaRdWrapper;
    //! Write DMA wrapper handle
    void* dmaWrWrapper;
} t_StDmaHalide_SessionCb;


/*DMA user context structure*/

/*This Below Structure is at Work Buffer
 Capture all the Details at Work Buffer */

typedef struct s_WorkBuffer{
        bool bInuse;                                /* To Indicate if this Work unit is Free or associated to a Frame*/ 
        int workBufferIndex;                        /* To Identify which Work Buffer we are using */
        int nRoiWalkWidth;                          /* Walk ROI width  */
        int nRoiWalkHeight;                         /* Walk ROI height */
		int nRoiXoffset;                            /* x-offset in the frame for ROI start*/ 
		int nRoiYoffset;                            /* Y-offset in the frame for ROI start*/ 
        int offset;                                 /* Offset from pVAFoldBuffer to get the actual Fold Address */
        int nPingPongBuffers;                       /* Number of Ping Pong Buffers, come from Halide Pipeline*/
 		unsigned int threadID;                      /* The  Software thread ID which is using this set of WorkBuffer*/
		int l2ChromaOffset;                         /* L2 Chroma Offset */
		int sizeDesc;                               /* DMA Descriptor Size*/
		int sizeTcm;                                /* L2 cache Size Allocated for each DMA Transfer(Ping or Pong Buffer Size)*/
		addr_t pVAFoldBuffer;                       /* Virtual Address of Locked L2 cache for Ping, pong Buffers*/
		addr_t addrL2PhysAddr_IntermBufPing;        /* physical address ping buffer*/
		qurt_mem_region_t tcm_region;               /* TCM Region used for allocating the L2 cache*/
		addr_t pDescVA;                             /* DMA Descriptor virtual address */   
		qurt_mem_region_t desc_region;              /* DMA Descritor Region used for Allocating descriptors */
}t_WorkBuffer;

/* This Below Structure is at Frame Granularity
 Capture all the Details at the Frame Level*/

typedef struct s_ResourcePerFrame{
        int frameIndex;                             /* To Identify which frame we are handling */
        int frameWidth;                             /* Frame Width*/
        int frameHeight;                            /* Frame Height */
        int frameStride;                            /* Frame Stride */
        int type;                                   /* Frame Format*/
		t_eDmaFmt chromaType;                       /* Frame Format*/
        t_eDmaFmt lumaType;                         /* Frame Type*/
		int plane;                                  /* Frame Plane Y or UV Plane*/
        int foldWidth;                              /* Fold Buffer Width */
		int foldHeight;                             /* Fold Buffer Height */
		int foldStride;                             /* Fold Buffer Stride */
		int nFolds;                                 /* Number of Folds(Circular Buffers)*/ 
		int foldBuffSize;                           /* Fold Buffer Size */
        bool end_frame;                             /* Default False*/
		bool isUBWC;                                /* Flag to to Indicate of the Frame is UBWC or not*/
        bool padding;                               /* Flag To indicate if padding to 16-bit in L2 $ is needed or not */
        addr_t pFrameAddress;                       /* Virtual Adress of the Frame(DMA Read/Write)*/
		t_WorkBuffer *pWorkBuffers;                 /* We dont allocate this but just link the Free WorkBuffer for Frame*/
}t_ResourcePerFrame;

/* This Below Structure is at DMA Resource Granularity
   Capture all the Detials at the DMA resource Level*/
typedef struct s_DmaResource{
	    bool bInuse;                                /* To identify if the DMA Resource is in use or free */
        int resourceID;                             /* ID for the DMA Resource to Distinguinsh from other resources*/
        t_StDmaHalide_SessionCb pSession;           /* PSession Holds both the DMA Read/Write handles or Either of them*/
        bool bSinkEn;                               /* flag to indicate DMA write is also allocated*/
        t_ResourcePerFrame *pFrame;                 /* We donot allocate memory for this,just associate the current frame this Engine handles */
}t_DmaResource;


/* Capture Detials of available DMA  Resources */

typedef struct s_setDMAEngines {
        bool bInuse;                                 /* To Identify if the H/W thread is in use or Free*/        
        int dmaSetID;                                /* To Distinguinsh various sets of DMA engines*/
        int nDmaReadEngines;                         /* Number of DMA Enginers in use for Read in this SET*/
		int nDmaWriteEngines;                        /* Number of DMA Enginers in use for Write in this SET*/
        t_DmaResource *pDmaReadResource;             /* Allocate this based on the number of Read DMA Engines i.e nDmaReadEngines. Currently 1 */		                                             
		t_DmaResource *pDmaWriteResource;            /* Allocate this based on the number of Write DMA Engines i.e nDmaWriteEngines. Curently 1 */		
}t_setDMAEngines;

/* Frame Table*/
/* For Fast Seraching of the Frame in the DMA structures*/
typedef struct s_FrameTable{
    addr_t pVirtAddr;                                /* Virtual Address of the Frame*/
	int nDMASetID;                                   /* ID for the DMA Set used for this Frame */
	int nDMAEngineID;                                /* ID of DMA engine used for this Frame*/  
	int frameIndex;                                  /* The Frame ID used to disntinguish various Frames*/
	bool read;                                       /* To distinguish if the Frame is For Read or write*/
	int nWorkBufferID;	                             /* The Work Buffer attached to this Frame */
}t_FrameTable;


/* Hexagon DMA transfer user_context*/
typedef struct {
        int nFrames;                                 /* Total Number of Frames, used to maintain frame Table */
		int frameCnt;                                /* A Global count for indexing the Frames */
		int foldCnt;                                 /* A Global Count for indexing the Fold */
		int nThreads;                                /* Number of Software Threads = No of Tiles */
		t_FrameTable *pFrameTable;                   /* Allocate this based on the Number of Frames user want to DMA*/
		t_ResourcePerFrame *pResourceFrames;         /* Number of elements of this struct is nFrames and allocate accordingly*/
		t_WorkBuffer *pFoldStorage;                  /* Fold Storage Buffer */
		t_setDMAEngines psetDMAEngines[NUM_HW_THREADS];/* Can Run only N sets in parallel, where N is the number of H/W Threads*/
}t_DmaContext;
/* Halide USer Context */
typedef struct{
        t_DmaContext *pDmaContext;                    /* DMA Context*/
}t_HexagonContext;


#endif  /*_HALIDE_HEXAGON_DMA_CONTEXT_H_*/
