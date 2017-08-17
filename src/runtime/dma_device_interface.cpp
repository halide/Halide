#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "mini_qurt.h"
#include "mini_dma.h"
#include "dma_device_shim.h"
#include "HalideRuntimeHexagonDma.h"
#include "hexagon_dma_rt.h"
#include "hexagon_dma_context.h"


using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::Qurt;


extern "C" {

extern WEAK halide_device_interface_t hexagon_dma_device_interface;


int halide_dma_device_release(void *user_context)
{


    return 0;
}

int halide_dma_device_malloc(void *user_context,  halide_buffer_t *buf) {
	
    int nRet = OK;
	t_eDmaFmt chroma_type,luma_type;
	int FrameIdx;
	t_DmaContext *pDmaContext;

	qurt_mem_region_t region_tcm;
	qurt_mem_region_t region_tcm_desc;
	qurt_addr_t  tcm_buf_vaddr, tcm_desc_vaddr;
	qurt_size_t region_tcm_desc_size;
	qurt_mem_pool_t pool_tcm;

	/*Get the Frame Index*/
	if(halide_hexagon_dmart_get_frame_index(user_context, (addr_t)buf->host, &FrameIdx)){
		error(NULL) << "Function failed to get Frame Index  \n";
		return ERR;

	}
	/*Get the Dma Context*/
	if(halide_hexagon_dmart_get_context (user_context, (void**) &pDmaContext)){
		error(NULL) << "Function failed to get DMA Context \n";
		return ERR;
	}
	/*Get the Chroma and Luma Type*/
	chroma_type = pDmaContext->pResourceFrames[FrameIdx].chromaType;
	luma_type = pDmaContext->pResourceFrames[FrameIdx].lumaType;



	t_eDmaFmt aeFmtId[2] = {luma_type,chroma_type};
	int nROIWidth = 0;
	int nROIHeight = 0;
	bool isUBWC = 0;
	bool padding = 0;
	int frameWidth = 0;
	int frameHeight = 0;
	int frameStride = 0;
	int lumaStride = 0;
	int chromaStride = 0;
	void* dmaHandle = NULL;
	int nComponents;
	int nFolds;
	int plane;

	/* Get the necessary Parameters from DMA Context*/
	/*Get ROI and Padding Details */
	nROIWidth = pDmaContext->pResourceFrames[FrameIdx].foldWidth;
	nROIHeight = pDmaContext->pResourceFrames[FrameIdx].foldHeight;
	padding = pDmaContext->pResourceFrames[FrameIdx].padding;
	isUBWC = pDmaContext->pResourceFrames[FrameIdx].isUBWC;
	/*Get the Frame Details*/
	frameWidth = pDmaContext->pResourceFrames[FrameIdx].frameWidth;
	frameHeight = pDmaContext->pResourceFrames[FrameIdx].frameHeight;
	frameStride = pDmaContext->pResourceFrames[FrameIdx].frameStride;
	plane = pDmaContext->pResourceFrames[FrameIdx].plane;
	nComponents = ((plane==LUMA_COMPONENT)|| (plane==CHROMA_COMPONENT))?1:2;
	nFolds = pDmaContext->pResourceFrames[FrameIdx].nFolds;

	dma_tPixAlignInfo pStRoiSize;
	pStRoiSize.u16W = nROIWidth;
	pStRoiSize.u16H = nROIHeight;
	// Note: Both the source and destination are checked for UBWC mode as buffers are shared between the source frame and destination frame.
	lumaStride = dma_getStride(luma_type, isUBWC, pStRoiSize);
	chromaStride = dma_getStride(chroma_type, isUBWC, pStRoiSize);

	//##################################################################################################################
	//Allocate DMA if required
	//##################################################################################################################
	bool dma_allocate;

	if(halide_hexagon_dmart_allocateDMA(user_context,(addr_t )buf->host, &dma_allocate)){
		error(NULL) << "Function failed to check if DMA Alloc is needed \n";
		return ERR;

	}
	if(dma_allocate){

		/* No Free DMA Engine Available, Allocate one */
		dmaHandle = dma_allocateDMAEngine();

		if(dmaHandle == 0)
		{
			error(NULL) << "halide_hexagon_dma_device_malloc:Failed to allocate the read DMA engine.\n";
			return ERR;
		}
		if(halide_hexagon_dmart_set_dma_handle(user_context, dmaHandle,(addr_t)buf->host)){
			error(NULL) << "Function failed to set DMA Handle to DMA context \n";
			return ERR;

		}
	}else{
		/*An Allocated DMA Already Exists Re-use it */
		bool read = pDmaContext->pFrameTable[FrameIdx].read;
		if(read){
			dmaHandle = halide_hexagon_dmart_get_readHandle(user_context,(addr_t )buf->host);

		}else{
			dmaHandle = halide_hexagon_dmart_get_writeHandle(user_context,(addr_t )buf->host);

		}
	}


	/*Check if there are any Free and Allocated Fold Storage Available */
	/*If none of them are free we can try allocating a New Fold Storage*/
	bool Fold_Exists = false;
	int FoldIdx;
	halide_hexagon_dmart_get_free_fold (user_context, &Fold_Exists, &FoldIdx);

	if(Fold_Exists){
		/*An Allocated and Free Fold Exists*/
		/* re-use it*/

		buf->device = pDmaContext->pFoldStorage[FoldIdx].pVAFoldBuffer;
		// Not able to use the Existing Descriptors
		region_tcm_desc_size = pDmaContext->pFoldStorage[FoldIdx].sizeDesc;
		tcm_desc_vaddr = pDmaContext->pFoldStorage[FoldIdx].pDescVA;


	}
	else{
		/*Free Fold Doesnt Exists We will try allocating one */

		//#####################################################################################################################
		//Now allocate the descriptors
		// 2 ping pong buffers for each frame ( read and write so two)
		//################################################################################################################################
		region_tcm_desc_size = dma_getDescriptorSize(aeFmtId,nComponents,nFolds);

		//#################################################################################################################################
		//Now allocate the L2 Cache
		//##################################################################################################################################


		//The maximum TCM Buf Size
		qurt_size_t tcm_buf_size;
		if(halide_hexagon_dmart_get_fold_size(user_context,(addr_t )buf->host,&tcm_buf_size)){
			error(NULL) << "Function failed to get Fold Buffer Size \n";
			return ERR;
		}
		/* Allocating in multiples of 4K */
		qurt_size_t region_tcm_size = ALIGN(tcm_buf_size, 0x1000);


		// It is a good idea to check that this size is not too large, as while we are still in DDR, when locked to the TCM
		// large sizes will become problematic.
		qurt_size_t region_tcm_limit = 0x40000; // Limit is set to 256k
		if (region_tcm_size > region_tcm_limit){
			error(NULL) << "The required TCM region for this ROI" << region_tcm_size << "exceeds the set limit of " << region_tcm_limit;
			error(NULL) << "The ROI must be lowered or the allowed region made larger.\n" ;
			nRet = ERR;
			return nRet;
		}


		nRet = dma_getMemPoolID(&pool_tcm);
		if(nRet != QURT_EOK){
			error(NULL) << "Failed to attach the TCM memory region. The error code is:" << nRet << "\n" ;
			nRet = ERR;
			return nRet;
		}



		tcm_buf_vaddr = dma_allocateCache(pool_tcm, region_tcm_size, &region_tcm);
		tcm_desc_vaddr = dma_allocateCache(pool_tcm, region_tcm_desc_size, &region_tcm_desc);

		// Lock the TCM region. This maps the region mapped as TCM to the actual TCM.
		// This is done to ensure the region is not invalidated during DMA processing.
		// This can also be done through the firmware (using the nDmaWrapper_LockCache function).
		nRet = dma_lockCache(tcm_buf_vaddr, region_tcm_size);
		if(nRet != QURT_EOK){
			error(NULL) << "QURT TCM lock failed due to QURT_EALIGN ERROR misaligned u32Size = " << region_tcm_size << "\n";
			nRet = ERR;
			return nRet;
		}
		// Lock the descriptor region as well.
		nRet  = dma_lockCache(tcm_desc_vaddr, region_tcm_desc_size);
		if(nRet != QURT_EOK){
			error(NULL) << "QURT TCM lock failed due to QURT_EALIGN ERROR misaligned u32Size = " << region_tcm_desc_size << "\n" ;
			nRet = ERR;
			return nRet;
		}


		if(halide_hexagon_dmart_set_fold_storage(user_context, tcm_buf_vaddr,region_tcm,tcm_buf_size,tcm_desc_vaddr,region_tcm_desc,region_tcm_desc_size, &FoldIdx)){
			error(NULL) << "Function failed to Set Fold Storage to DMA context \n";
			return ERR;
		}
		/*Assign the Allocated Fold Storage to Device Memory*/
		buf->device = tcm_buf_vaddr;
	}
	/*Now We have Allocated the Fold and will link it to the Frame*/
	if(halide_hexagon_dmart_set_storage_linkage (user_context,(addr_t)buf->host, (addr_t) buf->device , FoldIdx)){
		error(NULL) << "Function failed to Link Frame and Fold Storage \n";
		return ERR;
	}

	//Populate Work Descriptors and Prepare DMA
	//##########################################################################################################################################################


	dma_tPrepareParams params;
	params.handle = dmaHandle;
	params.hostAddress =  (addr_t )buf->host;
	params.frameWidth = frameWidth;
	params.frameHeight = frameHeight;
	params.frameStride = frameStride;
	params.roiWidth = nROIWidth;
	params.roiHeight = nROIHeight;
	params.lumaStride = lumaStride;
	params.chromaStride = chromaStride;
	params.lumaType = luma_type;
	params.chromaType = chroma_type;
	params.nComponents = nComponents;
	params.padding = padding;
	params.isUBWC = isUBWC;
	params.desc_address = tcm_desc_vaddr;
	params.desc_size = region_tcm_desc_size;

	nRet = dma_prepareForTransfer(params);
	if(nRet != QURT_EOK)
	{
		error(NULL) << "Error in Preparing for DMA Transfer\n";
		return nRet;
	}

	return nRet;
}

int halide_dma_device_free(void *user_context,  halide_buffer_t *buf) {
	
	void* handle = NULL;
    halide_assert(NULL, user_context!=NULL);
    halide_assert(NULL, buf!=NULL);

	bool read_flag = false;
    if(halide_hexagon_dmart_isBufferRead(user_context,(addr_t)buf->host,&read_flag)){
    	error(NULL) << "Function failed to get Operation type: (Read | Write) Flag \n";
    	return ERR;
    }


    if(read_flag){
    	handle = halide_hexagon_dmart_get_readHandle(user_context,(addr_t)buf->host);
    	if(handle == 0){
    		error(NULL) << "Function failed to get DMA Read Handle  \n";
    	 	return ERR;
    	}
    }else{
    	handle = halide_hexagon_dmart_get_writeHandle(user_context,(addr_t)buf->host);
    	if(handle == 0){
    		error(NULL) << "Function failed to get DMA Write Handle  \n";
			return ERR;
        }

    }
    if(handle != 0)
    {
    	bool last_frame;
		if(halide_hexagon_dmart_get_last_frame(user_context,(addr_t)buf->host,&last_frame)){
			error(NULL) << "Function failed to get if the Frame is last frame or not \n";
			return ERR;		
		}
        dma_finishFrame(handle);
        if (last_frame)
        {			
			dma_freeDMA(handle);
        }
    }
	
	/* Free the Allocated Qurt Memory Regions*/	
	qurt_mem_region_t tcm_region,desc_region;
	qurt_addr_t desc_va;
	qurt_size_t desc_size, tcm_size;
	
		
	if(halide_hexagon_dmart_get_tcmDesc_params(user_context, (addr_t) buf->device,&tcm_region,&tcm_size,(addr_t *)&desc_va,&desc_region, &desc_size)){
		error(NULL) << "Function failed to get TCM Desc Params  \n";
    	return ERR;
	}
		
	/* Release the TCM regions that were locked.*/
    /* This can also be done through the firmware (using the nDmaWrapper_UnlockCache function).*/
    int nRet  = dma_unlockCache (buf->device, tcm_size);
    if(nRet != QURT_EOK){
    	error(NULL) << "QURT TCM unlock failed due to QURT_EALIGN ERROR misaligned u32Size = " << tcm_size << "\n";
    	nRet = ERR;
        return nRet;
    }
    nRet  = dma_unlockCache (desc_va, desc_size);
    if(nRet != QURT_EOK){
    	error(NULL) << "QURT TCM descriptor unlock failed due to QURT_EALIGN ERROR misaligned u32Size = " << desc_size << "\n";
    	nRet = ERR;
        return nRet;
    }

    // Delete all regions that were allocated.
    dma_deleteMemRegion(tcm_region);
    dma_deleteMemRegion(desc_region);

    buf->device = 0;
	
    return 0;
}


int halide_dma_copy_to_device(void *user_context,  halide_buffer_t *buf)
{
	void* handle = NULL;
	int nRet;
	
	halide_assert(NULL, user_context!=NULL);
	halide_assert(NULL, buf!=NULL);
	
	
	bool read_flag;
	if(halide_hexagon_dmart_isBufferRead(user_context,(addr_t )buf->host,&read_flag)){
		error(NULL) << "Function failed to get Operation type: (Read | Write) Flag \n";
		return ERR;				
	}	
    
	if(read_flag){
		handle = halide_hexagon_dmart_get_readHandle(user_context,(addr_t )buf->host);
		if(handle == NULL){
			error(NULL) << "Function failed to get DMA Read Handle  \n";
			return ERR;			
		}				
	}else{
		return ERR;		
	}
	int nComponents;
	if(halide_hexagon_dmart_get_num_components(user_context,(addr_t )buf->host,&nComponents)){
		error(NULL) << "Function failed to get number of Components from DMA Context \n";
		return ERR;		
	}

	dma_tMoveParams moveParam;
	halide_hexagon_dmart_get_update_params(user_context, (addr_t)buf->device, &moveParam);

	moveParam.handle = handle;
	moveParam.nComponents = nComponents;
	nRet = dma_moveData(moveParam);
	if(nRet != QURT_EOK)
		return ERR;

	return nRet;
}


int halide_dma_copy_to_host(void *user_context,  halide_buffer_t *buf)
{
	void* handle;
	bool read_flag;
	if(halide_hexagon_dmart_isBufferRead(user_context,(addr_t )buf->host,&read_flag)){
		error(NULL) << "Function failed to get Operation type: (Read | Write) Flag \n";
		return ERR;				
	}

	if(!read_flag){
		handle = halide_hexagon_dmart_get_writeHandle(user_context,(addr_t )buf->host);
		if(handle == 0){
			error(NULL) << "Function failed to get DMA Read Handle  \n";
			return ERR;			
		}		
	}else{
		return ERR;
	}
	int nComponents;
	if(halide_hexagon_dmart_get_num_components(user_context,(addr_t )buf->host,&nComponents)){
		error(NULL) << "Function failed to get number of Components from DMA Context \n";
		return ERR;		
	}

	dma_tMoveParams moveParam;
	halide_hexagon_dmart_get_update_params(user_context, (addr_t)buf->device, &moveParam);

	moveParam.handle = handle;
	moveParam.nComponents = nComponents;

	int nRet;
	nRet = dma_moveData(moveParam);
	if(nRet != QURT_EOK)
		return ERR;

    return nRet;
}


int halide_dma_device_sync(void* user_context,  halide_buffer_t *buf)
{
	int nRet;
	void* handle=NULL;
	bool read_flag;
	
	if(halide_hexagon_dmart_isBufferRead(user_context,(addr_t )buf->host,&read_flag)){
		error(NULL) << "Function failed to get Operation type: (Read | Write) Flag \n";
		return ERR;				
	}
    if(read_flag){
    	handle = halide_hexagon_dmart_get_readHandle(user_context,(addr_t )buf->host);
		if(handle == 0){
			error(NULL) << "Function failed to get DMA Read Handle  \n";
			return ERR;			
		}
		nRet = dma_wait(handle);
				
    }else{
    	handle = halide_hexagon_dmart_get_writeHandle(user_context,(addr_t )buf->host);
		if(handle == 0){
			error(NULL) << "Function failed to get DMA Write  Handle  \n";
			return ERR;			
		}
		nRet = dma_wait(handle);
		
	}
	
	return nRet;
}


int halide_dma_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    int result = halide_dma_device_malloc(user_context, buf);
    return result;
}

int halide_dma_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    halide_dma_device_free(user_context, buf);
    buf->host = NULL;
    return 0;
}


WEAK const halide_device_interface_t *halide_hexagon_dma_device_interface() {
    return &hexagon_dma_device_interface;
}

}

WEAK halide_device_interface_impl_t hexagon_dma_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_dma_device_malloc,
    halide_dma_device_free,
    halide_dma_device_sync,
    halide_dma_device_release,
    halide_dma_copy_to_host,
    halide_dma_copy_to_device,
    halide_dma_device_and_host_malloc,
    halide_dma_device_and_host_free,
    halide_default_device_wrap_native,
    halide_default_device_detach_native,
};

struct halide_device_interface_t hexagon_dma_device_interface = {
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

