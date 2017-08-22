/**
*  This file contains the definitions of the hexagon dma device interface class
* This file is an interface between Halide Runtime and the DMA driver
* The functions in this file take care of the various functionalities of the
* DMA driver through their respective halide device interface functions
* The function in this file use the Hexagon DMA context structure for sharing
* the DMA context across various APIs of the
* DMA device interface
*/

#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "mini_qurt.h"
#include "hexagon_mini_dma.h"
#include "hexagon_dma_device_shim.h"
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
	
    int nRet = E_OK;
	t_eDmaFmt chroma_type,luma_type;
	int frame_idx;
	t_dma_context* dma_context;
	qurt_mem_region_t region_tcm;
	qurt_mem_region_t region_tcm_desc;
	qurt_addr_t  tcm_buf_vaddr, tcm_desc_vaddr;
	qurt_size_t region_tcm_desc_size;
	qurt_mem_pool_t pool_tcm;

	/*Get the Frame Index*/
	if(halide_hexagon_dmart_get_frame_index(user_context, (addr_t)buf->host, &frame_idx))
	{
		error(NULL) << "Function failed to get Frame Index  \n";
		return E_ERR;
	}
	/*Get the Dma Context*/
	if(halide_hexagon_dmart_get_context (user_context, (void**) &dma_context))
	{
		error(NULL) << "Function failed to get DMA Context \n";
		return E_ERR;
	}
	/*Get the Chroma and Luma Type*/
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
	int ncomponents;
	int nfolds;
	int plane;

	/* Get the necessary Parameters from DMA Context*/
	/*Get ROI and Padding Details */
	roi_width = dma_context->presource_frames[frame_idx].fold_width;
	roi_height = dma_context->presource_frames[frame_idx].fold_height;
	padding = dma_context->presource_frames[frame_idx].padding;
	is_ubwc = dma_context->presource_frames[frame_idx].is_ubwc;
	/*Get the Frame Details*/
	frame_width = dma_context->presource_frames[frame_idx].frame_width;
	frame_height = dma_context->presource_frames[frame_idx].frame_height;
	frame_stride = dma_context->presource_frames[frame_idx].frame_stride;
	plane = dma_context->presource_frames[frame_idx].plane;
	ncomponents = ((plane==LUMA_COMPONENT)|| (plane==CHROMA_COMPONENT))?1:2;
	nfolds = dma_context->presource_frames[frame_idx].num_folds;

	t_dma_pix_align_info pst_roi_size;
	pst_roi_size.u16W = roi_width;
	pst_roi_size.u16H = roi_height;
	// Note: Both the source and destination are checked for UBWC mode
	// as buffers are shared between the source frame and destination frame.
	luma_stride = dma_get_stride(luma_type, is_ubwc, pst_roi_size);
	chroma_stride = dma_get_stride(chroma_type, is_ubwc, pst_roi_size);

	//#######################################################
	//Allocate DMA if required
	//######################################################
	bool dma_allocate;
	if(halide_hexagon_dmart_allocate_dma(user_context,(addr_t )buf->host, &dma_allocate))
	{
		error(NULL) << "Function failed to check if DMA Alloc is needed \n";
		return E_ERR;
	}
	if(dma_allocate)
	{
		/* No Free DMA Engine Available, Allocate one */
		dma_handle = dma_allocate_dma_engine();
		if(dma_handle == 0)
		{
			error(NULL) << "halide_hexagon_dma_device_malloc:Failed to allocate the read DMA engine.\n";
			return E_ERR;
		}
		if(halide_hexagon_dmart_set_dma_handle(user_context, dma_handle,(addr_t)buf->host))
		{
			error(NULL) << "Function failed to set DMA Handle to DMA context \n";
			return E_ERR;

		}
	}
	else
	{
		/*An Allocated DMA Already Exists Re-use it */
		bool read = dma_context->pframe_table[frame_idx].read;
		if(read)
		{
			dma_handle = halide_hexagon_dmart_get_read_handle(user_context,(addr_t )buf->host);
		}
		else
		{
			dma_handle = halide_hexagon_dmart_get_write_handle(user_context,(addr_t )buf->host);

		}
	}

	/*Check if there are any Free and Allocated Fold Storage Available */
	/*If none of them are free we can try allocating a New Fold Storage*/
	bool fold_exists = false;
	int fold_idx;
	halide_hexagon_dmart_get_free_fold (user_context, &fold_exists, &fold_idx);

	if(fold_exists)
	{
		/*An Allocated and Free Fold Exists*/
	    /* re-use it*/
		buf->device = dma_context->pfold_storage[fold_idx].fold_virtual_addr;
		// Not able to use the Existing Descriptors
		region_tcm_desc_size = dma_context->pfold_storage[fold_idx].size_desc;
		tcm_desc_vaddr = dma_context->pfold_storage[fold_idx].desc_virtual_addr;
	}
	else
	{
		/*Free Fold Doesnt Exists We will try allocating one */

		//#################################################################################
		//Now allocate the descriptors
		// 2 ping pong buffers for each frame ( read and write so two)
		//################################################################################
		region_tcm_desc_size = dma_get_descriptor_size(ae_fmt_id,ncomponents,nfolds);

		//#########################################################
		//Now allocate the L2 Cache
		//########################################################
		//The maximum TCM Buf Size
		qurt_size_t tcm_buf_size;
		if(halide_hexagon_dmart_get_fold_size(user_context,(addr_t )buf->host,&tcm_buf_size))
		{
			error(NULL) << "Function failed to get Fold Buffer Size \n";
			return E_ERR;
		}
		/* Allocating in multiples of 4K */
		qurt_size_t region_tcm_size = ALIGN(tcm_buf_size, 0x1000);

		// It is a good idea to check that this size is not too large, as while we are still in DDR, when locked to the TCM
		// large sizes will become problematic.
		qurt_size_t region_tcm_limit = 0x40000; // Limit is set to 256k
		if (region_tcm_size > region_tcm_limit)
		{
			error(NULL) << "The required TCM region for this ROI" << region_tcm_size << \
					      "exceeds the set limit of " << region_tcm_limit;
			error(NULL) << "The ROI must be lowered or the allowed region made larger.\n" ;
			nRet = E_ERR;
			return nRet;
		}

		nRet = dma_get_mem_pool_id(&pool_tcm);
		if(nRet != QURT_EOK)
		{
			error(NULL) << "Failed to attach the TCM memory region. The error code is:" << nRet << "\n" ;
			nRet = E_ERR;
			return nRet;
		}

		tcm_buf_vaddr = dma_allocate_cache(pool_tcm, region_tcm_size, &region_tcm);
		tcm_desc_vaddr = dma_allocate_cache(pool_tcm, region_tcm_desc_size, &region_tcm_desc);

		// Lock the TCM region. This maps the region mapped as TCM to the actual TCM.
		// This is done to ensure the region is not invalidated during DMA processing.
		// This can also be done through the firmware (using the nDmaWrapper_LockCache function).
		nRet = dma_lock_cache(tcm_buf_vaddr, region_tcm_size);
		if(nRet != QURT_EOK)
		{
			error(NULL) << "QURT TCM lock failed due to QURT_EALIGN ERROR  \
					 misaligned u32Size = " << region_tcm_size << "\n";
			nRet = E_ERR;
			return nRet;
		}
		// Lock the descriptor region as well.
		nRet  = dma_lock_cache(tcm_desc_vaddr, region_tcm_desc_size);
		if(nRet != QURT_EOK)
		{
			error(NULL) << "QURT TCM lock failed due to QURT_EALIGN ERROR  \
					 misaligned u32Size = " << region_tcm_desc_size << "\n" ;
			nRet = E_ERR;
			return nRet;
		}

		if(halide_hexagon_dmart_set_fold_storage(user_context, tcm_buf_vaddr, region_tcm, \
				   tcm_buf_size, tcm_desc_vaddr, region_tcm_desc, region_tcm_desc_size, &fold_idx))
		{
			error(NULL) << "Function failed to Set Fold Storage to DMA context \n";
			return E_ERR;
		}
		/*Assign the Allocated Fold Storage to Device Memory*/
		buf->device = tcm_buf_vaddr;
	}
	/*Now We have Allocated the Fold and will link it to the Frame*/
	if(halide_hexagon_dmart_set_storage_linkage (user_context,(addr_t)buf->host, (addr_t) buf->device , fold_idx))
	{
		error(NULL) << "Function failed to Link Frame and Fold Storage \n";
		return E_ERR;
	}

	//Populate Work Descriptors and Prepare DMA
	//#################################################################
	t_dma_prepare_params params;
	params.handle = dma_handle;
	params.host_address =  (addr_t )buf->host;
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
	if(nRet != QURT_EOK)
	{
		error(NULL) << "Error in Preparing for DMA Transfer\n";
		return nRet;
	}
	return nRet;
}

