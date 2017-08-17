#ifndef _DMA_HALIDE_RT_C_
#define _DMA_HALIDE_RT_C_

/*******************************************************************************
 * Include files
 *******************************************************************************/


#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "printer.h"
#include "mini_qurt.h"
#include "mini_dma.h"
#include "dma_device_shim.h"
#include "hexagon_dma_context.h"
#include "HalideRuntimeHexagonDma.h"
#include "hexagon_dma_rt.h"

using namespace Halide::Runtime::Internal::Qurt;


/* Table to Get DMA Chroma format for the user type selected */
/*index will be t_eUserFmt*/
t_eDmaFmt type2DmaChroma[6]={eDmaFmt_NV12_UV,eDmaFmt_NV12_UV,eDmaFmt_P010_UV,eDmaFmt_TP10_UV,eDmaFmt_NV124R_UV,eDmaFmt_NV124R_UV};
/* Table to Get DMA Luma format for the user type selected */
/*index will be t_eUserFmt*/
t_eDmaFmt type2DmaLuma[6]={eDmaFmt_NV12_Y,eDmaFmt_NV12_Y,eDmaFmt_P010_Y,eDmaFmt_TP10_Y,eDmaFmt_NV124R_Y,eDmaFmt_NV124R_Y};

/* Table for accounting the dimension for various Formats*/
float type_size[6]={1,1,1,0.6667,1,1};


/* _DmaRT_SetContext
*
* set DMA context from hexagon context
* *dma_context = DMA context*/
int halide_hexagon_dmart_set_context (void* user_context, void* dma_context)
{
	halide_assert(NULL, user_context!=NULL);
	/* Cast the user_context to Hexagon_user_context */
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
    
    if( NULL == hexagon_user_context->pDmaContext ){
        hexagon_user_context->pDmaContext = (t_DmaContext *)dma_context; 
	}
    else
	{
		/* Error DMA Context already exists */
		error(NULL) << "DMA Context Already Exist \n";
		/* Retuning just -1 in case of Error*/
		/* Setup Error Codes and Return the respective Error*/
		return ERR ;
		
	}

	return OK;
}


/* _DmaRT_GetContext
*
* get DMA context from hexagon context
* **dma_context = DMA context */

int halide_hexagon_dmart_get_context (void* user_context, void** dma_context)
{
	halide_assert(NULL, user_context != NULL);
	/* Cast the user_context to Hexagon_user_context */
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	if( NULL != hexagon_user_context->pDmaContext ){
		
        *dma_context = (void *) hexagon_user_context->pDmaContext;  
	}
    else
	{
		/* Error DMA Context Doesnt exists */
    	error(NULL) << "DMA Context Doesnt Exist \n";
		/* Retuning just -1 in case of Error*/
		/* Setup Error Codes and Return the respective Error*/
		return ERR ;
		
	}

	return OK;
}

/* Get Frame Index of the Frame in the frame context*/
int halide_hexagon_dmart_get_frame_index(void *user_context, addr_t frame, int *FrameIdx)
{
	int index,i;

	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL,pDmaContext != NULL);

	/* Serch if the frame Already Exists*/
	index = -1;
	for( i=0 ;i< pDmaContext->nFrames; i++)
	{
		if ((-1==index)&& pDmaContext->pFrameTable[i].pVirtAddr==frame)
		{
			index = i;
		}
	}
	if(-1!=index){
		/*Frame Exist Return the Idx*/
		*FrameIdx = index;

	}else{
		/*Frame Doesnt Exist Return Error */
		return ERR;
	}
	/*Return Success */

	return OK;
}


