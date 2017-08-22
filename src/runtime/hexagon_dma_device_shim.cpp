/*!
 *  This file is a duplicate of the actual hexagon_dma_device_shim.cpp used to call the DMA driver functions
 * The definitions in this file a week reference so that these will be called only in case of unavailability of
 * actual DMA functions.
 * This file is need only if there is no hexagon SDK support or NO hexagon DMA support, in either csae we replace
 * the DMA operations with normal memory operations
 */

#ifndef _DMA_DEVICE_SHIM_C_
#define _DMA_DEVICE_SHIM_C_

#include "hexagon_mini_dma.h"
#include "mini_qurt.h"
#include "hexagon_dma_device_shim.h"

using namespace Halide::Runtime::Internal::Qurt;

typedef struct dma_dummy_lib
{
	int width;
	void* host_address;
}t_dma_dummy_lib;


WEAK int dma_is_dma_driver_ready()
{
	return QURT_EOK;
}

WEAK int dma_get_format_alignment(t_eDmaFmt eFmt, bool is_ubwc, t_dma_pix_align_info& pix_align)
{
	int nRet = 0;
	
	pix_align.u16H = 16;
	pix_align.u16W = 128;

	return nRet;
}

WEAK addr_t dma_lookup_physical_address(addr_t addr)
{
	return addr;
}

WEAK int dma_get_min_roi_size(t_eDmaFmt eFmt, bool isUbwc, t_dma_pix_align_info& pix_align)
{
	int nRet = 0;

	pix_align.u16H = 16;
	pix_align.u16W = 128;

	return nRet;
}

WEAK void* dma_allocate_dma_engine()
{
	t_dma_dummy_lib* dma_handle = NULL;

	dma_handle = (t_dma_dummy_lib *)malloc(sizeof(t_dma_dummy_lib));

	return (void*)dma_handle;
}  

WEAK qurt_size_t dma_get_descriptor_size(t_eDmaFmt* fmt_type, int ncomponents,int nfolds)
{
	qurt_size_t region_tcm_desc_size = 0;
	if(fmt_type != NULL)
	region_tcm_desc_size = ALIGN(64, 0x1000);
	return region_tcm_desc_size;
}

WEAK int dma_get_stride(t_eDmaFmt fmt_type, bool is_ubwc, t_dma_pix_align_info roi_dims)
{
	int stride = roi_dims.u16W;

	return stride;
}

WEAK int dma_get_mem_pool_id(qurt_mem_pool_t *mem_pool)
{
	int nRet = 0;
	*mem_pool = 1;
	
	return nRet;
}

WEAK qurt_addr_t dma_allocate_cache(qurt_mem_pool_t pool_tcm, qurt_size_t region_tcm_size, qurt_mem_region_t* region_tcm)
{
	unsigned char* buf_vaddr;
	
	buf_vaddr = (unsigned char*) malloc(region_tcm_size*sizeof(unsigned char*));
	
	if(region_tcm != 0)
	 *region_tcm = *reinterpret_cast<unsigned int*>(&buf_vaddr);
	
	memset(buf_vaddr, 0, region_tcm_size*sizeof(unsigned char*));
	qurt_addr_t buf_addr = *reinterpret_cast<unsigned int *>(&buf_vaddr);
	return buf_addr;
}

WEAK int dma_lock_cache(qurt_addr_t tcm_buf_vaddr, qurt_size_t region_tcm_size)
{
	int nRet  = QURT_EOK;
	//do nothing
	return nRet;
}

WEAK int dma_unlock_cache(qurt_addr_t tcm_buf_vaddr, qurt_size_t region_tcm_size)
{
	int nRet  = QURT_EOK;
	//do nothing
	return nRet;
}

WEAK int dma_prepare_for_transfer(t_dma_prepare_params param)
{
	int nRet  = QURT_EOK;
	t_dma_dummy_lib* dma_handle = (t_dma_dummy_lib*)param.handle;
	if(dma_handle != 0)
	{
		dma_handle->host_address = (void*)param.host_address;
		dma_handle->width = param.frame_width;
	}
//do Nothing
    return nRet;
}

WEAK int dma_wait(void* handle)
{
	int nRet = QURT_EOK;
    //do nothing
	return nRet;
}	

WEAK int dma_move_data(t_dma_move_params param)
{
	int nRet  = QURT_EOK;
	t_dma_dummy_lib* dma_handle = (t_dma_dummy_lib*)param.handle;
	if(dma_handle != 0)
    {
	   unsigned char* host_addr = (unsigned char*)dma_handle->host_address;
	   unsigned char* dest_addr = (unsigned char*)param.ping_buffer;
	   int x = param.xoffset;
	   int y = param.yoffset;
	   int w = param.roi_width;
	   int h = param.roi_height;
	   
	   unsigned int offset_buf = param.offset;
	   for(int xii=0;xii<h;xii++){
			for(int yii=0;yii<w;yii++){
				int xin = xii*w;  /* w should be Fold Offset */ 
	            int yin = yii;
							
				int RoiOffset = x+y*dma_handle->width;
				int xout = xii*dma_handle->width;
				int yout = yii;
							
				dest_addr[offset_buf+yin+xin] =  host_addr[RoiOffset + yout + xout ] ; /* Thid offset for Pinh Pong*/
	   		}
		}    
    }		
	return nRet;
}

WEAK int dma_free_dma_engine(void* handle)
{
	int nRet  = QURT_EOK;
	t_dma_dummy_lib* dma_handle = (t_dma_dummy_lib*)handle;
	
	if(dma_handle != 0)
	free(dma_handle);

	return nRet;
}


WEAK int dma_finish_frame(void* handle)
{
	int nRet  = QURT_EOK;
	//do nothing
	return nRet;
}

WEAK unsigned int dma_get_thread_id()
{
	static int i=0;
	i++;
	return i;
}


WEAK void dma_delete_mem_region(qurt_mem_region_t cache_mem)
{
    unsigned char* temp = reinterpret_cast<unsigned char*>(cache_mem);
	free(temp);
}
#endif