int halide_dma_device_free(void *user_context, halide_buffer_t *buf)
{
	void* handle = NULL;
    halide_assert(NULL, user_context!=NULL);
    halide_assert(NULL, buf!=NULL);

	bool read_flag = false;
    if(halide_hexagon_dmart_is_buffer_read(user_context, (addr_t)buf->host, &read_flag))
    {
    	error(NULL) << "Function failed to get Operation type: (Read | Write) Flag \n";
    	return E_ERR;
    }


    if(read_flag)
    {
    	handle = halide_hexagon_dmart_get_read_handle(user_context,(addr_t)buf->host);
    	if(handle == 0)
    	{
    		error(NULL) << "Function failed to get DMA Read Handle  \n";
    	 	return E_ERR;
    	}
    }
    else
    {
    	handle = halide_hexagon_dmart_get_write_handle(user_context,(addr_t)buf->host);
    	if(handle == 0)
    	{
    		error(NULL) << "Function failed to get DMA Write Handle  \n";
			return E_ERR;
        }
    }

    if(handle != 0)
    {
    	bool last_frame;
		if(halide_hexagon_dmart_get_last_frame(user_context,(addr_t)buf->host,&last_frame))
		{
			error(NULL) << "Function failed to get if the Frame is last frame or not \n";
			return E_ERR;
		}
        dma_finish_frame(handle);
        if (last_frame)
        {			
			dma_free_dma_engine(handle);
    		/* Free the Allocated Qurt Memory Regions*/
			qurt_mem_region_t tcm_region,desc_region;
			qurt_addr_t desc_va;
			qurt_size_t desc_size, tcm_size;

			if(halide_hexagon_dmart_get_tcm_desc_params(user_context, (addr_t) buf->device,&tcm_region,
												&tcm_size,(addr_t *)&desc_va,&desc_region, &desc_size))
			{
				error(NULL) << "Function failed to get TCM Desc Params  \n";
				return E_ERR;
			}

			/* Release the TCM regions that were locked.*/
			/* This can also be done through the firmware (using the nDmaWrapper_UnlockCache function).*/
			int nRet  = dma_unlock_cache(buf->device, tcm_size);

			if(nRet != QURT_EOK)
			{
				error(NULL) << "QURT TCM unlock failed due to QURT_EALIGN \
						 ERROR misaligned u32Size = " << tcm_size << "\n";
				nRet = E_ERR;
				return nRet;
			}

			nRet  = dma_unlock_cache(desc_va, desc_size);

			if(nRet != QURT_EOK)
			{
				error(NULL) << "QURT TCM descriptor unlock failed QURT_EALIGN ERROR misaligned u32Size = " << desc_size << "\n";
				nRet = E_ERR;
				return nRet;
			}

			// Delete all regions that were allocated.
			dma_delete_mem_region(tcm_region);
			dma_delete_mem_region(desc_region);
        }
    }
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
	if(halide_hexagon_dmart_is_buffer_read(user_context,(addr_t )buf->host,&read_flag))
	{
		error(NULL) << "Function failed to get Operation type: (Read | Write) Flag \n";
		return E_ERR;
	}	
    
	if(read_flag)
	{
		handle = halide_hexagon_dmart_get_read_handle(user_context,(addr_t )buf->host);
		if(handle == NULL)
		{
			error(NULL) << "Function failed to get DMA Read Handle  \n";
			return E_ERR;
	    }
	}
	else
	{
		return E_ERR;
	}

	int ncomponents;
	if(halide_hexagon_dmart_get_num_components(user_context,(addr_t )buf->host,&ncomponents))
	{
		error(NULL) << "Function failed to get number of Components from DMA Context \n";
		return E_ERR;
	}

	t_dma_move_params move_param;
	halide_hexagon_dmart_get_update_params(user_context, (addr_t)buf->device, &move_param);

	move_param.handle = handle;
	move_param.ncomponents = ncomponents;
	nRet = dma_move_data(move_param);
	if(nRet != QURT_EOK)
		return E_ERR;

	return nRet;
}