/* _DmaRT_SetHostframe
*
* set frame type
* *frame = VA of frame buffer
* type = NV12/NV124R/P010/UBWC_NV12/TP10/UBWC_NV124R
* frameID = frameID from the Video to distinguish various frames
* d = frame direction 0:read, 1:write
* w = frame width in pixels
* h = frame height in pixels
* s = frame stride in pixels
* last = last frame 0:no 1:yes *optional*
* inform dma it is last frame of the session, and can start freeing resource when requested
* at appropriate time (on last SW thread of that resource set) */
int halide_hexagon_dmart_set_host_frame (void* user_context, addr_t  frame, int type, int d, int w, int h, int s, int last)
{
	int freeDMA;
	int freeSet;
	int i;
	int nEngines;
	t_DmaResource *pDMAResource;
	bool flag;
	t_ResourcePerFrame *pframeContext;
	
	
	halide_assert(NULL, user_context != NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	/* Check if there are any Free H/W Threads available*/
	/* using a value to ease search*/
	freeSet =-1;
	for ( i =0 ; i < NUM_HW_THREADS;i++ ){

		if ((!pDmaContext->psetDMAEngines[i].bInuse) && (freeSet ==-1)){
			/* A Hardware Thread with ID = i is Free*/
			freeSet = i;
		}
	}
	if (-1==freeSet){
		/* None of the Hardware Threads are free*/		
		error(NULL) << "None of the Hardware threads are Free \n";
		/* Return a suitable Error ID, currently returning -1 */
		return -1;
	}else{
		/* If H/W threads are free we can use them for DMA transfer */
		/*Using the DMASetID same as the H/W Thread index`*/
		pDmaContext->psetDMAEngines[freeSet].dmaSetID = freeSet;
		addr_t pFrame;
		/*Check if the frame Entry Exist by searching in the frame Table*/
		pFrame = 0;
		for(i=0;i<pDmaContext->nFrames;i++)
		{
			if(frame==pDmaContext->pFrameTable[i].pVirtAddr){
				/* frame already Exist*/
				pFrame = frame;
			}
		}
		
		
		if(0==pFrame){
			/* The frame Doesnt Exist and we need to add it*/
			
			if (0==d){
				/* Current frame is to be read by DMA*/
				pDMAResource = pDmaContext->psetDMAEngines[freeSet].pDmaReadResource;
				nEngines = 	pDmaContext->psetDMAEngines[freeSet].nDmaReadEngines;
				/*Set the Flag that the Engine is used for Read*/
				flag = false;
				
			}else{
				/* Current frame is to be Written by DMA*/
				pDMAResource = pDmaContext->psetDMAEngines[freeSet].pDmaWriteResource;
				nEngines = 	pDmaContext->psetDMAEngines[freeSet].nDmaWriteEngines;
				/*Set the Flag that the Engine is used for Read*/
				flag = true;
			}
			
			
			/* using a value to ease search*/
			freeDMA =-1;
			/* Search if any of the Engine is Free*/
			for( i =0 ;i< nEngines; i++){
				if (!pDMAResource[i].bInuse && (-1 == freeDMA)){
					freeDMA = i;
				}
			}
			if (-1!=freeDMA){
				pframeContext = &(pDmaContext->pResourceFrames[pDmaContext->frameCnt]);
				/* Populate the frame Details in the frame Structure*/
				pframeContext->pFrameAddress = frame;
				pframeContext->frameWidth = w;
				pframeContext->frameHeight = h;
				pframeContext->frameStride = s;
				pframeContext->end_frame = last;
				pframeContext->type = type;
				pframeContext->chromaType = type2DmaChroma[type];
				pframeContext->lumaType = type2DmaLuma[type];
				pframeContext->frameIndex = pDmaContext->frameCnt;
				/*Attach this frame to the Free DMA Engine*/
				pDMAResource[freeDMA].pFrame = pframeContext;
				/*Set the Read Write Flag*/
				pDMAResource[freeDMA].bSinkEn = flag;
			
				/*Set the DMA is in use Flag*/
				pDMAResource[freeDMA].bInuse = true;
				/* Set that the H/W Thread is in USe*/
				pDmaContext->psetDMAEngines[freeSet].bInuse = true;

				/* Insert frame Entry in frame Table for easy Search*/
				pDmaContext->pFrameTable[pDmaContext->frameCnt].pVirtAddr = frame;
				pDmaContext->pFrameTable[pDmaContext->frameCnt].nDMASetID = freeSet ;
				pDmaContext->pFrameTable[pDmaContext->frameCnt].nDMAEngineID = freeDMA;
				pDmaContext->pFrameTable[pDmaContext->frameCnt].frameIndex = pDmaContext->frameCnt;
				pDmaContext->pFrameTable[pDmaContext->frameCnt].read = (d==0)? 1:0;
				/*Increment the frameCnt (index) as a new frame is added*/
				pDmaContext->frameCnt++;
				
			
			}else{
				/*Error No Free DMA Engines*/
				error(NULL) << "Error No free DMA Engines for Read operation \n";
				/* Return a suitable Error Code currently returning -1*/
				return ERR;
			}	
		
		}
		else{
			/* frame Already Exists an Error State*/
			error(NULL) << "The frame with the given VA is already registered for DMA Transfer \n";
			/* Set up Error Code and return respective value, Currently REturning -1*/
			return ERR;
		}	
				
	}

	return OK;
	 
}

/* _DmaRT_SetPadFlag
*
* set for dma padding in L2$ (8bit in DDR, 16bit in L2$) - padding '0' to LSb
* *frame = VA of frame buffer
* flag = 0:no padding, 1:padding */
int halide_hexagon_dmart_set_padding (void* user_context, addr_t  frame, int flag)
{
	int index,i,frameIdx;
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	/* Serch if the frame Already Exists*/
	index = -1;
	for( i=0 ;i< pDmaContext->nFrames; i++)
	{
		if ((-1==index)&& pDmaContext->pFrameTable[i].pVirtAddr==frame)
		{
			index = i;
		}		
	}
	if (-1==index)
	{
		/*Error the frame index Doesnt Exist */
		error(NULL) << "The frame with the given VA doesnt exist \n";
		/*Currently Returning -1, we have to setup Error Codes*/
		return ERR;
		
	}else{
		    /* frame Exists populate the padding Flag to the frame Context */
			frameIdx = pDmaContext->pFrameTable[index].frameIndex;
			pDmaContext->pResourceFrames[frameIdx].padding = (flag)? 1:0;;
	}

	return OK;
}


/* _DmaRT_SetComponent
*
* set which component to dma
* *frame = VA of frame buffer
* plane = Y/UV */
int halide_hexagon_dmart_set_component (void* user_context, addr_t  frame, int plane)
{
	int index,i,frameIdx;
	
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	/* Serch if the frame Already Exists*/
	index = -1;
	for( i=0 ;i< pDmaContext->nFrames; i++)
	{
		if ((-1==index)&& pDmaContext->pFrameTable[i].pVirtAddr==frame)
		{
			index = i;
		}		
	}
	if (-1==index)
	{
		/*Error the frame index Doesnt Exist */
		error(NULL) << "The frame with the given VA doesnt exist \n";
		/*Currently Returning -1, we have to setup Error Codes*/
		
		return ERR;
		
	}else{
		/* frame Exists populate the padding Flag to the frame Context */
			frameIdx = pDmaContext->pFrameTable[index].frameIndex;
			pDmaContext->pResourceFrames[frameIdx].plane = plane;
	}

	return OK;
	    
}


/* _DmaRT_SetParallel **optional depending on thread scope**
*
* is parallel processing (parallization of inner most loop only; must avoid nested parallelism)
* threads = number of SW threads (one thread per Halide tile) */
int halide_hexagon_dmart_set_parallel (void* user_context, int threads)
{
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	pDmaContext->nThreads = threads;

	return OK;
}


/* _DmaRT_SetStorageTile
*
* specify the largest folding storage size to dma tile and folding instances; to be used when malloc of device memory (L2$)
* *frame = VA of frame buffer
* w = fold width in pixels
* h = fold height in pixels
* s = fold stride in pixels
* n = number of folds (circular buffers) 
* example ping/pong fold_storage for NV12; for both Y and UV, Y only, UV only
        +-----------+   +-----------+   +-----------+
        | Y.0       |   | Y.0       |   | UV.0      |
        |           |   |           |   +-----------+
        +-----------+   +-----------+   | UV.1      |
        | UV.0      |   | Y.1       |   +-----------+
        +-----------+   |           |
        |           |   +-----------+
        | Y.1       |
        +-----------+
        | UV.1      |
        +-----------+
    */
int halide_hexagon_dmart_set_max_fold_storage (void* user_context, addr_t  frame, int w, int h, int s, int n)
{
	int frameIdx;
	int i,index;
	int padd_Factor;
	float type_factor;
    
    halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
		
	/* Serch if the frame Already Exists*/
	index = -1;
	for(i=0; i< pDmaContext->nFrames; i++)
	{
		if ((-1==index)&& pDmaContext->pFrameTable[i].pVirtAddr==frame)
		{
			index = i;
		}		
	}
	if (-1==index)
	{
		/*Error the frame index Doesnt Exist */
		error(NULL) << "The frame with the given VA doesnt exist \n";
		/*Currently Returning -1, we have to setup Error Codes*/
		
		return ERR;
		
	}else{
		/* frame Exists populate the padding Flag to the frame Context */
			
			
			frameIdx = pDmaContext->pFrameTable[index].frameIndex;
			
			pDmaContext->pResourceFrames[frameIdx].foldWidth = w;
		    pDmaContext->pResourceFrames[frameIdx].foldHeight = h;
		    pDmaContext->pResourceFrames[frameIdx].foldStride =s;
		    pDmaContext->pResourceFrames[frameIdx].nFolds = n;
			
			padd_Factor = pDmaContext->pResourceFrames[frameIdx].padding ? 2:1;
			type_factor = type_size[pDmaContext->pResourceFrames[frameIdx].type];
			/*Size of the fold Buffer */
		    pDmaContext->pResourceFrames[frameIdx].foldBuffSize = h*s*n*padd_Factor*type_factor;
			
	}

	return OK;
}

int halide_hexagon_dmart_get_free_fold (void* user_context, bool *free_fold, int* store_id){

	int i,fold_Idx;
	 halide_assert(NULL,user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	 halide_assert(NULL,pDmaContext != NULL);

	/* Search if the fold Already Exists and not in use*/
	fold_Idx =-1;
	for(i=0;i < pDmaContext->foldCnt;i++)
	{
		if((-1==fold_Idx)&& (pDmaContext->pFoldStorage[i].bInuse==false) && (pDmaContext->pFoldStorage[i].pVAFoldBuffer) ){
			/*Fold is Free and Already Allocated */
			fold_Idx = i;
		}
	}
	if(-1!=fold_Idx){
		/*A Free Fold Exists and Return the Index*/
		*free_fold = true;
		*store_id = fold_Idx;
		error(NULL) << "dmart_get_free_fold, free fold Exist \n";
	}
	else{
		/*An Already Allocated Free Fold Doesnt Exist*/
		*free_fold = false;
		*store_id = fold_Idx;
		error(NULL) << "dmart_get_free_fold, no free fold Exist \n";
	}

	return 0;
}



/* _DmaRT_SetStorageLinkage
*
* associate host frame to device storage - one call per frame
* *frame = VA of frame buffer
* *fold = VA of device storage
* *store_id = output of storage id*/
int halide_hexagon_dmart_set_storage_linkage (void* user_context, addr_t  frame, addr_t  fold, int store_id)
{
        int i,index;
        halide_assert(NULL,user_context!=NULL);
		t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
		t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
		halide_assert(NULL,pDmaContext != NULL);

		halide_assert(NULL,frame!=0);
		halide_assert(NULL,fold!=0);

		/* Serch if the frame Already Exists*/
		index = -1;
		for(i=0 ;i< pDmaContext->nFrames; i++)
		{
			if ((-1==index)&& pDmaContext->pFrameTable[i].pVirtAddr==frame)
			{
				index = i;
			}
		}

		/*See if the store_id is a valid one or not*/
		if (store_id > -1){
			/*Set that the the Identified fold is in use*/
			pDmaContext->pFoldStorage[store_id].bInuse = true;
			/* QURT API to get thread ID */
			pDmaContext->pFoldStorage[store_id].threadID =  dma_getThreadID();
			/*Associate the fold to frame in frame table*/
			pDmaContext->pFrameTable[index].nWorkBufferID = store_id;
			/*Associate the fold to the frame in frame Context*/
			pDmaContext->pResourceFrames[index].pWorkBuffers = &pDmaContext->pFoldStorage[store_id];

		}else{
			error(NULL) << "Error from dmart_set_storage_linkage, Invalid fold Index \n";
			return ERR;

		}

		return OK;
	
}


/* _DmaRT_SetLockResource and _DmaRT_SetUnlockResource **optional depending on thread scope**
*
* lock dma resource set to thread, max number of resource set is based on available HW threads
* lock = 0:unlock, 1:lock
* *rsc_id = lock outputs id value, unlock inputs id value */
int halide_hexagon_dmart_set_resource (void* user_context, int lock, int* rsc_id)
{
	/*Optional Function not populating */

	return OK;

}


/* _DmaRT_SetDeviceFold
*
* set the offset into folding device storage to dma - one call for each frame, per transaction
* store_id = storage id, pin point the correct folding storage
* offset = offset from start of L2$ (local folding storage) to dma - to id the fold (circular buffer)
* rcs_id = locked resource ID **optional** */
int halide_hexagon_dmart_set_device_storage_offset (void* user_context, addr_t buf_addr, int offset, int rsc_id)
{
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	int index = -1;
	
	for (int i=0;i<pDmaContext->foldCnt;i++){
		if((-1==index) && (pDmaContext->pFoldStorage[i].pVAFoldBuffer==buf_addr)){
			index = i;
		}		
	}
	
	if(index !=-1){
		/* The fold Storage Exist */
		pDmaContext->pFoldStorage[index].offset = offset;
		
	}else{
		error(NULL) << "The Device Storage Doesnt Exist \n";
		return ERR;
	}
	

	return OK;
}


/* _DmaRT_SetHostROI
*
* set host ROI to dma
* store_id = storage id, pin point the correct folding storage
* x = ROI start horizontal position in pixels
* y = ROI start vertical position in pixels
* w = ROI width in pixels
* h = ROI height in pixels
* rsc_id = locked resource ID **optional** */
int halide_hexagon_dmart_set_host_roi (void* user_context, addr_t buf_addr, int x, int y, int w, int h, int rsc_id)
{
	int type,isUbwcDst;
	t_eDmaFmt eFmtChroma;
	int index;
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	int i=0;
	halide_assert(NULL, pDmaContext != NULL);
	dma_tPixAlignInfo pixAlignInfo;
	
	int store_id = -1;
	
	for (int i=0;i<pDmaContext->foldCnt;i++){
		if((-1==store_id) && (pDmaContext->pFoldStorage[i].pVAFoldBuffer==buf_addr)){
			store_id = i;			
		}		
	}
	
	if(store_id !=-1){
		/* The fold Storage Exist */
		/* Need to Get frame type for checking Alignment requirements*/
		/* Search if the frame Already Exists*/
		index = -1;
		for(i=0 ;i< pDmaContext->nFrames; i++)
		{
			if ((-1==index)&& pDmaContext->pFrameTable[i].nWorkBufferID==store_id)
			{
				index = i;
			}		
		}
		type= pDmaContext->pResourceFrames[index].type;
		isUbwcDst = ((type==1)||(type==5))?1:0;
		/*Convert the User provided frame type to UBWC DMA chroma type for alignment check*/
		eFmtChroma = type2DmaChroma[type];
	
	
		/*CHECK need to work on Alignment requirements*/
		/*assert  x/y for start alignment based on type*/
		/*assert w/h for size alignment based on type*/
		dma_getMinRoiSize(eFmtChroma, isUbwcDst, pixAlignInfo);
		if (h%(pixAlignInfo.u16H) != 0 || w%(pixAlignInfo.u16W) != 0){
			error(NULL) << "ROI width and height for this application must be aligned to W = " << pixAlignInfo.u16W << "and  H = " << pixAlignInfo.u16H << "\n";
			return ERR;
		}
		if (y%(pixAlignInfo.u16H) != 0 || x%(pixAlignInfo.u16W) != 0){
			error(NULL) << "ROI X-position and y-position for this application must be aligned to  W = " << pixAlignInfo.u16W << "and  H = " << pixAlignInfo.u16H << "\n";
			return ERR;
		}
	
		/*Store the ROI Details*/
		pDmaContext->pFoldStorage[store_id].nRoiXoffset = x;
		pDmaContext->pFoldStorage[store_id].nRoiYoffset  = y;
		pDmaContext->pFoldStorage[store_id].nRoiWalkWidth=w;
		pDmaContext->pFoldStorage[store_id].nRoiWalkHeight=h;	
		
		
	}else{
		error(NULL) << "The Device Storage Doesnt Exist \n";
		return ERR;
	}
	
	return OK;
}


/* _DmaRT_ClrHostframe
*
* clear frame
* *frame = VA of frame buffer */
int halide_hexagon_dmart_clr_host_frame (void* user_context, addr_t  frame)
{
	int fold_Idx;
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	/* Serch for the frame Entry*/
	int index = -1;
	for(int i=0; i< pDmaContext->nFrames; i++)
	{
		if ((-1==index)&& pDmaContext->pFrameTable[i].pVirtAddr==frame)
		{
			index = i;
		}		
	}
	if(-1!=index){
		/*Dis-associate frame From fold */
		fold_Idx = pDmaContext->pFrameTable[index].nWorkBufferID;
		/*Free the fold for recycling */
		pDmaContext->pFoldStorage[fold_Idx].bInuse = false;
		
		
		int DmaID = pDmaContext->pFrameTable[index].nDMAEngineID;
		int SetID = pDmaContext->pFrameTable[index].nDMASetID;
        /*Free The DMA Engine*/
		if(pDmaContext->pFrameTable[index].read){
			/*Clear the Read Resource*/
			memset((void*) &pDmaContext->psetDMAEngines[SetID].pDmaReadResource[DmaID],0,sizeof(struct s_DmaResource));		
			
		}else{
			/*Clear the Write Resource*/
			memset((void*) &pDmaContext->psetDMAEngines[SetID].pDmaWriteResource[DmaID],0,sizeof(struct s_DmaResource));					
		}		
		/*Free the H/W Thread*/
		pDmaContext->psetDMAEngines[SetID].bInuse = false;
        /*Clear the frame Context*/
		memset((void*) &pDmaContext->pResourceFrames[index],0,sizeof(struct s_ResourcePerFrame));
        
		/*Clear the frame Table*/
		memset((void*)&pDmaContext->pFrameTable[index],0,sizeof(struct s_FrameTable));
        		
		/*Decrement the frameCnt (index) as a frame is deleted*/
		pDmaContext->frameCnt--;
		
	}else{
		/* Error the frame doesnt Exist*/
		error(NULL) << "frame to be Freed doesnt exist\n";
		/*Currently returning -1, but setup the Error codes*/
		return ERR;
	}	

	return OK;
}



///////////////////////////////////////////////////////////////////////////////

/* Initialize the DMA Context 
* dmaContext  - pointer to Allocated DMA Context
* nFrmames    - Total Number of frames
*/


int halide_hexagon_dmaapp_dma_init(void *dmaContext, int nFrames)
{
	
	int i;
	int Engine_per_thread,rem_engine;
	t_DmaContext *pDmaContext;
	
	
	halide_assert(NULL, dmaContext!=NULL);
	halide_assert(NULL, nFrames!=0);
	
	/* Zero Initialize the Buffer*/
	memset(dmaContext,0,sizeof(t_DmaContext));
	
	pDmaContext = (t_DmaContext *)dmaContext;
	/*Initialize the Number of frames */
	pDmaContext->nFrames = nFrames;
		
	/*Allocate for frameTable */
	pDmaContext->pFrameTable = (t_FrameTable *) malloc(nFrames*sizeof(t_FrameTable));
	if(0==pDmaContext->pFrameTable){
		/*Malloc Failed to Alloc the Required Buff*/
		/* Setup Error Codes currrently returning -1*/
		error(NULL) << "Malloc Failed to allocate in DMA Init function \n";
		return ERR;
	}
	/*Init to Zero */
	memset(pDmaContext->pFrameTable,0,nFrames*sizeof(t_FrameTable));
	/* Allocate the frame Resource Struture*/
	pDmaContext->pResourceFrames = (t_ResourcePerFrame *) malloc(nFrames*sizeof(t_ResourcePerFrame));
	if(0==pDmaContext->pResourceFrames){
		/*Malloc Failed to Alloc the Required Buff*/
		/* Setup Error Codes currrently returning -1*/
		error(NULL) << "Malloc Failed to allocate in DMA Init function \n";
		return ERR;
	}
	/*Init to Zero */
	memset(pDmaContext->pResourceFrames,0,nFrames*sizeof(t_ResourcePerFrame));
	
    /* Number of Folds =  NUM_DMA_ENGINES */	
	pDmaContext->pFoldStorage = (t_WorkBuffer *) malloc(NUM_DMA_ENGINES *sizeof(t_WorkBuffer) );
	if(0==pDmaContext->pFoldStorage){
		/*Malloc Failed to Alloc for USer Structure*/
		/*currently returning -1 but need to setup Error Codes*/
		error(NULL) << "Malloc Failed to allocate in DMA Init function \n";
		return -1;
	}
	/*Init to Zero */
	memset(pDmaContext->pFoldStorage,0,NUM_DMA_ENGINES *sizeof(t_WorkBuffer));
	/* Allocate reamining structures*/
	/* Allocate DMA Read Resources */
	/* Equally share across HW threads*/
	
	Engine_per_thread = NUM_DMA_ENGINES/NUM_HW_THREADS;
	
	for (i=0;i<NUM_HW_THREADS;i++){
		/* Equally Share the DMA resources between Read and Write Engine */
		pDmaContext->psetDMAEngines[i].pDmaReadResource = (t_DmaResource *) malloc((Engine_per_thread/2)*sizeof(t_DmaResource));
		if(NULL==pDmaContext->psetDMAEngines[i].pDmaReadResource){
			/*Malloc Failed to Alloc the Required Buff*/
			/* Setup Error Codes currrently returning -1*/
			error(NULL) << "Malloc Failed to allocate in DMA Init function \n";
			return ERR;
		}
		pDmaContext->psetDMAEngines[i].nDmaReadEngines = (Engine_per_thread/2);
		/*Init to Zero */
	    memset(pDmaContext->psetDMAEngines[i].pDmaReadResource,0,(Engine_per_thread/2)*sizeof(t_DmaResource));
		/*Allocate the Remaining for Write*/
		rem_engine = Engine_per_thread - (Engine_per_thread/2);
		pDmaContext->psetDMAEngines[i].pDmaWriteResource =(t_DmaResource *) malloc(rem_engine*sizeof(t_DmaResource));
		
		if(NULL==pDmaContext->psetDMAEngines[i].pDmaWriteResource){
			/*Malloc Failed to Alloc the Required Buff*/
			/* Setup Error Codes currrently returning -1*/
			error(NULL) << "Malloc Failed to allocate in DMA Init function \n";
			return ERR;
		}
		pDmaContext->psetDMAEngines[i].nDmaWriteEngines = rem_engine;
		/*Init to Zero */
	    memset(pDmaContext->psetDMAEngines[i].pDmaWriteResource,0,rem_engine*sizeof(t_DmaResource));
	}
	

	return OK;
}


/* createContext 
* CreateContext check if DMA Engines are available
 * Allocates Memory for a DMA Context
 * Returns Error if DMA Context is not available
 */
int halide_hexagon_dmaapp_create_context(void** user_context, int nFrames)
{
	t_DmaContext *pDmaContext;
	if(!*user_context){
		/*If user Context is Null */
		/*Allocate the User Context*/
		/*This case shouldnt arise we assume that the user_context Exist*/
		*user_context = (t_HexagonContext *) malloc(sizeof(t_HexagonContext));
		t_HexagonContext *hexagon_user_context = (t_HexagonContext *) *user_context;
		/*Initializing the DMA Context to NULL so that we can allocate*/
		hexagon_user_context->pDmaContext = NULL;
	}

	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) *user_context;
	halide_assert(NULL, hexagon_user_context->pDmaContext ==0);
	/*Check if DMA Driver is Ready and Allocate the DMA Structure*/
	
	if (QURT_EOK==dma_isDMADriverReady()){
	
		/* Alloc DMA Context*/
		pDmaContext = (t_DmaContext *) malloc( sizeof(t_DmaContext));
	
		if (NULL != pDmaContext ){
			/*initialize dma_context*/
			halide_hexagon_dmaapp_dma_init((void *)pDmaContext,nFrames);
			halide_hexagon_dmart_set_context (*user_context, (void* )pDmaContext);
			
		}
		else{
			error(NULL) << "DMA structure Allocation have some issues \n";
			/* Setup Error Codes, Currently Returning -1*/
			return ERR;
		}
	}
	else{
		return ERR;
	}
	return OK;
}



/* Attach Context checks if the frame width, height and type is aligned with DMA Transfer
 * Returns Error if the frame is not aligned.
 * AttachContext needs to be called for each frame
 */
int halide_hexagon_dmaapp_attach_context(void* user_context, addr_t  frame,int type, int d, int w, int h, int s, int last)
{
	
	int nRet;
	bool isUbwcDst;
	t_eDmaFmt eFmtChroma;
	
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	halide_assert(NULL, hexagon_user_context->pDmaContext !=NULL);
	dma_tPixAlignInfo pixAlignInfo;

	/*Check for valid type and alignment requirements*/
	/*Type should be one of the entries in Union t_eUserFmt*/
	if (type>5 || type <0){
		/*Error Not a Valid type */
		error(NULL) << "the frame type is invalid in dmaapp_attach_context \n";
		return -1;
	}
	isUbwcDst = ((type==1)||(type==5))?1:0;
	/*Convert the User provided frame type to UBWC DMA chroma type for alignment check*/
	eFmtChroma = type2DmaChroma[type];
	
	nRet = dma_getFormatAlignment(eFmtChroma, isUbwcDst, pixAlignInfo);
    if (nRet != QURT_EOK){
    	if (h%(pixAlignInfo.u16H) != 0 || w%(pixAlignInfo.u16W) != 0){
    		error(NULL) << "frame width and height for this application must be aligned to W=" << pixAlignInfo.u16W << "H=" <<  pixAlignInfo.u16H << "\n";
    		return ERR;
    	}
    	return ERR;
    }
	
	    
	if(0!=frame){
		nRet = halide_hexagon_dmart_set_host_frame(user_context, frame, type, d, w, h, s, last);
		if(nRet){
			/*Error in setting the Host frame*/
			error(NULL) << "hexagon_dmart_set_host_frame function failed \n";
			return ERR;
		}
		
	}

	return OK;
}



/* Detach Context signals the end of frame
* This call is used when user has more frames to process
* and does not want to free the DMA Engines
* Return an Error if there is an error in DMA Transfer
*/
int halide_hexagon_dmaapp_detach_context(void* user_context, addr_t  frame)
{
	int index,i;
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	halide_assert(NULL, hexagon_user_context->pDmaContext !=NULL);
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	
    if(0 !=frame ){
		/* Serch for the frame Entry*/
		index = -1;
		for(i=0; i< pDmaContext->nFrames; i++)
		{
			if ((-1==index)&& pDmaContext->pFrameTable[i].pVirtAddr==frame)
			{
				index = i;
			}		
		}
		if(-1!=index){
			//if(!(pDmaContext->pFrameTable[index].read)){
				/*Write frame, need to call DMA Finish function */
				/*This function yet to be populated*/
			//	dma_finish_frame();				
			//}
			/*Clear the frame */
			halide_hexagon_dmart_clr_host_frame (user_context,frame);
			
		}else{
			error(NULL) << "Error, the frame Deoesnt exist to detach \n";
			/*Currently Returning -1, need to setup Error Codes*/
			return ERR;
		}		
		
		
	}else{
		/*Error frame is null*/
		error(NULL) << "The frame provided to function dmaapp_detach_context is NULL \n";
		/*Currently Returning -1, setup Return Codes*/
		return ERR;
	} 

	return OK;
}



/* Delete Context frees up the dma handle if not yet freed
 * and deallocates memory for the userContext
 */
int halide_hexagon_dmaapp_delete_context(void* user_context)
{
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	halide_assert(NULL, hexagon_user_context->pDmaContext !=NULL);
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	
	/*De-associate all the allocated Buffers*/
	pDmaContext->pFoldStorage = NULL;
	pDmaContext->pFrameTable = NULL;
	pDmaContext->pResourceFrames = NULL;
	for (int i=0;i<NUM_HW_THREADS;i++){
		pDmaContext->psetDMAEngines[i].pDmaReadResource = NULL;
		pDmaContext->psetDMAEngines[i].pDmaWriteResource =NULL;
	}
	/*Finally Free the DMA Context */	
    pDmaContext = NULL;

	return OK;
}




int  halide_hexagon_dmart_isBufferRead(void* user_context, addr_t  frame,bool *ret_val)
{
	
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	int index = -1;
	/*Search the frame in the frame Table*/
	for(int i=0;i<pDmaContext->nFrames;i++){
		if((-1==index)&&pDmaContext->pFrameTable[i].pVirtAddr == frame){
			index = i;
		}		
	}
	
	if(-1!=index){
		/*frame Exist */

		*ret_val = pDmaContext->pFrameTable[index].read;
  	}else{
		/*The frame Doesnt Exist */
  		error(NULL) << "The frame Doesnt Exist \n";
		return -1;
	}

	return OK;
}
int halide_hexagon_dmart_get_fold_size(void* user_context,addr_t  frame, unsigned int *Size)
{	
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	int index = -1;
	/*Search the frame in the frame Table*/
	for(int i=0;i<pDmaContext->nFrames;i++){
		if((-1==index)&&pDmaContext->pFrameTable[i].pVirtAddr == frame){
			index = i;
		}		
	}
	
	if(-1!=index){

		*Size =  pDmaContext->pResourceFrames[index].foldBuffSize;
  	}else{
		/*The frame Doesnt Exist */
  		error(NULL) << "The frame Doesnt Exist \n";
		return -1;
	}
	
	return OK;

}


/*The Function will return the Number of Components (Planes ) in the frame*/
/* will retun 1 - for Y plane */
/* will retun 1 - for UV plane */
/* will retun 2 - for both Y and UV planes */
int halide_hexagon_dmart_get_num_components (void* user_context, addr_t  frame, int *nComponents){
	int index;
	int i;
	
	halide_assert(NULL, user_context!=NULL);
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	/* Serch if the frame Already Exists*/
	index = -1;
	for( i=0 ;i< pDmaContext->nFrames; i++)
	{
		if ((-1==index)&& pDmaContext->pFrameTable[i].pVirtAddr==frame)
		{
			index = i;
		}		
	}
	if (-1==index)
	{
		/*Error the frame index Doesnt Exist */
		error(NULL) << "The frame with the given VA doesnt exist \n";
		/*Currently Returning -1, we have to setup Error Codes*/
		return -1;
		
	}else{
		    /* frame Exists get the number of components */
			int plane = pDmaContext->pResourceFrames[index].plane;
			/*Return 1 for both Chroma and Luma Components  */
			/*Return 2 in case of Other i.e having both Planes*/
			*nComponents = ((plane ==LUMA_COMPONENT)||(plane==CHROMA_COMPONENT))?1:2;
			
			
	}	

	/* Return Success */
	return OK;
}

int halide_hexagon_dmart_allocateDMA(void* user_context,addr_t  frame, bool *dma_allocate)
{	
	
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);

	int index = -1;
	/*Search the frame in the frame Table*/
	for(int i=0;i<pDmaContext->nFrames;i++){
		if((-1==index)&&pDmaContext->pFrameTable[i].pVirtAddr == frame){
			index = i;
		}
	}

	if(-1!=index){
		/*frame Exist */
		error(NULL) << "pDmaContext->frameCnt " << pDmaContext->frameCnt << "\n";

		int setID = pDmaContext->pFrameTable[index].nDMASetID;
		int engineID =  pDmaContext->pFrameTable[index].nDMAEngineID;

		error(NULL) << "setID " << setID <<  "engineID " << engineID << "\n";


		if(pDmaContext->pFrameTable[index].read){
			/*Read Engine */
			if(pDmaContext->psetDMAEngines[setID].pDmaReadResource[engineID].pSession.dmaRdWrapper ==NULL){
				*dma_allocate = true;
			}
		}else{
			/*Write Engine*/
			if(pDmaContext->psetDMAEngines[setID].pDmaWriteResource[engineID].pSession.dmaWrWrapper == NULL){
				*dma_allocate = false;
			}
		}
	}else{
		/*The frame Doesnt Exist */
		error(NULL) << "The frame Doesnt Exist \n";
		*dma_allocate = false;
		return -1;

	}

	return OK;
}