int halide_dma_copy_to_host(void *user_context, halide_buffer_t *buf)
{
	void* handle;
	bool read_flag;
	if(halide_hexagon_dmart_is_buffer_read(user_context,(addr_t )buf->host,&read_flag))
	{
		error(NULL) << "Function failed to get Operation type: (Read | Write) Flag \n";
		return E_ERR;
	}

	if(!read_flag)
	{
		handle = halide_hexagon_dmart_get_write_handle(user_context,(addr_t )buf->host);
		if(handle == 0)
		{
			error(NULL) << "Function failed to get DMA Read Handle  \n";
			return E_ERR;
	    }
	}
	else
	{
		return E_ERR;
	}

	int ncomponents;
	if(halide_hexagon_dmart_get_num_components(user_context,(addr_t )buf->host,&ncomponents))
	{
		error(NULL) << "Function failed to get number of Components from DMA Context \n";
		return E_ERR;
	}

	t_dma_move_params move_param;
	halide_hexagon_dmart_get_update_params(user_context, (addr_t)buf->device, &move_param);

	move_param.handle = handle;
	move_param.ncomponents = ncomponents;

	int nRet;
	nRet = dma_move_data(move_param);
	if(nRet != QURT_EOK)
		return E_ERR;

    return nRet;
}


int halide_dma_device_sync(void* user_context,  halide_buffer_t *buf)
{
	int nRet;
	void* handle=NULL;
	bool read_flag;
	
	if(halide_hexagon_dmart_is_buffer_read(user_context,(addr_t )buf->host,&read_flag))
	{
		error(NULL) << "Function failed to get Operation type: (Read | Write) Flag \n";
		return E_ERR;
	}

	if(read_flag)
	{
    	handle = halide_hexagon_dmart_get_read_handle(user_context,(addr_t )buf->host);
		if(handle == 0)
		{
			error(NULL) << "Function failed to get DMA Read Handle  \n";
			return E_ERR;
		}
		nRet = dma_wait(handle);
	}
	else
	{
    	handle = halide_hexagon_dmart_get_write_handle(user_context,(addr_t )buf->host);
		if(handle == 0)
		{
			error(NULL) << "Function failed to get DMA Write  Handle  \n";
			return E_ERR;
		}
		nRet = dma_wait(handle);
	}
	return nRet;
}


int halide_dma_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf)
{
    int result = halide_dma_device_malloc(user_context, buf);
    return result;
}

int halide_dma_device_and_host_free(void *user_context, struct halide_buffer_t *buf)
{
    halide_dma_device_free(user_context, buf);
    buf->host = NULL;
    return 0;
}


WEAK const halide_device_interface_t *halide_hexagon_dma_device_interface()
{
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