/*set Functions*/
int halide_hexagon_dmart_set_dma_handle(void* user_context, void*handle, addr_t frame)
{
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	int index = -1;
	/*Search the frame in the frame Table*/
	for(int i=0;i<pDmaContext->nFrames;i++){
		if((-1==index)&&pDmaContext->pFrameTable[i].pVirtAddr == frame){
			index = i;
		}		
	}
	
	if(-1!=index){
		/*frame Exist */
		
		int setID = pDmaContext->pFrameTable[index].nDMASetID;
		int engineID =  pDmaContext->pFrameTable[index].nDMAEngineID;

		if(pDmaContext->pFrameTable[index].read){
			pDmaContext->psetDMAEngines[setID].pDmaReadResource[engineID].pSession.dmaRdWrapper = handle;
		}else{
			pDmaContext->psetDMAEngines[setID].pDmaWriteResource[engineID].pSession.dmaWrWrapper = handle;
		}		
  	}else{
		/*The frame Doesnt Exist */
  		error(NULL) << "The frame Doesnt Exist \n";
		return -1;
		
	}

	return OK;

}

void* halide_hexagon_dmart_get_readHandle(void* user_context, addr_t frame)
{
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	
	halide_assert(NULL, pDmaContext != NULL);
	
	
	
	int index = -1;
	/*Search the frame in the frame Table*/
	for(int i=0;i<pDmaContext->nFrames;i++){
		if((-1==index)&&pDmaContext->pFrameTable[i].pVirtAddr == frame){
			index = i;
		}		
	}
	
	if(-1!=index){
		/*frame Exist */

		int setID = pDmaContext->pFrameTable[index].nDMASetID;
		int engineID =  pDmaContext->pFrameTable[index].nDMAEngineID;

		return pDmaContext->psetDMAEngines[setID].pDmaReadResource[engineID].pSession.dmaRdWrapper;
  	}else{
		/*The frame Doesnt Exist */
  		error(NULL) << "The frame Doesnt Exist \n";
		return NULL;
	}
}

void* halide_hexagon_dmart_get_writeHandle(void* user_context, addr_t frame)
{
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	
	int index = -1;
	/*Search the frame in the frame Table*/
	for(int i=0;i<pDmaContext->nFrames;i++){
		if((-1==index)&&pDmaContext->pFrameTable[i].pVirtAddr == frame){
			index = i;
		}		
	}
	
	if(-1!=index){
		/*frame Exist */
		
		int setID = pDmaContext->pFrameTable[index].nDMASetID;
		int engineID =  pDmaContext->pFrameTable[index].nDMAEngineID;

		return pDmaContext->psetDMAEngines[setID].pDmaWriteResource[engineID].pSession.dmaWrWrapper;
  	}else{
		/*The frame Doesnt Exist */
  		error(NULL) << "The frame Doesnt Exist \n";
		return NULL;
	}
	
}

int halide_hexagon_dmart_set_fold_storage(void* user_context, addr_t addr,qurt_mem_region_t tcm_region, qurt_size_t size,addr_t desc_va,qurt_mem_region_t desc_region,qurt_size_t desc_size, int *fold_id)
{

	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);

	pDmaContext->pFoldStorage[pDmaContext->foldCnt].pVAFoldBuffer = addr;
	pDmaContext->pFoldStorage[pDmaContext->foldCnt].bInuse = 0;
	pDmaContext->pFoldStorage[pDmaContext->foldCnt].addrL2PhysAddr_IntermBufPing = dma_lookUpPhysicalAddress(addr);
    pDmaContext->pFoldStorage[pDmaContext->foldCnt].tcm_region = tcm_region;
    pDmaContext->pFoldStorage[pDmaContext->foldCnt].pDescVA	= desc_va;
	pDmaContext->pFoldStorage[pDmaContext->foldCnt].desc_region = desc_region;
	pDmaContext->pFoldStorage[pDmaContext->foldCnt].sizeDesc = desc_size;
	pDmaContext->pFoldStorage[pDmaContext->foldCnt].sizeTcm = size;

    *fold_id = pDmaContext->foldCnt;

	pDmaContext->foldCnt++;
	

	return OK;
}


int halide_hexagon_dmart_get_update_params(void* user_context, addr_t buf_addr, dma_tMoveParams* param)
{
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	int store_id = -1;
	
	for (int i=0;i<pDmaContext->foldCnt;i++){
		if((-1==store_id) && (pDmaContext->pFoldStorage[i].pVAFoldBuffer==buf_addr)){
			store_id = i;			
		}		
	}

	if(-1!=store_id)
	{
		param->yoffset = pDmaContext->pFoldStorage[store_id].nRoiYoffset;
		param->roiHeight = pDmaContext->pFoldStorage[store_id].nRoiWalkHeight ;
		param->xoffset = pDmaContext->pFoldStorage[store_id].nRoiXoffset;
		param->roiWidth = pDmaContext->pFoldStorage[store_id].nRoiWalkWidth;
		// The TCM address is updated given the index of the buffer to use (ping/pong) which alternates on every DMA transfer.
		param->ping_buffer = pDmaContext->pFoldStorage[store_id].addrL2PhysAddr_IntermBufPing;
		param->Offset =  pDmaContext->pFoldStorage[store_id].offset;
		param->l2ChromaOffset = pDmaContext->pFoldStorage[store_id].offset;
  	        return OK;
	}
	else
	   return ERR;

}


int  halide_hexagon_dmart_get_tcmDesc_params(void* user_context, addr_t  dev_buf,qurt_mem_region_t *tcm_region,qurt_size_t *size_tcm,addr_t  *desc_va,qurt_mem_region_t *desc_region,qurt_size_t *desc_size){
	
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);
	int store_id = -1;
	
	for (int i=0;i<pDmaContext->foldCnt;i++){
		if((-1==store_id) && (pDmaContext->pFoldStorage[i].pVAFoldBuffer==dev_buf)){
			store_id = i;			
		}		
	}
	if(-1!=store_id){
		*tcm_region = pDmaContext->pFoldStorage[store_id].tcm_region;
		*desc_va=	pDmaContext->pFoldStorage[store_id].pDescVA;
		*desc_region=	pDmaContext->pFoldStorage[store_id].desc_region;
		*desc_size=	pDmaContext->pFoldStorage[store_id].sizeDesc;
		*size_tcm = pDmaContext->pFoldStorage[store_id].sizeTcm;
	}else{
		/* The fold Buffer doesnt exist */
		error(NULL) << " Device Buffer Doesnt exist \n";
		return ERR;
	}	
	return OK;
	
}

int halide_hexagon_dmart_get_last_frame(void* user_context, addr_t  frame, bool *last_frame)
{
	t_HexagonContext *hexagon_user_context = (t_HexagonContext *) user_context;
	t_DmaContext *pDmaContext = hexagon_user_context->pDmaContext;
	halide_assert(NULL, pDmaContext != NULL);

	int index = -1;

	    /*Search the frame in the frame Table*/
		for(int i=0;i<pDmaContext->nFrames;i++){
			if((-1==index)&&pDmaContext->pFrameTable[i].pVirtAddr == frame){
				index = i;
			}
		}

	if(-1!=index){
			/*frame Exist */

			int setID = pDmaContext->pFrameTable[index].nDMASetID;
			int engineID =  pDmaContext->pFrameTable[index].nDMAEngineID;

			if(pDmaContext->pFrameTable[index].read){
 			   *last_frame = pDmaContext->psetDMAEngines[setID].pDmaReadResource[engineID].pFrame->end_frame;
			}
			else{
			  *last_frame = pDmaContext->psetDMAEngines[setID].pDmaWriteResource[engineID].pFrame->end_frame;
			}
	}else{
		/* The fold Buffer doesnt exist */
		error(NULL) << " The frame doesnt exist \n";
		return ERR;
	}
	return OK;

    
}

#endif
